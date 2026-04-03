// DCSClientProtocol -- Connects to remote DCS reflectors as a client node
// URFD acts as a DCS hotspot/node connecting to external DCS reflectors.
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include <string.h>
#include <sstream>

#include "Global.h"
#include "DCSClientPeer.h"
#include "DCSClientProtocol.h"

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CDcsClientProtocol::Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	// client callsign (how we identify to remote DCS reflectors)
	if (g_Configure.Contains(g_Keys.dcsclient.callsign))
		m_ClientCallsign.SetCallsign(g_Configure.GetString(g_Keys.dcsclient.callsign), false);
	else
		m_ClientCallsign = m_ReflectorCallsign;

	// Parse BlockProtocols
	if (g_Configure.Contains(g_Keys.dcsclient.blockprotocols))
	{
		const std::map<std::string, EProtocol> protoMap = {
			{"SvxReflector", EProtocol::svxreflector}, {"DExtra", EProtocol::dextra},
			{"DPlus", EProtocol::dplus}, {"DCS", EProtocol::dcs},
			{"DMRPlus", EProtocol::dmrplus}, {"MMDVM", EProtocol::dmrmmdvm},
			{"MMDVMClient", EProtocol::mmdvmclient}, {"YSF", EProtocol::ysf},
			{"M17", EProtocol::m17}, {"NXDN", EProtocol::nxdn},
			{"P25", EProtocol::p25}, {"USRP", EProtocol::usrp},
			{"URF", EProtocol::urf}, {"XLXPeer", EProtocol::xlxpeer},
			{"G3", EProtocol::g3},
		};
		std::istringstream ss(g_Configure.GetString(g_Keys.dcsclient.blockprotocols));
		std::string token;
		while (std::getline(ss, token, ','))
		{
			token.erase(0, token.find_first_not_of(" \t"));
			token.erase(token.find_last_not_of(" \t") + 1);
			auto it = protoMap.find(token);
			if (it != protoMap.end())
			{
				m_BlockedSources.insert(it->second);
				std::cout << "DCSClient: blocking protocol " << token << std::endl;
			}
			else if (!token.empty())
				std::cerr << "DCSClient: unknown protocol in BlockProtocols: " << token << std::endl;
		}
	}

	// Block self-routing
	m_BlockedSources.insert(EProtocol::dcsclient);
	SaveBlockDefaults();

	// Parse static module mappings from config: Map1=host,port,remotemod,localmod
	auto &cfgData = g_Configure.GetData();
	for (auto it = cfgData.begin(); it != cfgData.end(); ++it)
	{
		if (it.key().substr(0, 12) == "dcsClientMap")
		{
			std::istringstream ss(it.value().get<std::string>());
			std::string host, portStr, remoteModStr, localModStr;
			if (std::getline(ss, host, ',') && std::getline(ss, portStr, ',') &&
				std::getline(ss, remoteModStr, ',') && std::getline(ss, localModStr, ','))
			{
				// trim whitespace
				host.erase(0, host.find_first_not_of(" \t"));
				host.erase(host.find_last_not_of(" \t") + 1);
				portStr.erase(0, portStr.find_first_not_of(" \t"));
				localModStr.erase(0, localModStr.find_first_not_of(" \t"));
				remoteModStr.erase(0, remoteModStr.find_first_not_of(" \t"));

				uint16_t p = (uint16_t)std::stoul(portStr);
				char remoteMod = remoteModStr[0];
				char localMod = localModStr[0];

				if (IsLetter(remoteMod) && IsLetter(localMod) && g_Reflector.IsValidModule(localMod))
				{
					SDcsClientMapping mapping;
					mapping.host = host;
					mapping.port = p;
					mapping.ip = CIp(host.c_str(), AF_INET, SOCK_DGRAM, p);
					mapping.remoteModule = remoteMod;
					mapping.localModule = localMod;
					mapping.connected = false;
					m_Mappings.push_back(mapping);
					std::cout << "DCSClient: mapping " << host << ":" << p
					          << " module " << remoteMod << " -> local " << localMod << std::endl;
				}
				else
					std::cerr << "DCSClient: invalid mapping: " << it.value() << std::endl;
			}
		}
	}

	if (!CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	std::cout << "DCSClient: initialized with callsign " << m_ClientCallsign
	          << ", " << m_Mappings.size() << " mapping(s)" << std::endl;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// task

void CDcsClientProtocol::Task(void)
{
	CBuffer Buffer;
	CIp     Ip;

	// receive packets
	if (Receive4(Buffer, Ip, 20))
	{
		HandleIncoming(Buffer, Ip);
	}

	// handle admin-requested reconnect
	if (m_ReconnectRequested.exchange(false))
	{
		std::cout << "DCSClient: reconnect requested" << std::endl;
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		for (auto &m : m_Mappings)
		{
			if (m.connected)
			{
				SendDisconnect(m);
				m.connected = false;
			}
			m.reconnectTimer.start();
		}
	}

	// handle connections (connect/reconnect/keepalive)
	HandleConnections();

	// handle end of streaming timeout
	CheckStreamsTimeout();

	// handle outbound queue
	HandleQueue();
}

////////////////////////////////////////////////////////////////////////////////////////
// connection management

void CDcsClientProtocol::HandleConnections(void)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);

	for (auto &m : m_Mappings)
	{
		if (m.connected)
		{
			// send keepalive
			if (m.keepaliveTimer.time() > DCSCLI_KEEPALIVE_PERIOD)
			{
				SendKeepalive(m);
				m.keepaliveTimer.start();
			}

			// check timeout
			if (m.reconnectTimer.time() > DCSCLI_KEEPALIVE_TIMEOUT)
			{
				std::cout << "DCSClient: keepalive timeout for " << m.host
				          << " module " << m.remoteModule << std::endl;
				m.connected = false;

				// remove the virtual client
				CClients *clients = g_Reflector.GetClients();
				auto client = clients->FindClient(m.ip, EProtocol::dcsclient);
				if (client)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();

				m.reconnectTimer.start();
			}
		}
		else
		{
			// try to connect
			if (m.reconnectTimer.time() > DCSCLI_RECONNECT_PERIOD)
			{
				// re-resolve hostname
				m.ip = CIp(m.host.c_str(), AF_INET, SOCK_DGRAM, m.port);
				SendConnect(m);
				m.reconnectTimer.start();
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// incoming packet handler

void CDcsClientProtocol::HandleIncoming(const CBuffer &Buffer, const CIp &Ip)
{
	CCallsign Callsign;
	char      Module;
	std::unique_ptr<CDvHeaderPacket> Header;
	std::unique_ptr<CDvFramePacket>  Frame;

	if (IsValidDvPacket(Buffer, Header, Frame))
	{
		// find which mapping this belongs to
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDcsClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping && mapping->connected)
		{
			// remap header for local reflector routing:
			// RPT2 module = our local module (determines which reflector module receives it)
			// RPT1 = our client callsign (identifies the source "repeater")
			CCallsign rpt1(m_ClientCallsign);
			rpt1.SetCSModule(mapping->localModule);
			Header->SetRpt1Callsign(rpt1);
			Header->SetRpt2Module(mapping->localModule);

			if (g_GateKeeper.MayTransmit(Header->GetMyCallsign(), Ip, EProtocol::dcsclient, mapping->localModule))
			{
				OnDvHeaderPacketIn(Header, Ip);
				OnDvFramePacketIn(Frame, &Ip);
			}
		}
	}
	else if (IsValidConnectAckPacket(Buffer, &Callsign, &Module))
	{
		std::cout << "DCSClient: ACK from " << Callsign << " module " << Module << std::endl;

		std::lock_guard<std::mutex> lock(m_MappingMutex);
		for (auto &m : m_Mappings)
		{
			if (m.ip == Ip && m.remoteModule == Module)
			{
				m.connected = true;
				m.keepaliveTimer.start();
				m.reconnectTimer.start();

				// create virtual client for this mapping
				CCallsign cs;
				cs = m_ClientCallsign;
				cs.SetCSModule(m.remoteModule);
				g_Reflector.GetClients()->AddClient(
					std::make_shared<CDCSClientPeer>(cs, Ip, m.localModule));
				g_Reflector.ReleaseClients();
				break;
			}
		}
	}
	else if (IsValidConnectNackPacket(Buffer, &Callsign, &Module))
	{
		// NAK is also sent as disconnect confirmation — only log if unexpected
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDcsClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping && mapping->connected)
			std::cout << "DCSClient: NAK from " << Callsign << " module " << Module << std::endl;
	}
	else if (IsValidKeepAlivePacket(Buffer, &Callsign))
	{
		// keepalive from remote reflector — reset timeout
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDcsClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping)
		{
			mapping->reconnectTimer.start();

			// also keep the virtual client alive
			CClients *clients = g_Reflector.GetClients();
			auto it = clients->begin();
			std::shared_ptr<CClient> client = nullptr;
			while ((client = clients->FindNextClient(EProtocol::dcsclient, it)) != nullptr)
			{
				if (client->GetIp() == Ip)
					client->Alive();
			}
			g_Reflector.ReleaseClients();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// stream helpers

void CDcsClientProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
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

	// find the virtual client for this mapping's local module
	auto it = clients->begin();
	std::shared_ptr<CClient> client = nullptr;
	while ((client = clients->FindNextClient(EProtocol::dcsclient, it)) != nullptr)
	{
		if (client->GetIp() == Ip && client->GetReflectorModule() == module)
			break;
	}

	if (!client)
	{
		// try any client from this IP
		client = clients->FindClient(Ip, EProtocol::dcsclient);
	}

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

	g_Reflector.GetUsers()->Hearing(my, rpt1, rpt2, "DCSClient");
	g_Reflector.ReleaseUsers();
}

////////////////////////////////////////////////////////////////////////////////////////
// outbound queue: Reflector -> remote DCS reflector

void CDcsClientProtocol::HandleQueue(void)
{
	while (!m_Queue.IsEmpty())
	{
		auto packet = m_Queue.Pop();
		const auto module = packet->GetPacketModule();

		// find the mapping for this local module
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDcsClientMapping *mapping = FindMappingByLocal(module);
		if (!mapping || !mapping->connected)
			continue;

		if (packet->IsDvHeader())
		{
			// cache the header and rewrite RPT1/RPT2 for the remote DCS reflector
			// D-Star routing convention:
			//   RPT1 = origin repeater (us) + module
			//   RPT2 = gateway callsign with 'G' suffix (signals "please route")
			// MY callsign (the actual caller) is preserved as-is
			m_StreamsCache[module].header = CDvHeaderPacket((const CDvHeaderPacket &)*packet.get());
			m_StreamsCache[module].seqCounter = 0;

			// RPT1 = our callsign + remote module letter
			CCallsign rpt1(m_ClientCallsign);
			rpt1.SetCSModule(mapping->remoteModule);
			m_StreamsCache[module].header.SetRpt1Callsign(rpt1);

			// RPT2 = our callsign + 'G' (gateway routing flag)
			CCallsign rpt2(m_ClientCallsign);
			rpt2.SetCSModule('G');
			m_StreamsCache[module].header.SetRpt2Callsign(rpt2);
		}
		else
		{
			CBuffer buffer;
			if (packet->IsLastPacket())
			{
				EncodeLastDCSPacket(m_StreamsCache[module].header, (const CDvFramePacket &)*packet.get(),
				                    m_StreamsCache[module].seqCounter++, &buffer);
			}
			else if (packet->IsDvFrame())
			{
				EncodeDCSPacket(m_StreamsCache[module].header, (const CDvFramePacket &)*packet.get(),
				                m_StreamsCache[module].seqCounter++, &buffer);
			}

			if (buffer.size() > 0)
			{
				Send(buffer, mapping->ip);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// admin API

bool CDcsClientProtocol::AddMapping(const std::string &host, uint16_t port, char remoteMod, char localMod)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);

	// check for duplicate local module
	for (auto &m : m_Mappings)
	{
		if (m.localModule == localMod)
			return false;
	}

	if (!g_Reflector.IsValidModule(localMod))
		return false;

	SDcsClientMapping mapping;
	mapping.host = host;
	mapping.port = port;
	mapping.ip = CIp(host.c_str(), AF_INET, SOCK_DGRAM, port);
	mapping.remoteModule = remoteMod;
	mapping.localModule = localMod;
	mapping.connected = false;
	mapping.reconnectTimer.start();
	m_Mappings.push_back(mapping);

	std::cout << "DCSClient: added mapping " << host << ":" << port
	          << " module " << remoteMod << " -> local " << localMod << std::endl;
	return true;
}

bool CDcsClientProtocol::RemoveMapping(char localMod)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);

	for (auto it = m_Mappings.begin(); it != m_Mappings.end(); ++it)
	{
		if (it->localModule == localMod)
		{
			if (it->connected)
			{
				SendDisconnect(*it);

				// remove the virtual client
				CClients *clients = g_Reflector.GetClients();
				auto client = clients->FindClient(it->ip, EProtocol::dcsclient);
				if (client && client->GetReflectorModule() == localMod)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();
			}

			std::cout << "DCSClient: removed mapping for local module " << localMod << std::endl;
			m_Mappings.erase(it);
			return true;
		}
	}
	return false;
}

std::vector<SDcsClientMapping> CDcsClientProtocol::GetMappings(void) const
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);
	return m_Mappings;
}

