// YSFClientProtocol -- Connects to remote YSF reflectors as a client node
// URFD acts as a YSF hotspot/node connecting to external YSF reflectors.
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include <string.h>
#include <sstream>

#include "Global.h"
#include "YSFClientPeer.h"
#include "YSFClientProtocol.h"

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CYsfClientProtocol::Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	// client callsign
	if (g_Configure.Contains(g_Keys.ysfclient.callsign))
		m_ClientCallsign.SetCallsign(g_Configure.GetString(g_Keys.ysfclient.callsign), false);
	else
		m_ClientCallsign = m_ReflectorCallsign;

	// prepare 10-char space-padded callsign for YSF packets
	memset(m_szCallsign, ' ', YSF_CALLSIGN_LENGTH);
	m_szCallsign[YSF_CALLSIGN_LENGTH] = 0;
	m_ClientCallsign.GetCallsignString(m_szCallsign);
	m_szCallsign[::strlen(m_szCallsign)] = ' ';

	// Parse BlockProtocols
	if (g_Configure.Contains(g_Keys.ysfclient.blockprotocols))
	{
		const std::map<std::string, EProtocol> protoMap = {
			{"SvxReflector", EProtocol::svxreflector}, {"DExtra", EProtocol::dextra},
			{"DPlus", EProtocol::dplus}, {"DCS", EProtocol::dcs},
			{"DCSClient", EProtocol::dcsclient}, {"DExtraClient", EProtocol::dextraclient},
			{"DPlusClient", EProtocol::dplusclient}, {"YSFClient", EProtocol::ysfclient},
			{"DMRPlus", EProtocol::dmrplus}, {"MMDVM", EProtocol::dmrmmdvm},
			{"MMDVMClient", EProtocol::mmdvmclient}, {"YSF", EProtocol::ysf},
			{"M17", EProtocol::m17},
			{"NXDN", EProtocol::nxdn}, {"P25", EProtocol::p25},
			{"USRP", EProtocol::usrp}, {"URF", EProtocol::urf},
			{"XLXPeer", EProtocol::xlxpeer}, {"G3", EProtocol::g3},
		};
		std::istringstream ss(g_Configure.GetString(g_Keys.ysfclient.blockprotocols));
		std::string token;
		while (std::getline(ss, token, ','))
		{
			token.erase(0, token.find_first_not_of(" \t"));
			token.erase(token.find_last_not_of(" \t") + 1);
			auto it = protoMap.find(token);
			if (it != protoMap.end())
			{
				m_BlockedSources.insert(it->second);
				std::cout << "YSFClient: blocking protocol " << token << std::endl;
			}
			else if (!token.empty())
				std::cerr << "YSFClient: unknown protocol in BlockProtocols: " << token << std::endl;
		}
	}

	// Block self-routing
	m_BlockedSources.insert(EProtocol::ysfclient);
	SaveBlockDefaults();

	// Parse static module mappings: Map1=host,port,localmod
	auto &cfgData = g_Configure.GetData();
	for (auto it = cfgData.begin(); it != cfgData.end(); ++it)
	{
		if (it.key().substr(0, 12) == "ysfClientMap")
		{
			std::istringstream ss(it.value().get<std::string>());
			std::string host, portStr, localModStr;
			// Map<N> = host,port,localmod[,dgid]
			if (std::getline(ss, host, ',') && std::getline(ss, portStr, ',') &&
				std::getline(ss, localModStr, ','))
			{
				std::string dgidStr;
				std::getline(ss, dgidStr, ',');  // optional DG-ID

				host.erase(0, host.find_first_not_of(" \t"));
				host.erase(host.find_last_not_of(" \t") + 1);
				portStr.erase(0, portStr.find_first_not_of(" \t"));
				localModStr.erase(0, localModStr.find_first_not_of(" \t"));
				dgidStr.erase(0, dgidStr.find_first_not_of(" \t"));
				dgidStr.erase(dgidStr.find_last_not_of(" \t") + 1);

				uint16_t p = (uint16_t)std::stoul(portStr);
				char localMod = localModStr[0];
				uint8_t dgid = dgidStr.empty() ? 0 : (uint8_t)std::stoul(dgidStr);

				if (IsLetter(localMod) && g_Reflector.IsValidModule(localMod))
				{
					SYsfClientMapping mapping;
					mapping.host = host;
					mapping.port = p;
					mapping.ip = CIp(host.c_str(), AF_INET, SOCK_DGRAM, p);
					mapping.localModule = localMod;
					mapping.dgid = dgid;
					mapping.connected = false;
					m_Mappings.push_back(mapping);
					std::cout << "YSFClient: mapping " << host << ":" << p
					          << " -> local " << localMod;
					if (dgid > 0)
						std::cout << " (DG-ID " << (int)dgid << ")";
					std::cout << std::endl;
				}
				else
					std::cerr << "YSFClient: invalid mapping: " << it.value() << std::endl;
			}
		}
	}

	if (!CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	std::cout << "YSFClient: initialized with callsign " << m_ClientCallsign
	          << ", " << m_Mappings.size() << " mapping(s)" << std::endl;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// task

void CYsfClientProtocol::Task(void)
{
	CBuffer Buffer;
	CIp     Ip;

	if (Receive4(Buffer, Ip, 20))
	{
		HandleIncoming(Buffer, Ip);
	}

	// handle admin-requested reconnect
	if (m_ReconnectRequested.exchange(false))
	{
		std::cout << "YSFClient: reconnect requested" << std::endl;
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		for (auto &m : m_Mappings)
		{
			if (m.connected)
			{
				SendDisconnect(m);
				m.connected = false;
				CClients *clients = g_Reflector.GetClients();
				auto client = clients->FindClient(m.ip, EProtocol::ysfclient);
				if (client)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();
			}
			m.pollTimer.start();
		}
	}

	HandleConnections();
	CheckStreamsTimeout();
	HandleQueue();
}

////////////////////////////////////////////////////////////////////////////////////////
// connection management

void CYsfClientProtocol::HandleConnections(void)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);

	for (auto &m : m_Mappings)
	{
		if (m.connected)
		{
			// send poll keepalive
			if (m.pollTimer.time() > YSFCLI_POLL_PERIOD)
			{
				SendPoll(m);
				m.pollTimer.start();
			}

			// check timeout
			if (m.timeoutTimer.time() > YSFCLI_KEEPALIVE_TIMEOUT)
			{
				std::cout << "YSFClient: keepalive timeout for " << m.host << std::endl;
				m.connected = false;

				CClients *clients = g_Reflector.GetClients();
				auto client = clients->FindClient(m.ip, EProtocol::ysfclient);
				if (client)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();

				m.pollTimer.start();
			}
		}
		else
		{
			// try to connect (send poll — YSF has no separate connect packet)
			if (m.pollTimer.time() > YSFCLI_RECONNECT_PERIOD)
			{
				m.ip = CIp(m.host.c_str(), AF_INET, SOCK_DGRAM, m.port);
				SendPoll(m);
				m.pollTimer.start();
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// incoming packet handler

void CYsfClientProtocol::HandleIncoming(const CBuffer &Buffer, const CIp &Ip)
{
	CCallsign Callsign;
	CYSFFICH  Fich;
	std::unique_ptr<CDvHeaderPacket> Header;
	std::array<std::unique_ptr<CDvFramePacket>, 5> Frames;
	std::unique_ptr<CDvFramePacket> OneFrame, LastFrame;

	// poll response = connection confirmed
	if (IsValidPollPacket(Buffer, &Callsign))
	{
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SYsfClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping)
		{
			mapping->timeoutTimer.start();

			if (!mapping->connected)
			{
				mapping->connected = true;
				mapping->pollTimer.start();
				std::cout << "YSFClient: connected to " << mapping->host
				          << " (local module " << mapping->localModule << ")" << std::endl;

				// create virtual client
				CCallsign cs(m_ClientCallsign);
				cs.SetCSModule(mapping->localModule);
				g_Reflector.GetClients()->AddClient(
					std::make_shared<CYSFClientPeer>(cs, Ip, mapping->localModule));
				g_Reflector.ReleaseClients();
			}

			// keep virtual client alive
			CClients *clients = g_Reflector.GetClients();
			auto it = clients->begin();
			std::shared_ptr<CClient> client = nullptr;
			while ((client = clients->FindNextClient(EProtocol::ysfclient, it)) != nullptr)
			{
				if (client->GetIp() == Ip)
					client->Alive();
			}
			g_Reflector.ReleaseClients();
		}
	}
	else if (IsValidDvPacket(Buffer, &Fich))
	{
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SYsfClientMapping *mapping = FindMappingByIp(Ip);
		if (!mapping || !mapping->connected)
			return;

		if (IsValidDvHeaderPacket(Ip, Fich, Buffer, Header, Frames))
		{
			// remap to local module
			Header->SetRpt2Module(mapping->localModule);
			CCallsign rpt1(m_ClientCallsign);
			rpt1.SetCSModule(mapping->localModule);
			Header->SetRpt1Callsign(rpt1);

			if (g_GateKeeper.MayTransmit(Header->GetMyCallsign(), Ip, EProtocol::ysfclient, mapping->localModule))
			{
				OnDvHeaderPacketIn(Header, Ip);
			}

			// push initial frames
			for (int i = 0; i < 2; i++)
			{
				if (Frames[i])
					OnDvFramePacketIn(Frames[i], &Ip);
			}
		}
		else if (IsValidDvFramePacket(Ip, Fich, Buffer, Header, Frames))
		{
			// late entry header?
			if (Header)
			{
				Header->SetRpt2Module(mapping->localModule);
				CCallsign rpt1(m_ClientCallsign);
				rpt1.SetCSModule(mapping->localModule);
				Header->SetRpt1Callsign(rpt1);

				if (g_GateKeeper.MayTransmit(Header->GetMyCallsign(), Ip, EProtocol::ysfclient, mapping->localModule))
				{
					OnDvHeaderPacketIn(Header, Ip);
				}
			}

			// push 5 voice frames
			for (int i = 0; i < 5; i++)
			{
				if (Frames[i])
					OnDvFramePacketIn(Frames[i], &Ip);
			}
		}
		else if (IsValidDvLastFramePacket(Ip, Fich, Buffer, OneFrame, LastFrame))
		{
			if (OneFrame)
				OnDvFramePacketIn(OneFrame, &Ip);
			if (LastFrame)
				OnDvFramePacketIn(LastFrame, &Ip);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// stream helpers

void CYsfClientProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
{
	auto stream = GetStream(Header->GetStreamId(), &Ip);
	if (stream)
	{
		stream->Tickle();
		return;
	}

	CCallsign my(Header->GetMyCallsign());
	CCallsign rpt1(Header->GetRpt1Callsign());
	CCallsign rpt2(Header->GetRpt2Callsign());
	char module = Header->GetRpt2Module();

	CClients *clients = g_Reflector.GetClients();

	auto it = clients->begin();
	std::shared_ptr<CClient> client = nullptr;
	while ((client = clients->FindNextClient(EProtocol::ysfclient, it)) != nullptr)
	{
		if (client->GetIp() == Ip && client->GetReflectorModule() == module)
			break;
	}

	if (!client)
		client = clients->FindClient(Ip, EProtocol::ysfclient);

	if (client)
	{
		rpt1 = client->GetCallsign();
		client->Alive();
		if ((stream = g_Reflector.OpenStream(Header, client)) != nullptr)
		{
			m_Streams[stream->GetStreamId()] = stream;
		}
	}

	g_Reflector.ReleaseClients();

	g_Reflector.GetUsers()->Hearing(my, rpt1, rpt2, "YSFClient");
	g_Reflector.ReleaseUsers();
}

////////////////////////////////////////////////////////////////////////////////////////
// outbound queue: Reflector -> remote YSF reflector

void CYsfClientProtocol::HandleQueue(void)
{
	while (!m_Queue.IsEmpty())
	{
		auto packet = m_Queue.Pop();
		const auto module = packet->GetPacketModule();

		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SYsfClientMapping *mapping = FindMappingByLocal(module);
		if (!mapping || !mapping->connected)
			continue;

		CBuffer buffer;

		if (packet->IsDvHeader())
		{
			m_StreamsCache[module].header = CDvHeaderPacket((CDvHeaderPacket &)*packet.get());
			m_StreamsCache[module].dgid = mapping->dgid;

			CCallsign rpt1(m_ClientCallsign);
			rpt1.SetCSModule(mapping->localModule);
			m_StreamsCache[module].header.SetRpt1Callsign(rpt1);

			EncodeYSFHeaderPacket(m_StreamsCache[module].header, mapping->dgid, &buffer);
		}
		else if (packet->IsLastPacket())
		{
			EncodeLastYSFPacket(m_StreamsCache[module].header, m_StreamsCache[module].dgid, &buffer);
		}
		else
		{
			uint8_t sid = packet->GetYsfPacketSubId();
			if (sid <= 4)
			{
				m_StreamsCache[module].frames[sid] = CDvFramePacket((CDvFramePacket &)*packet.get());
				if (sid == 4)
				{
					EncodeYSFPacket(m_StreamsCache[module].header, m_StreamsCache[module].frames, m_StreamsCache[module].dgid, &buffer);
				}
			}
		}

		if (buffer.size() > 0)
		{
			Send(buffer, mapping->ip);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// admin API

bool CYsfClientProtocol::AddMapping(const std::string &host, uint16_t port, char localMod, uint8_t dgid)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);

	for (auto &m : m_Mappings)
		if (m.localModule == localMod)
			return false;

	if (!g_Reflector.IsValidModule(localMod))
		return false;

	SYsfClientMapping mapping;
	mapping.host = host;
	mapping.port = port;
	mapping.ip = CIp(host.c_str(), AF_INET, SOCK_DGRAM, port);
	mapping.localModule = localMod;
	mapping.dgid = dgid;
	mapping.connected = false;
	mapping.pollTimer.start();
	m_Mappings.push_back(mapping);

	std::cout << "YSFClient: added mapping " << host << ":" << port
	          << " -> local " << localMod;
	if (dgid > 0)
		std::cout << " (DG-ID " << (int)dgid << ")";
	std::cout << std::endl;
	return true;
}

bool CYsfClientProtocol::RemoveMapping(char localMod)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);

	for (auto it = m_Mappings.begin(); it != m_Mappings.end(); ++it)
	{
		if (it->localModule == localMod)
		{
			if (it->connected)
			{
				SendDisconnect(*it);

				CClients *clients = g_Reflector.GetClients();
				auto client = clients->FindClient(it->ip, EProtocol::ysfclient);
				if (client && client->GetReflectorModule() == localMod)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();
			}

			std::cout << "YSFClient: removed mapping for local module " << localMod << std::endl;
			m_Mappings.erase(it);
			return true;
		}
	}
	return false;
}

std::vector<SYsfClientMapping> CYsfClientProtocol::GetMappings(void) const
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);
	return m_Mappings;
}

////////////////////////////////////////////////////////////////////////////////////////
// mapping helpers

SYsfClientMapping *CYsfClientProtocol::FindMappingByLocal(char localMod)
{
	for (auto &m : m_Mappings)
		if (m.localModule == localMod)
			return &m;
	return nullptr;
}

SYsfClientMapping *CYsfClientProtocol::FindMappingByIp(const CIp &ip)
{
	for (auto &m : m_Mappings)
		if (m.ip == ip)
			return &m;
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////
// connection helpers

void CYsfClientProtocol::SendPoll(SYsfClientMapping &mapping)
{
	CBuffer buffer;
	EncodePollPacket(&buffer);
	Send(buffer, mapping.ip);
}

void CYsfClientProtocol::SendDisconnect(SYsfClientMapping &mapping)
{
	CBuffer buffer;
	EncodeDisconnectPacket(&buffer);
	// send multiple times for reliability (g4klx sends 2)
	Send(buffer, mapping.ip);
	Send(buffer, mapping.ip);
	std::cout << "YSFClient: disconnecting from " << mapping.host << std::endl;
}

////////////////////////////////////////////////////////////////////////////////////////
// stream ID helper

uint32_t CYsfClientProtocol::IpToStreamId(const CIp &ip) const
{
	return ip.GetAddr() ^ (uint32_t)ip.GetPort();
}

////////////////////////////////////////////////////////////////////////////////////////
// packet decoding helpers

bool CYsfClientProtocol::IsValidPollPacket(const CBuffer &Buffer, CCallsign *callsign)
{
	uint8_t tag[] = { 'Y','S','F','P' };

	if ((Buffer.size() == 14) && (Buffer.Compare(tag, sizeof(tag)) == 0))
	{
		callsign->SetCallsign(Buffer.data()+4, 8);
		return callsign->IsValid();
	}
	return false;
}

bool CYsfClientProtocol::IsValidDvPacket(const CBuffer &Buffer, CYSFFICH *Fich)
{
	uint8_t tag[] = { 'Y','S','F','D' };

	if ((Buffer.size() == 155) && (Buffer.Compare(tag, sizeof(tag)) == 0))
	{
		if (Fich->decode(&(Buffer.data()[40])))
		{
			return (Fich->getDT() == YSF_DT_VD_MODE2);
		}
	}
	return false;
}

bool CYsfClientProtocol::IsValidDvHeaderPacket(const CIp &Ip, const CYSFFICH &Fich, const CBuffer &Buffer,
	std::unique_ptr<CDvHeaderPacket> &header, std::array<std::unique_ptr<CDvFramePacket>, 5> &frames)
{
	if (Fich.getFI() == YSF_FI_HEADER)
	{
		uint32_t uiStreamId = IpToStreamId(Ip);

		CYSFPayload ysfPayload;
		CCallsign csMY;
		if (ysfPayload.processHeaderData((unsigned char *)&(Buffer.data()[35])))
		{
			char sz[YSF_CALLSIGN_LENGTH+1];
			memcpy(sz, &(Buffer.data()[14]), YSF_CALLSIGN_LENGTH);
			sz[YSF_CALLSIGN_LENGTH] = 0;
			for (uint32_t i = 0; i < YSF_CALLSIGN_LENGTH; ++i)
				if (sz[i] == '/' || sz[i] == '\\' || sz[i] == '-' || sz[i] == ' ')
					sz[i] = 0;

			csMY = CCallsign((const char *)sz);
			CCallsign rpt1(m_ClientCallsign);
			CCallsign rpt2(m_ClientCallsign);
			rpt2.SetCSModule(' ');

			header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket(csMY, CCallsign("CQCQCQ"), rpt1, rpt2, uiStreamId, Fich.getFN()));
		}

		uint8_t uiAmbe[9];
		memset(uiAmbe, 0x00, sizeof(uiAmbe));
		CCallsign csDummy;
		frames[0] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(uiAmbe, uiStreamId, Fich.getFN(), 0, 0, csDummy, false));
		frames[1] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(uiAmbe, uiStreamId, Fich.getFN(), 1, 0, csDummy, false));

		if (header && frames[0] && frames[1] && frames[0]->IsValid() && frames[1]->IsValid())
			return true;
	}
	return false;
}

