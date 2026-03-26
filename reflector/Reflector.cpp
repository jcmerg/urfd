//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.

// urfd -- The universal reflector
// Copyright © 2021 Thomas A. Early N7TAE
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include <string.h>

#include "Global.h"

CReflector::CReflector()
{
	m_EchoModule = ' ';
	m_EchoHasHeader = false;
}

CReflector::~CReflector()
{
	keep_running = false;
	if ( m_MaintenanceFuture.valid() )
	{
		m_MaintenanceFuture.get();
	}
	if ( m_EchoFuture.valid() )
	{
		m_EchoFuture.get();
	}

	for (auto it=m_Modules.cbegin(); it!=m_Modules.cend(); it++)
	{
		if (m_RouterFuture[*it].valid())
			m_RouterFuture[*it].get();
	}
	m_RouterFuture.clear();
	m_Stream.clear();
}


////////////////////////////////////////////////////////////////////////////////////////
// operation

bool CReflector::Start(void)
{
	// get config stuff
	const auto cs(g_Configure.GetString(g_Keys.names.callsign));
	m_Callsign.SetCallsign(cs, false);
	m_Modules.assign(g_Configure.GetString(g_Keys.modules.modules));
	const auto tcmods(g_Configure.GetString(g_Keys.tc.modules));
	const auto port = g_Configure.GetUnsigned(g_Keys.tc.port);

#ifndef NO_DHT
	// start the dht instance
	refhash = dht::InfoHash::get(cs);
	node.run(17171, dht::crypto::generateIdentity(cs), true, 59973);
	node.bootstrap(g_Configure.GetString(g_Keys.names.bootstrap), "17171");
#endif

	// let's go!
	keep_running = true;

	// init transcoder comms
	if (port)
	{
		if (g_TCServer.Open(g_Configure.GetString(g_Keys.tc.bind), tcmods, port))
			return true;
	}

	// init gate keeper. It can only return true!
	g_GateKeeper.Init();

	// init dmrid directory. No need to check the return value.
	g_LDid.LookupInit();

	// init dmrid directory. No need to check the return value.
	g_LNid.LookupInit();

	// init wiresx node directory. Likewise with the return vale.
	g_LYtr.LookupInit();

	// create protocols
	if (! m_Protocols.Init())
	{
		m_Protocols.Close();
		return true;
	}

	// echo module
	if (g_Configure.Contains(g_Keys.echo.module)
		&& (!g_Configure.Contains(g_Keys.echo.enable) || g_Configure.GetBoolean(g_Keys.echo.enable)))
	{
		m_EchoModule = g_Configure.GetString(g_Keys.echo.module)[0];
		std::cout << "Echo module: " << m_EchoModule << std::endl;
	}

	// start one thread per reflector module
	for (auto c : m_Modules)
	{
		auto stream = std::make_shared<CPacketStream>(c);
		if (stream)
		{
			// if it's a transcoded module, then we need to initialize the codec stream
			if (port)
			{
				if (std::string::npos != tcmods.find(c))
				{
					if (stream->InitCodecStream())
						return true;
				}
			}
			m_Stream[c] = stream;
		}
		else
		{
			std::cerr << "Could not make a CPacketStream for module '" << c << "'" << std::endl;
			return true;
		}
		try
		{
			m_RouterFuture[c] = std::async(std::launch::async, &CReflector::RouterThread, this, c);
		}
		catch(const std::exception& e)
		{
			std::cerr << "Cannot start module '" << c << "' thread: " << e.what() << '\n';
			keep_running = false;
			return true;
		}
	}

	// start the reporting thread
	try
	{
		m_MaintenanceFuture = std::async(std::launch::async, &CReflector::MaintenanceThread, this);
	}
	catch(const std::exception& e)
	{
		std::cerr << "Cannot start the dashboard data report thread: " << e.what() << '\n';
	}

	// start echo thread
	if (m_EchoModule != ' ')
	{
		try
		{
			m_EchoFuture = std::async(std::launch::async, &CReflector::EchoThread, this);
		}
		catch(const std::exception& e)
		{
			std::cerr << "Cannot start echo thread: " << e.what() << '\n';
		}
	}

#ifndef NO_DHT
	PutDHTConfig();
#endif

	return false;
}