////////////////////////////////////////////////////////////////////////////////////////
// mapping helpers

SDcsClientMapping *CDcsClientProtocol::FindMappingByLocal(char localMod)
{
	for (auto &m : m_Mappings)
		if (m.localModule == localMod)
			return &m;
	return nullptr;
}

SDcsClientMapping *CDcsClientProtocol::FindMappingByRemote(const CIp &ip, char remoteMod)
{
	for (auto &m : m_Mappings)
		if (m.ip == ip && m.remoteModule == remoteMod)
			return &m;
	return nullptr;
}

SDcsClientMapping *CDcsClientProtocol::FindMappingByIp(const CIp &ip)
{
	for (auto &m : m_Mappings)
		if (m.ip == ip)
			return &m;
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////
// connection helpers

void CDcsClientProtocol::SendConnect(SDcsClientMapping &mapping)
{
	CBuffer buffer;
	EncodeConnectPacket(&buffer, mapping.localModule, mapping.remoteModule);
	Send(buffer, mapping.ip);
	std::cout << "DCSClient: connecting to " << mapping.host << ":" << mapping.port
	          << " module " << mapping.remoteModule << " (local " << mapping.localModule << ")" << std::endl;
}

void CDcsClientProtocol::SendDisconnect(SDcsClientMapping &mapping)
{
	CBuffer buffer;
	EncodeDisconnectPacket(&buffer);
	Send(buffer, mapping.ip);
	std::cout << "DCSClient: disconnecting from " << mapping.host
	          << " module " << mapping.remoteModule << std::endl;
}

void CDcsClientProtocol::SendKeepalive(SDcsClientMapping &mapping)
{
	CBuffer buffer;
	EncodeKeepAlivePacket(&buffer);
	Send(buffer, mapping.ip);
}

////////////////////////////////////////////////////////////////////////////////////////
// packet decoding helpers (same wire format as DCS protocol)

bool CDcsClientProtocol::IsValidDvPacket(const CBuffer &Buffer, std::unique_ptr<CDvHeaderPacket> &header, std::unique_ptr<CDvFramePacket> &frame)
{
	uint8_t tag[] = { '0','0','0','1' };

	if ((Buffer.size() >= 100) && (Buffer.Compare(tag, sizeof(tag)) == 0))
	{
		header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket((struct dstar_header *)&(Buffer.data()[4]), *((uint16_t *)&(Buffer.data()[43])), 0x80));
		frame = std::unique_ptr<CDvFramePacket>(new CDvFramePacket((SDStarFrame *)&(Buffer.data()[46]), *((uint16_t *)&(Buffer.data()[43])), Buffer.data()[45]));

		if (header && header->IsValid() && frame && frame->IsValid())
			return true;
	}
	return false;
}