bool CYsfClientProtocol::IsValidDvFramePacket(const CIp &Ip, const CYSFFICH &Fich, const CBuffer &Buffer,
	std::unique_ptr<CDvHeaderPacket> &header, std::array<std::unique_ptr<CDvFramePacket>, 5> &frames)
{
	if (Fich.getFI() == YSF_FI_COMMUNICATIONS)
	{
		uint32_t uiStreamId = IpToStreamId(Ip);

		// late entry: create header if no stream exists
		auto stream = GetStream(uiStreamId, &Ip);
		if (!stream)
		{
			char sz[YSF_CALLSIGN_LENGTH+1];
			memcpy(sz, &(Buffer.data()[14]), YSF_CALLSIGN_LENGTH);
			sz[YSF_CALLSIGN_LENGTH] = 0;
			for (uint32_t i = 0; i < YSF_CALLSIGN_LENGTH; ++i)
				if (sz[i] == '/' || sz[i] == '\\' || sz[i] == '-' || sz[i] == ' ')
					sz[i] = 0;

			CCallsign csMY((const char *)sz);
			CCallsign rpt1(m_ClientCallsign);
			CCallsign rpt2(m_ClientCallsign);
			rpt2.SetCSModule(' ');
			header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket(csMY, CCallsign("CQCQCQ"), rpt1, rpt2, uiStreamId, Fich.getFN()));
		}

		// decode 5 AMBE frames
		uint8_t ambe0[9], ambe1[9], ambe2[9], ambe3[9], ambe4[9];
		uint8_t *ambes[5] = { ambe0, ambe1, ambe2, ambe3, ambe4 };
		CYsfUtils::DecodeVD2Vchs((unsigned char *)&(Buffer.data()[35]), ambes);

		char sz[YSF_CALLSIGN_LENGTH+1];
		memcpy(sz, &(Buffer.data()[14]), YSF_CALLSIGN_LENGTH);
		sz[YSF_CALLSIGN_LENGTH] = 0;
		for (uint32_t i = 0; i < YSF_CALLSIGN_LENGTH; ++i)
			if (sz[i] == '/' || sz[i] == '\\' || sz[i] == '-' || sz[i] == ' ')
				sz[i] = 0;
		CCallsign csMY((const char *)sz);

		uint8_t fid = Buffer.data()[34];
		frames[0] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(ambe0, uiStreamId, Fich.getFN(), 0, fid, csMY, false));
		frames[1] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(ambe1, uiStreamId, Fich.getFN(), 1, fid, csMY, false));
		frames[2] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(ambe2, uiStreamId, Fich.getFN(), 2, fid, csMY, false));
		frames[3] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(ambe3, uiStreamId, Fich.getFN(), 3, fid, csMY, false));
		frames[4] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(ambe4, uiStreamId, Fich.getFN(), 4, fid, csMY, false));

		if (frames[0] && frames[0]->IsValid() && frames[1] && frames[1]->IsValid() &&
		    frames[2] && frames[2]->IsValid() && frames[3] && frames[3]->IsValid() &&
		    frames[4] && frames[4]->IsValid())
			return true;
	}
	return false;
}