void CReflector::Stop(void)
{
	// stop & delete all threads
	keep_running = false;

	// stop transcoder comms
	// if it was never opened, then there is nothing to close;
	g_TCServer.Close();

	// stop & delete report threads
	if ( m_MaintenanceFuture.valid() )
	{
		m_MaintenanceFuture.get();
	}

	// stop echo thread
	if ( m_EchoFuture.valid() )
	{
		m_EchoFuture.get();
	}

	// stop & delete all router thread
	for (auto c : m_Modules)
	{
		if (m_RouterFuture[c].valid())
			m_RouterFuture[c].get();
	}

	// close protocols
	m_Protocols.Close();

	// close gatekeeper
	g_GateKeeper.Close();

	// close databases
	g_LDid.LookupClose();
	g_LNid.LookupClose();
	g_LYtr.LookupClose();

#ifndef NO_DHT
	// kill the DHT
	node.cancelPut(refhash, toUType(EUrfdValueID::Config));
	node.cancelPut(refhash, toUType(EUrfdValueID::Peers));
	node.cancelPut(refhash, toUType(EUrfdValueID::Clients));
	node.cancelPut(refhash, toUType(EUrfdValueID::Users));
	node.shutdown({}, true);
	node.join();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////
// stream opening & closing

bool CReflector::IsStreaming(char module)
{
	return false;
}

// clients MUST have bee locked by the caller so we can freely access it within the function
std::shared_ptr<CPacketStream> CReflector::OpenStream(std::unique_ptr<CDvHeaderPacket> &DvHeader, std::shared_ptr<CClient>client)
{
	// check sid is not zero
	if ( 0U == DvHeader->GetStreamId() )
	{
		std::cerr << "StreamId for client " << client->GetCallsign() << " is zero!" << std::endl;
		return nullptr;
	}

	// check if client is valid candidate
	if ( ! m_Clients.IsClient(client) || client->IsAMaster() )
	{
		return nullptr;
	}

	// check if no stream with same streamid already open
	// to prevent loops
	if ( IsStreamOpen(DvHeader) )
	{
		std::cerr << "Detected stream loop on module " << DvHeader->GetRpt2Module() << " for client " << client->GetCallsign() << " with sid " << DvHeader->GetStreamId() << std::endl;
		return nullptr;
	}

	// set the packet module
	DvHeader->SetPacketModule(client->GetReflectorModule());
	// get the module's queue
	char module = DvHeader->GetRpt2Module();
	auto stream = GetStream(module);
	if ( stream == nullptr )
	{
		std::cerr << "Can't find module '" << module << "' for Client " << client->GetCallsign() << std::endl;
		return nullptr;
	}

	// is it available ?
	if ( stream->OpenPacketStream(*DvHeader, client) )
	{
		// stream open, mark client as master
		// so that it can't be deleted
		client->SetMasterOfModule(module);

		// update last heard time
		client->Heard();

		// report
		std::cout << std::showbase << std::hex;
		std::cout << "Opening stream on module " << module << " for client " << client->GetCallsign() << " with sid " << ntohs(DvHeader->GetStreamId()) << " by user " << DvHeader->GetMyCallsign() << std::endl;
		std::cout << std::noshowbase << std::dec;

		// and push header packet
		stream->Push(std::move(DvHeader));

		// notify
		//OnStreamOpen(stream->GetUserCallsign());

	}
	return stream;
}

void CReflector::CloseStream(std::shared_ptr<CPacketStream> stream)
{
	if ( stream != nullptr )
	{
		// wait queue is empty. this waits forever
		bool bEmpty = stream->IsEmpty();
		while (! bEmpty)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			bEmpty = stream->IsEmpty();
		}

		GetClients();	// lock clients

		// get and check the master
		std::shared_ptr<CClient>client = stream->GetOwnerClient();
		if ( client != nullptr )
		{
			// client no longer a master
			client->NotAMaster();

			// notify
			//OnStreamClose(stream->GetUserCallsign());

			std::cout << "Closing stream of module " << GetStreamModule(stream) << std::endl;
		}

		// release clients
		ReleaseClients();

		// and stop the queue
		stream->ClosePacketStream();
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// router threads

void CReflector::RouterThread(const char ThisModule)
{
	auto pitem = m_Stream.find(ThisModule);
	if (m_Stream.end() == pitem)
	{
		std::cerr << "Module '" << ThisModule << " CPacketStream doesn't exist! aborting RouterThread()" << std::endl;
		return;
	}
	const auto streamIn = pitem->second;
	while (keep_running)
	{
		// wait until something shows up
		auto packet = streamIn->PopWait();

		packet->SetPacketModule(ThisModule);

		// iterate on all protocols
		m_Protocols.Lock();
		for ( auto it=m_Protocols.begin(); it!=m_Protocols.end(); it++ )
		{
			auto copy = packet->Copy();

			// if packet is header, update RPT2 according to protocol
			if ( copy->IsDvHeader() )
			{
				// make the protocol-patched reflector callsign
				CCallsign csRPT = (*it)->GetReflectorCallsign();
				csRPT.SetCSModule(ThisModule);
				// and put it in the copy
				(dynamic_cast<CDvHeaderPacket *>(copy.get()))->SetRpt2Callsign(csRPT);
			}

			(*it)->Push(std::move(copy));
		}
		m_Protocols.Unlock();

		// echo: buffer packets for echo module (skip echo playback packets)
		if (ThisModule == m_EchoModule && (packet->GetStreamId() < 0xEC00 || packet->GetStreamId() > 0xECFF))
		{
			std::lock_guard<std::mutex> lock(m_EchoMutex);
			if (packet->IsDvHeader())
			{
				m_EchoBuffer.clear();
				m_EchoHeader = *dynamic_cast<CDvHeaderPacket *>(packet.get());
				m_EchoHasHeader = true;
			}
			if (packet->IsDvFrame())
			{
				m_EchoBuffer.push_back(packet->Copy());
				m_EchoLastFrame = std::chrono::steady_clock::now();
			}
		}
	}
}

// Maintenance thread hands xml and/or json update,
// and also keeps the transcoder TCP port(s) connected
#define XML_UPDATE_PERIOD 10

void CReflector::MaintenanceThread()
{
	std::string xmlpath, jsonpath;
	if (g_Configure.Contains(g_Keys.files.xml))
		xmlpath.assign(g_Configure.GetString(g_Keys.files.xml));
	if (g_Configure.Contains(g_Keys.files.json))
		jsonpath.assign(g_Configure.GetString(g_Keys.files.json));
	auto tcport = g_Configure.GetUnsigned(g_Keys.tc.port);

	if (xmlpath.empty() && jsonpath.empty())
		return;	// nothing to do

	while (keep_running)
	{
		// report to xml file
		if (! xmlpath.empty())
		{
			std::ofstream xmlFile;
			xmlFile.open(xmlpath, std::ios::out | std::ios::trunc);
			if ( xmlFile.is_open() )
			{
				// write xml file
				WriteXmlFile(xmlFile);

				// and close file
				xmlFile.close();
			}
			else
			{
				std::cout << "Failed to open " << xmlpath  << std::endl;
			}
		}

		// json report
		if (!  jsonpath.empty())
		{
			nlohmann::json jreport;
			JsonReport(jreport);
			std::ofstream jsonFile;
			jsonFile.open(jsonpath, std::ios::out | std::ios::trunc);
			if (jsonFile.is_open())
			{
				jsonFile << jreport.dump();
				jsonFile.close();
			}
		}


		// and wait a bit and do something useful at the same time
		for (int i=0; i< XML_UPDATE_PERIOD*10 && keep_running; i++)
		{
			if (tcport && g_TCServer.AnyAreClosed())
			{
				if (g_TCServer.Accept())
				{
					std::cerr << "Unrecoverable error, aborting..." << std::endl;
					abort();
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// modules & queues

std::shared_ptr<CPacketStream> CReflector::GetStream(char module)
{
	auto it=m_Stream.find(module);
	if (it!=m_Stream.end())
		return it->second;

	return nullptr;
}

bool CReflector::IsStreamOpen(const std::unique_ptr<CDvHeaderPacket> &DvHeader)
{
	for (auto it=m_Stream.begin(); it!=m_Stream.end(); it++)
	{
		if ( (it->second->GetStreamId() == DvHeader->GetStreamId()) && (it->second->IsOpen()) )
			return true;
	}
	return false;
}

char CReflector::GetStreamModule(std::shared_ptr<CPacketStream> stream)
{
	for (auto it=m_Stream.begin(); it!=m_Stream.end(); it++)
	{
		if ( it->second == stream )
			return it->first;
	}
	return ' ';
}

////////////////////////////////////////////////////////////////////////////////////////
// report helpers

void CReflector::JsonReport(nlohmann::json &report)
{
	for (auto &item : g_Configure.GetData().items())
	{
		if (isupper(item.key().at(0)))
			report["Configure"][item.key()] = item.value();
	}

	report["Peers"] = nlohmann::json::array();
	auto peers = GetPeers();
	for (auto pit=peers->cbegin(); pit!=peers->cend(); pit++)
		(*pit)->JsonReport(report);
	ReleasePeers();

	report["Clients"] = nlohmann::json::array();
	auto clients = GetClients();
	for (auto cit=clients->cbegin(); cit!=clients->cend(); cit++)
		(*cit)->JsonReport(report);
	ReleaseClients();

	report["Users"] = nlohmann::json::array();
	auto users = GetUsers();
	for (auto uid=users->begin(); uid!=users->end(); uid++)
		(*uid).JsonReport(report);
	ReleaseUsers();
}

#define XML_PROTO_ENABLED(key) (!g_Configure.Contains(key) || g_Configure.GetBoolean(key))

void CReflector::WriteXmlFile(std::ofstream &xmlFile)
{
	// write header
	xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;

	// software version
	xmlFile << "<Version>" << g_Version << "</Version>" << std::endl;

	// reflector metadata
	xmlFile << "<Reflector>" << std::endl;
	xmlFile << "\t<Callsign>" << g_Configure.GetString(g_Keys.names.callsign) << "</Callsign>" << std::endl;
	if (g_Configure.Contains(g_Keys.names.country))
		xmlFile << "\t<Country>" << g_Configure.GetString(g_Keys.names.country) << "</Country>" << std::endl;
	if (g_Configure.Contains(g_Keys.names.sponsor))
		xmlFile << "\t<Sponsor>" << g_Configure.GetString(g_Keys.names.sponsor) << "</Sponsor>" << std::endl;
	if (g_Configure.Contains(g_Keys.names.url))
		xmlFile << "\t<DashboardURL>" << g_Configure.GetString(g_Keys.names.url) << "</DashboardURL>" << std::endl;
	if (g_Configure.Contains(g_Keys.names.email))
		xmlFile << "\t<Email>" << g_Configure.GetString(g_Keys.names.email) << "</Email>" << std::endl;
	xmlFile << "</Reflector>" << std::endl;

	// configured modules
	xmlFile << "<Modules>" << std::endl;
	auto modules = g_Configure.GetString(g_Keys.modules.modules);
	for (char m : modules)
	{
		if (m < 'A' || m > 'Z') continue;
		xmlFile << "<Module>" << std::endl;
		xmlFile << "\t<Name>" << m << "</Name>" << std::endl;
		auto descKey = g_Keys.modules.descriptor[m - 'A'];
		if (g_Configure.Contains(descKey))
			xmlFile << "\t<Description>" << g_Configure.GetString(descKey) << "</Description>" << std::endl;
		// auto-set Echo description
		if (g_Configure.Contains(g_Keys.echo.module) && g_Configure.GetString(g_Keys.echo.module)[0] == m)
			xmlFile << "\t<Echo>true</Echo>" << std::endl;
		// count connected nodes on this module
		int nodeCount = 0;
		CClients *modClients = GetClients();
		for (auto cit = modClients->cbegin(); cit != modClients->cend(); cit++)
		{
			if ((*cit)->IsNode() && (*cit)->GetReflectorModule() == m)
				nodeCount++;
		}
		ReleaseClients();
		xmlFile << "\t<LinkedNodes>" << nodeCount << "</LinkedNodes>" << std::endl;
		// check if transcoded
		auto tcmods = g_Configure.GetString(g_Keys.tc.modules);
		bool transcoded = (tcmods.find(m) != std::string::npos);
		xmlFile << "\t<Transcoded>" << (transcoded ? "true" : "false") << "</Transcoded>" << std::endl;
		// standard protocol IDs for this module
		int modIdx = m - 'A';
		if (XML_PROTO_ENABLED(g_Keys.dmrplus.enable))
			xmlFile << "\t<DMRplus>" << (4001 + modIdx) << "</DMRplus>" << std::endl;
		if (XML_PROTO_ENABLED(g_Keys.ysf.enable))
			xmlFile << "\t<YSFDGID>" << (10 + modIdx) << "</YSFDGID>" << std::endl;
		// per-module protocol mappings (only if protocol is enabled)
		if (XML_PROTO_ENABLED(g_Keys.ysf.enable)
			&& g_Configure.Contains(g_Keys.ysf.autolinkmod) && g_Configure.GetString(g_Keys.ysf.autolinkmod)[0] == m)
		{
			xmlFile << "\t<Mapping><Protocol>YSF</Protocol><Type>AutoLink</Type>";
			if (g_Configure.Contains(g_Keys.ysf.ysfreflectordb.id))
				xmlFile << "<ID>" << g_Configure.GetUnsigned(g_Keys.ysf.ysfreflectordb.id) << "</ID>";
			if (g_Configure.Contains(g_Keys.ysf.ysfreflectordb.name))
				xmlFile << "<RemoteName>" << g_Configure.GetString(g_Keys.ysf.ysfreflectordb.name) << "</RemoteName>";
			xmlFile << "</Mapping>" << std::endl;
		}
		if (XML_PROTO_ENABLED(g_Keys.nxdn.enable)
			&& g_Configure.Contains(g_Keys.nxdn.autolinkmod) && g_Configure.GetString(g_Keys.nxdn.autolinkmod)[0] == m)
		{
			xmlFile << "\t<Mapping><Protocol>NXDN</Protocol><Type>AutoLink</Type>";
			if (g_Configure.Contains(g_Keys.nxdn.reflectorid))
				xmlFile << "<ID>" << g_Configure.GetUnsigned(g_Keys.nxdn.reflectorid) << "</ID>";
			xmlFile << "</Mapping>" << std::endl;
		}
		if (XML_PROTO_ENABLED(g_Keys.p25.enable)
			&& g_Configure.Contains(g_Keys.p25.autolinkmod) && g_Configure.GetString(g_Keys.p25.autolinkmod)[0] == m)
		{
			xmlFile << "\t<Mapping><Protocol>P25</Protocol><Type>AutoLink</Type>";
			if (g_Configure.Contains(g_Keys.p25.reflectorid))
				xmlFile << "<ID>" << g_Configure.GetUnsigned(g_Keys.p25.reflectorid) << "</ID>";
			xmlFile << "</Mapping>" << std::endl;
		}
		if (g_Configure.Contains(g_Keys.usrp.enable) && g_Configure.GetBoolean(g_Keys.usrp.enable)
			&& g_Configure.Contains(g_Keys.usrp.module) && g_Configure.GetString(g_Keys.usrp.module)[0] == m)
		{
			xmlFile << "\t<Mapping><Protocol>USRP</Protocol><Type>Bridge</Type>";
			if (g_Configure.Contains(g_Keys.usrp.callsign))
				xmlFile << "<RemoteName>" << g_Configure.GetString(g_Keys.usrp.callsign) << "</RemoteName>";
			xmlFile << "</Mapping>" << std::endl;
		}
		// BMMmdvm TG mappings for this module
		if (g_Configure.Contains(g_Keys.bmhb.enable) && g_Configure.GetBoolean(g_Keys.bmhb.enable))
		{
			const auto &jdata = g_Configure.GetData();
			for (auto it = jdata.begin(); it != jdata.end(); ++it)
			{
				const std::string &key = it.key();
				if (key.substr(0, 6) == "bmhbTG")
				{
					try {
						std::string val = it.value().get<std::string>();
						if (val.size() >= 1 && val[0] == m)
						{
							uint32_t tg = std::stoul(key.substr(6));
							std::string ts = "TS2";
							auto comma = val.find(',');
							if (comma != std::string::npos)
								ts = val.substr(comma + 1);
							xmlFile << "\t<Mapping><Protocol>BMMmdvm</Protocol><Type>TG</Type>";
							xmlFile << "<ID>" << tg << "</ID>";
							xmlFile << "<Timeslot>" << ts << "</Timeslot>";
							xmlFile << "</Mapping>" << std::endl;
						}
					} catch (...) {}
				}
			}
		}
		xmlFile << "</Module>" << std::endl;
	}
	xmlFile << "</Modules>" << std::endl;

	// enabled protocols (only show if enabled; default=true for backwards compat)
	xmlFile << "<Protocols>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.dextra.enable))
		xmlFile << "<Protocol><Name>DExtra</Name><Port>" << g_Configure.GetUnsigned(g_Keys.dextra.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.dplus.enable))
		xmlFile << "<Protocol><Name>DPlus</Name><Port>" << g_Configure.GetUnsigned(g_Keys.dplus.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.dcs.enable))
		xmlFile << "<Protocol><Name>DCS</Name><Port>" << g_Configure.GetUnsigned(g_Keys.dcs.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.mmdvm.enable))
		xmlFile << "<Protocol><Name>MMDVM</Name><Port>" << g_Configure.GetUnsigned(g_Keys.mmdvm.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.dmrplus.enable))
		xmlFile << "<Protocol><Name>DMRPlus</Name><Port>" << g_Configure.GetUnsigned(g_Keys.dmrplus.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.m17.enable))
		xmlFile << "<Protocol><Name>M17</Name><Port>" << g_Configure.GetUnsigned(g_Keys.m17.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.ysf.enable))
		xmlFile << "<Protocol><Name>YSF</Name><Port>" << g_Configure.GetUnsigned(g_Keys.ysf.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.p25.enable))
		xmlFile << "<Protocol><Name>P25</Name><Port>" << g_Configure.GetUnsigned(g_Keys.p25.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.nxdn.enable))
		xmlFile << "<Protocol><Name>NXDN</Name><Port>" << g_Configure.GetUnsigned(g_Keys.nxdn.port) << "</Port></Protocol>" << std::endl;
	if (XML_PROTO_ENABLED(g_Keys.urf.enable))
		xmlFile << "<Protocol><Name>URF</Name><Port>" << g_Configure.GetUnsigned(g_Keys.urf.port) << "</Port></Protocol>" << std::endl;
	if (g_Configure.Contains(g_Keys.bm.enable) && g_Configure.GetBoolean(g_Keys.bm.enable))
		xmlFile << "<Protocol><Name>BM</Name><Port>" << g_Configure.GetUnsigned(g_Keys.bm.port) << "</Port></Protocol>" << std::endl;
	if (g_Configure.Contains(g_Keys.usrp.enable) && g_Configure.GetBoolean(g_Keys.usrp.enable))
		xmlFile << "<Protocol><Name>USRP</Name><Port>" << g_Configure.GetUnsigned(g_Keys.usrp.rxport) << "</Port></Protocol>" << std::endl;
	if (g_Configure.Contains(g_Keys.g3.enable) && g_Configure.GetBoolean(g_Keys.g3.enable))
		xmlFile << "<Protocol><Name>G3</Name><Port>40000</Port></Protocol>" << std::endl;
	if (g_Configure.Contains(g_Keys.bmhb.enable) && g_Configure.GetBoolean(g_Keys.bmhb.enable))
		xmlFile << "<Protocol><Name>BMMmdvm</Name><Port>" << (g_Configure.Contains(g_Keys.bmhb.localport) ? g_Configure.GetUnsigned(g_Keys.bmhb.localport) : 0) << "</Port></Protocol>" << std::endl;
	xmlFile << "</Protocols>" << std::endl;

	CCallsign cs = m_Callsign;
	cs.PatchCallsign(0, "XLX", 3);

	// linked peers
	xmlFile << "<" << cs << "linked peers>" << std::endl;
	// lock
	CPeers *peers = GetPeers();
	// iterate on peers
	for ( auto pit=peers->cbegin(); pit!=peers->cend(); pit++ )
	{
		(*pit)->WriteXml(xmlFile);
	}
	// unlock
	ReleasePeers();
	xmlFile << "</" << cs << "linked peers>" << std::endl;

	// linked nodes
	xmlFile << "<" << cs << "linked nodes>" << std::endl;
	// lock
	CClients *clients = GetClients();
	// iterate on clients
	for ( auto cit=clients->cbegin(); cit!=clients->cend(); cit++ )
	{
		if ( (*cit)->IsNode() )
		{
			(*cit)->WriteXml(xmlFile);
		}
	}
	// unlock
	ReleaseClients();
	xmlFile << "</" << cs << "linked nodes>" << std::endl;

	// last heard users
	xmlFile << "<" << cs << "heard users>" << std::endl;
	// lock
	CUsers *users = GetUsers();
	// iterate on users
	for ( auto it=users->begin(); it!=users->end(); it++ )
	{
		it->WriteXml(xmlFile);
	}
	// unlock
	ReleaseUsers();
	xmlFile << "</" << cs << "heard users>" << std::endl;
}