bool CDcsClientProtocol::IsValidConnectAckPacket(const CBuffer &Buffer, CCallsign *callsign, char *reflectormodule)
{
	// 14 bytes: bytes 0-7=callsign, 8=repeater SSID, 9=reflector SSID, 10-12="ACK"
	// Compare(buffer, offset, len): compare 'len' bytes at 'offset'
	if (Buffer.size() == 14)
	{
		if (Buffer.data()[10] == 'A' && Buffer.data()[11] == 'C' && Buffer.data()[12] == 'K')
		{
			callsign->SetCallsign(Buffer.data(), 8);
			*reflectormodule = Buffer.data()[9];
			return callsign->IsValid();
		}
		// urfd format: ACK at offset 11
		if (Buffer.data()[11] == 'A' && Buffer.data()[12] == 'C' && Buffer.data()[13] == 'K')
		{
			callsign->SetCallsign(Buffer.data(), 8);
			*reflectormodule = Buffer.data()[10];
			return callsign->IsValid();
		}
	}
	return false;
}

bool CDcsClientProtocol::IsValidConnectNackPacket(const CBuffer &Buffer, CCallsign *callsign, char *reflectormodule)
{
	if (Buffer.size() == 14)
	{
		if (Buffer.data()[10] == 'N' && Buffer.data()[11] == 'A' && Buffer.data()[12] == 'K')
		{
			callsign->SetCallsign(Buffer.data(), 8);
			*reflectormodule = Buffer.data()[9];
			return callsign->IsValid();
		}
		if (Buffer.data()[11] == 'N' && Buffer.data()[12] == 'A' && Buffer.data()[13] == 'K')
		{
			callsign->SetCallsign(Buffer.data(), 8);
			*reflectormodule = Buffer.data()[10];
			return callsign->IsValid();
		}
	}
	return false;
}