bool CYsfClientProtocol::IsValidDvLastFramePacket(const CIp &Ip, const CYSFFICH &Fich, const CBuffer &Buffer,
	std::unique_ptr<CDvFramePacket> &oneframe, std::unique_ptr<CDvFramePacket> &lastframe)
{
	if (Fich.getFI() == YSF_FI_TERMINATOR)
	{
		uint32_t uiStreamId = IpToStreamId(Ip);

		uint8_t uiAmbe[9];
		memset(uiAmbe, 0x00, sizeof(uiAmbe));

		char sz[YSF_CALLSIGN_LENGTH+1];
		memcpy(sz, &(Buffer.data()[14]), YSF_CALLSIGN_LENGTH);
		sz[YSF_CALLSIGN_LENGTH] = 0;
		for (uint32_t i = 0; i < YSF_CALLSIGN_LENGTH; ++i)
			if (sz[i] == '/' || sz[i] == '\\' || sz[i] == '-' || sz[i] == ' ')
				sz[i] = 0;
		CCallsign csMY((const char *)sz);

		oneframe  = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(uiAmbe, uiStreamId, Fich.getFN(), 0, 0, csMY, false));
		lastframe = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(uiAmbe, uiStreamId, Fich.getFN(), 1, 0, csMY, true));

		if (oneframe && oneframe->IsValid() && lastframe && lastframe->IsValid())
			return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////