////////////////////////////////////////////////////////////////////////////////////////
// echo thread

void CReflector::EchoThread(void)
{
	std::cout << "Echo thread started for module " << m_EchoModule << std::endl;

	while (keep_running)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		std::vector<std::unique_ptr<CPacket>> playback;
		CDvHeaderPacket header;
		bool doPlayback = false;

		{
			std::lock_guard<std::mutex> lock(m_EchoMutex);
			if (m_EchoHasHeader && !m_EchoBuffer.empty())
			{
				auto elapsed = std::chrono::steady_clock::now() - m_EchoLastFrame;
				if (elapsed > std::chrono::seconds(3))
				{
					header = m_EchoHeader;
					playback = std::move(m_EchoBuffer);
					m_EchoBuffer.clear();
					m_EchoHasHeader = false;
					doPlayback = true;
				}
			}
		}

		if (doPlayback && !playback.empty())
		{
			std::cout << "Echo: playing back " << playback.size() << " frames on module " << m_EchoModule << std::endl;

			// get the stream for the echo module
			auto streamIt = m_Stream.find(m_EchoModule);
			if (streamIt == m_Stream.end())
				continue;
			auto stream = streamIt->second;

			// wait for any current stream to finish
			while (stream->IsOpen() && keep_running)
				std::this_thread::sleep_for(std::chrono::milliseconds(50));

			if (!keep_running) break;

			// generate a new stream ID
			static uint16_t echoStreamId = 0xEC00;
			echoStreamId++;
			if (echoStreamId == 0) echoStreamId = 0xEC00;

			// create echo header - swap MY callsign to show "ECHO"
			CCallsign echoCs;
			echoCs.SetCallsign("ECHO    ", false);
			CCallsign rpt1 = header.GetRpt1Callsign();
			CCallsign rpt2 = header.GetRpt2Callsign();
			auto echoHeader = std::unique_ptr<CDvHeaderPacket>(
				new CDvHeaderPacket(echoCs, header.GetMyCallsign(), rpt1, rpt2, echoStreamId, (uint8_t)0)
			);
			echoHeader->SetPacketModule(m_EchoModule);

			// push header to stream
			stream->Push(std::move(echoHeader));

			// push voice frames with 20ms spacing
			for (size_t i = 0; i < playback.size() && keep_running; i++)
			{
				playback[i]->SetStreamId(echoStreamId);
				playback[i]->SetPacketModule(m_EchoModule);
				if (i == playback.size() - 1)
					playback[i]->SetLastPacket(true);
				stream->Push(std::move(playback[i]));
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}

			std::cout << "Echo: playback complete" << std::endl;
		}
	}

	std::cout << "Echo thread stopped" << std::endl;
}