bool CDcsClientProtocol::IsValidKeepAlivePacket(const CBuffer &Buffer, CCallsign *callsign)
{
	// DCS keepalive sizes: 9 (DCS002), 15, 17 (ircDDBGateway), 22 (extended)
	if ((Buffer.size() == 9) || (Buffer.size() == 15) || (Buffer.size() == 17) || (Buffer.size() == 22))
	{
		callsign->SetCallsign(Buffer.data(), std::min((int)Buffer.size(), (int)CALLSIGN_LEN));
		return callsign->IsValid();
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////
// packet encoding helpers

void CDcsClientProtocol::EncodeConnectPacket(CBuffer *Buffer, char localModule, char remoteModule)
{
	// ircDDBGateway format (519 bytes):
	// Bytes 0-7:    Repeater callsign (space-padded)
	// Byte 8:       Repeater SSID (= our local module, identifies our "band")
	// Byte 9:       Reflector SSID (= remote module we want to link to)
	// Byte 10:      0x00
	// Bytes 11-18:  Reflector callsign (space-padded, unused by most reflectors)
	// Bytes 19-518: HTML info (null-padded)
	uint8_t cs[CALLSIGN_LEN];
	m_ClientCallsign.GetCallsign(cs);
	Buffer->Set(cs, CALLSIGN_LEN);              // bytes 0-7: callsign (8 chars, space-padded)
	Buffer->Append((uint8_t)localModule);       // byte 8: repeater SSID (our module)
	Buffer->Append((uint8_t)remoteModule);      // byte 9: reflector SSID (target module)
	Buffer->Append((uint8_t)0x00);              // byte 10: null
	Buffer->Append((uint8_t)0x20, 8);           // bytes 11-18: reflector callsign (spaces)
	// bytes 19-518: HTML info (gateway type + software version, like ircDDBGateway)
	{
		std::ostringstream html;
		html << "<table border=\"0\" width=\"95%%\"><tr>"
		     << "<td width=\"4%%\"><img border=\"0\" src=dongle.jpg></td>"
		     << "<td width=\"96%%\"><font size=\"2\">"
		     << "<b>DONGLE</b> urfd " << g_Version << "<br>"
		     << "Reflector Client"
		     << "</font></td></tr></table>";
		std::string info = html.str();
		uint8_t htmlBuf[500];
		memset(htmlBuf, 0x00, sizeof(htmlBuf));
		memcpy(htmlBuf, info.c_str(), std::min(info.size(), (size_t)500));
		Buffer->Append(htmlBuf, 500);
	}
}

void CDcsClientProtocol::EncodeDisconnectPacket(CBuffer *Buffer)
{
	// ircDDBGateway CT_UNLINK format (19 bytes):
	// Bytes 0-7:    Repeater callsign (space-padded)
	// Byte 8:       Repeater SSID (space = unlink all)
	// Byte 9:       0x20 (space)
	// Byte 10:      0x00
	// Bytes 11-18:  Reflector callsign (space-padded)
	uint8_t cs[CALLSIGN_LEN];
	m_ClientCallsign.GetCallsign(cs);
	Buffer->Set(cs, CALLSIGN_LEN);              // bytes 0-7: callsign (8 chars)
	Buffer->Append((uint8_t)' ');               // byte 8: SSID (space = unlink all)
	Buffer->Append((uint8_t)' ');               // byte 9: space
	Buffer->Append((uint8_t)0x00);              // byte 10: null
	Buffer->Append((uint8_t)0x20, 8);           // bytes 11-18: reflector callsign (spaces)
}

void CDcsClientProtocol::EncodeKeepAlivePacket(CBuffer *Buffer)
{
	// ircDDBGateway DC_POLL format (17 bytes):
	// Bytes 0-7:  Callsign1 (space-padded)
	// Byte 8:     0x00
	// Bytes 9-16: Callsign2 (space-padded)
	uint8_t cs[CALLSIGN_LEN];
	m_ClientCallsign.GetCallsign(cs);
	Buffer->Set(cs, CALLSIGN_LEN);              // bytes 0-7 (8 bytes callsign)
	Buffer->Append((uint8_t)0x00);              // byte 8: null separator
	Buffer->Append(cs, CALLSIGN_LEN);           // bytes 9-16 (repeat callsign)
}

void CDcsClientProtocol::EncodeDCSPacket(const CDvHeaderPacket &Header, const CDvFramePacket &DvFrame, uint32_t iSeq, CBuffer *Buffer) const
{
	uint8_t tag[] = { '0','0','0','1' };
	struct dstar_header DstarHeader;

	Header.ConvertToDstarStruct(&DstarHeader);

	Buffer->Set(tag, sizeof(tag));
	Buffer->Append((uint8_t *)&DstarHeader, sizeof(struct dstar_header) - sizeof(uint16_t));
	Buffer->Append(DvFrame.GetStreamId());
	Buffer->Append((uint8_t)(DvFrame.GetPacketId() % 21));
	Buffer->Append((uint8_t *)DvFrame.GetCodecData(ECodecType::dstar), 9);
	Buffer->Append((uint8_t *)DvFrame.GetDvData(), 3);
	Buffer->Append((uint8_t)((iSeq >> 0) & 0xFF));
	Buffer->Append((uint8_t)((iSeq >> 8) & 0xFF));
	Buffer->Append((uint8_t)((iSeq >> 16) & 0xFF));
	Buffer->Append((uint8_t)0x01);
	Buffer->Append((uint8_t)0x00, 38);
}

void CDcsClientProtocol::EncodeLastDCSPacket(const CDvHeaderPacket &Header, const CDvFramePacket &DvFrame, uint32_t iSeq, CBuffer *Buffer) const
{
	EncodeDCSPacket(Header, DvFrame, iSeq, Buffer);
	(Buffer->data())[45] |= 0x40;
}