// packet encoding helpers

void CYsfClientProtocol::EncodePollPacket(CBuffer *Buffer)
{
	// YSFP + 10-char callsign = 14 bytes
	uint8_t tag[] = { 'Y','S','F','P' };
	Buffer->Set(tag, sizeof(tag));
	Buffer->Append((uint8_t *)m_szCallsign, YSF_CALLSIGN_LENGTH);
}

void CYsfClientProtocol::EncodeDisconnectPacket(CBuffer *Buffer)
{
	// YSFU + 10-char callsign = 14 bytes
	uint8_t tag[] = { 'Y','S','F','U' };
	Buffer->Set(tag, sizeof(tag));
	Buffer->Append((uint8_t *)m_szCallsign, YSF_CALLSIGN_LENGTH);
}

bool CYsfClientProtocol::EncodeYSFHeaderPacket(const CDvHeaderPacket &Header, uint8_t dgid, CBuffer *Buffer) const
{
	uint8_t tag[]  = { 'Y','S','F','D' };
	uint8_t dest[] = { 'A','L','L',' ',' ',' ',' ',' ',' ',' ' };
	char  sz[YSF_CALLSIGN_LENGTH];
	uint8_t fichd[YSF_FICH_LENGTH_BYTES];

	Buffer->Set(tag, sizeof(tag));
	// rpt1 (gateway callsign)
	Buffer->Append((uint8_t *)m_szCallsign, YSF_CALLSIGN_LENGTH);
	// my (source callsign)
	memset(sz, ' ', sizeof(sz));
	Header.GetMyCallsign().GetCallsignString(sz);
	sz[::strlen(sz)] = ' ';
	Buffer->Append((uint8_t *)sz, YSF_CALLSIGN_LENGTH);
	// dest
	Buffer->Append(dest, 10);
	// net frame counter
	Buffer->Append((uint8_t)0x00);
	// sync
	Buffer->Append((uint8_t *)YSF_SYNC_BYTES, YSF_SYNC_LENGTH_BYTES);
	// FICH
	CYSFFICH fich;
	fich.setFI(YSF_FI_HEADER);
	fich.setCS(2U);
	fich.setFN(0U);
	fich.setFT(7U);
	fich.setDev(0U);
	fich.setMR(YSF_MR_BUSY);
	fich.setDT(YSF_DT_VD_MODE2);
	fich.setSQL(dgid > 0 ? 1U : 0U);
	fich.setSQ(dgid);
	fich.encode(fichd);
	Buffer->Append(fichd, YSF_FICH_LENGTH_BYTES);
	// payload
	unsigned char csd1[20U], csd2[20U];
	memset(csd1, '*', YSF_CALLSIGN_LENGTH);
	memset(csd1 + YSF_CALLSIGN_LENGTH, ' ', YSF_CALLSIGN_LENGTH);
	Header.GetMyCallsign().GetCallsignString(sz);
	memcpy(csd1 + YSF_CALLSIGN_LENGTH, sz, ::strlen(sz));
	memset(csd2, ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);
	CYSFPayload payload;
	uint8_t temp[120];
	payload.writeHeader(temp, csd1, csd2);
	Buffer->Append(temp+30, 120-30);

	return true;
}