#ifndef NO_DHT
// DHT put() and get()
void CReflector::PutDHTConfig()
{
	const std::string cs(g_Configure.GetString(g_Keys.names.callsign));
	SUrfdConfig1 cfg;
	time(&cfg.timestamp);
	cfg.callsign.assign(cs);
	cfg.ipv4addr.assign(g_Configure.GetString(g_Keys.ip.ipv4address));
	cfg.ipv6addr.assign(g_Configure.GetString(g_Keys.ip.ipv6address));
	cfg.modules.assign(g_Configure.GetString(g_Keys.modules.modules));
	if (g_Configure.GetUnsigned(g_Keys.tc.port))
		cfg.transcodedmods.assign(g_Configure.GetString(g_Keys.tc.modules));
	cfg.url.assign(g_Configure.GetString(g_Keys.names.url));
	cfg.email.assign(g_Configure.GetString(g_Keys.names.email));
	cfg.country.assign(g_Configure.GetString(g_Keys.names.country));
	cfg.sponsor.assign(g_Configure.GetString(g_Keys.names.sponsor));
	std::ostringstream ss;
	ss << g_Version;
	cfg.version.assign(ss.str());
	cfg.almod[toUType(EUrfdAlMod::nxdn)]   = g_Configure.GetAutolinkModule(g_Keys.nxdn.autolinkmod);
	cfg.almod[toUType(EUrfdAlMod::p25)]    = g_Configure.GetAutolinkModule(g_Keys.p25.autolinkmod);
	cfg.almod[toUType(EUrfdAlMod::ysf)]    = g_Configure.GetAutolinkModule(g_Keys.ysf.autolinkmod);
	cfg.ysffreq[toUType(EUrfdTxRx::rx)]    = g_Configure.GetUnsigned(g_Keys.ysf.defaultrxfreq);
	cfg.ysffreq[toUType(EUrfdTxRx::tx)]    = g_Configure.GetUnsigned(g_Keys.ysf.defaulttxfreq);
	cfg.refid[toUType(EUrfdRefId::nxdn)]   = g_Configure.GetUnsigned(g_Keys.nxdn.reflectorid);
	cfg.refid[toUType(EUrfdRefId::p25)]    = g_Configure.GetUnsigned(g_Keys.p25.reflectorid);
	cfg.port[toUType(EUrfdPorts::dcs)]     = (uint16_t)g_Configure.GetUnsigned(g_Keys.dcs.port);
	cfg.port[toUType(EUrfdPorts::dextra)]  = (uint16_t)g_Configure.GetUnsigned(g_Keys.dextra.port);
	cfg.port[toUType(EUrfdPorts::dmrplus)] = (uint16_t)g_Configure.GetUnsigned(g_Keys.dmrplus.port);
	cfg.port[toUType(EUrfdPorts::dplus)]   = (uint16_t)g_Configure.GetUnsigned(g_Keys.dplus.port);
	cfg.port[toUType(EUrfdPorts::m17)]     = (uint16_t)g_Configure.GetUnsigned(g_Keys.m17.port);
	cfg.port[toUType(EUrfdPorts::mmdvm)]   = (uint16_t)g_Configure.GetUnsigned(g_Keys.mmdvm.port);
	cfg.port[toUType(EUrfdPorts::nxdn)]    = (uint16_t)g_Configure.GetUnsigned(g_Keys.nxdn.port);
	cfg.port[toUType(EUrfdPorts::p25)]     = (uint16_t)g_Configure.GetUnsigned(g_Keys.p25.port);
	cfg.port[toUType(EUrfdPorts::urf)]     = (uint16_t)g_Configure.GetUnsigned(g_Keys.urf.port);
	cfg.port[toUType(EUrfdPorts::ysf)]     = (uint16_t)g_Configure.GetUnsigned(g_Keys.ysf.port);
	cfg.g3enabled = g_Configure.GetBoolean(g_Keys.g3.enable);
	for (const auto m : cfg.modules)
		cfg.description[m] = g_Configure.GetString(g_Keys.modules.descriptor[m-'A']);

	auto nv = std::make_shared<dht::Value>(cfg);
	nv->user_type.assign(URFD_CONFIG_1);
	nv->id = toUType(EUrfdValueID::Config);

	node.putSigned(
		refhash,
		nv,
#ifdef DEBUG
		[](bool success){ std::cout << "PutDHTConfig() " << (success ? "successful" : "unsuccessful") << std::endl; },
#else
		[](bool success){ if(! success) std::cout << "PutDHTConfig() unsuccessful" << std::endl; },
#endif
		true
	);
}