bool CYsfClientProtocol::EncodeYSFPacket(const CDvHeaderPacket &Header, const CDvFramePacket *DvFrames, uint8_t dgid, CBuffer *Buffer) const
{
	uint8_t tag[]  = { 'Y','S','F','D' };
	uint8_t dest[] = { 'A','L','L',' ',' ',' ',' ',' ',' ',' ' };
	uint8_t gps[]  = { 0x52,0x22,0x61,0x5F,0x27,0x03,0x5E,0x20,0x20,0x20 };
	char  sz[YSF_CALLSIGN_LENGTH];
	uint8_t fichd[YSF_FICH_LENGTH_BYTES];

	Buffer->Set(tag, sizeof(tag));
	// rpt1 (gateway callsign)
	Buffer->Append((uint8_t *)m_szCallsign, YSF_CALLSIGN_LENGTH);
	// my
	memset(sz, ' ', sizeof(sz));
	Header.GetMyCallsign().GetCallsignString(sz);
	sz[::strlen(sz)] = ' ';
	Buffer->Append((uint8_t *)sz, YSF_CALLSIGN_LENGTH);
	// dest
	Buffer->Append(dest, 10);
	// net frame counter
	Buffer->Append(DvFrames[0].GetYsfPacketFrameId());
	// sync
	Buffer->Append((uint8_t *)YSF_SYNC_BYTES, YSF_SYNC_LENGTH_BYTES);
	// FICH
	CYSFFICH fich;
	fich.setFI(YSF_FI_COMMUNICATIONS);
	fich.setCS(2U);
	fich.setFN(DvFrames[0].GetYsfPacketId());
	fich.setFT(6U);
	fich.setDev(0U);
	fich.setMR(YSF_MR_BUSY);
	fich.setDT(YSF_DT_VD_MODE2);
	fich.setSQL(dgid > 0 ? 1U : 0U);
	fich.setSQ(dgid);
	fich.encode(fichd);
	Buffer->Append(fichd, YSF_FICH_LENGTH_BYTES);
	// payload
	CYSFPayload payload;
	uint8_t temp[120];
	memset(temp, 0x00, sizeof(temp));
	for (int i = 0; i < 5; i++)
		CYsfUtils::EncodeVD2Vch((unsigned char *)DvFrames[i].GetCodecData(ECodecType::dmr), temp+35+(18*i));

	switch (DvFrames[0].GetYsfPacketId())
	{
	case 0:
		payload.writeVDMode2Data(temp, (const unsigned char*)"**********");
		break;
	case 1:
		memset(sz, ' ', sizeof(sz));
		Header.GetMyCallsign().GetCallsignString(sz);
		sz[::strlen(sz)] = ' ';
		payload.writeVDMode2Data(temp, (const unsigned char*)sz);
		break;
	case 2:
		payload.writeVDMode2Data(temp, (const unsigned char*)m_szCallsign);
		break;
	case 5:
		payload.writeVDMode2Data(temp, (const unsigned char*)"     G0gBJ");
		break;
	case 6:
		payload.writeVDMode2Data(temp, gps);
		break;
	default:
		payload.writeVDMode2Data(temp, (const unsigned char*)"          ");
		break;
	}
	Buffer->Append(temp+30, 120-30);

	return true;
}

bool CYsfClientProtocol::EncodeLastYSFPacket(const CDvHeaderPacket &Header, uint8_t dgid, CBuffer *Buffer) const
{
	uint8_t tag[]  = { 'Y','S','F','D' };
	uint8_t dest[] = { 'A','L','L',' ',' ',' ',' ',' ',' ',' ' };
	char  sz[YSF_CALLSIGN_LENGTH];
	uint8_t fichd[YSF_FICH_LENGTH_BYTES];

	Buffer->Set(tag, sizeof(tag));
	Buffer->Append((uint8_t *)m_szCallsign, YSF_CALLSIGN_LENGTH);
	memset(sz, ' ', sizeof(sz));
	Header.GetMyCallsign().GetCallsignString(sz);
	sz[::strlen(sz)] = ' ';
	Buffer->Append((uint8_t *)sz, YSF_CALLSIGN_LENGTH);
	Buffer->Append(dest, 10);
	Buffer->Append((uint8_t)0x00);
	Buffer->Append((uint8_t *)YSF_SYNC_BYTES, YSF_SYNC_LENGTH_BYTES);
	CYSFFICH fich;
	fich.setFI(YSF_FI_TERMINATOR);
	fich.setCS(2U);
	fich.setFN(0U);
	fich.setFT(7U);
	fich.setDev(0U);
	fich.setMR(YSF_MR_BUSY);
	fich.setDT(YSF_DT_VD_MODE2);
	fich.setSQL(dgid > 0 ? 1U : 0U);
	fich.setSQ(dgid);
	fich.encode(fichd);
	Buffer->Append(fichd, YSF_FICH_LENGTH_BYTES);
	unsigned char csd1[20U], csd2[20U];
	memset(csd1, '*', YSF_CALLSIGN_LENGTH);
	memset(csd1 + YSF_CALLSIGN_LENGTH, ' ', YSF_CALLSIGN_LENGTH);
	Header.GetMyCallsign().GetCallsignString(sz);
	memcpy(csd1 + YSF_CALLSIGN_LENGTH, sz, ::strlen(sz));
	memset(csd2, ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);
	CYSFPayload payload;
	uint8_t temp[120];
	payload.writeHeader(temp, csd1, csd2);
	Buffer->Append(temp+30, 120-30);

	return true;
}