void CReflector::GetDHTConfig(const std::string &cs)
{
	static SUrfdConfig1 cfg;
	cfg.timestamp = 0;	// every time this is called, zero the timestamp

	std::cout << "Getting " << cs << " connection info..." << std::endl;

	// we only want the configuration section of the reflector's document
	dht::Where w;
	w.id(toUType(EUrfdValueID::Config));

	node.get(
		dht::InfoHash::get(cs),
		[](const std::shared_ptr<dht::Value> &v) {
			if (0 == v->user_type.compare(URFD_CONFIG_1))
			{
				auto rdat = dht::Value::unpack<SUrfdConfig1>(*v);
				if (rdat.timestamp > cfg.timestamp)
				{
					// the time stamp is the newest so far, so put it in the static cfg struct
					cfg = dht::Value::unpack<SUrfdConfig1>(*v);
				}
			}
			else
			{
				std::cerr << "Get() returned unknown user_type: '" << v->user_type << "'" << std::endl;
			}
			return true;	// check all the values returned
		},
		[](bool success) {
			if (success)
			{
				if (cfg.timestamp)
				{
					// if the get() call was successful and there is a nonzero timestamp, then do the update
					g_GateKeeper.GetInterlinkMap()->Update(cfg.callsign, cfg.modules, cfg.ipv4addr, cfg.ipv6addr, cfg.port[toUType(EUrfdPorts::urf)], cfg.transcodedmods);
					g_GateKeeper.ReleaseInterlinkMap();
				}
				else
				{
					std::cerr << "node.Get() was successful, but the timestamp was zero" << std::endl;
				}
			}
			else
			{
				std::cout << "Get() was unsuccessful" << std::endl;
			}
		},
		{}, // empty filter
		w	// just the configuration section
	);
}

#endif
