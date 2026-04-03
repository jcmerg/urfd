// DPlusClientProtocol -- Connects to remote DPlus reflectors as a client
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include <string.h>
#include <sstream>

#include "Global.h"
#include "DPlusClientPeer.h"
#include "DPlusClientProtocol.h"

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CDPlusClientProtocol::Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	if (g_Configure.Contains(g_Keys.dplusclient.callsign))
		m_ClientCallsign.SetCallsign(g_Configure.GetString(g_Keys.dplusclient.callsign), false);
	else
		m_ClientCallsign = m_ReflectorCallsign;

	// Parse BlockProtocols
	if (g_Configure.Contains(g_Keys.dplusclient.blockprotocols))
	{
		const std::map<std::string, EProtocol> protoMap = {
			{"SvxReflector", EProtocol::svxreflector}, {"DExtra", EProtocol::dextra},
			{"DExtraClient", EProtocol::dextraclient}, {"DPlus", EProtocol::dplus},
			{"DCS", EProtocol::dcs}, {"DCSClient", EProtocol::dcsclient},
			{"DMRPlus", EProtocol::dmrplus}, {"MMDVM", EProtocol::dmrmmdvm},
			{"MMDVMClient", EProtocol::mmdvmclient}, {"YSF", EProtocol::ysf},
			{"YSFClient", EProtocol::ysfclient}, {"M17", EProtocol::m17},
			{"NXDN", EProtocol::nxdn}, {"P25", EProtocol::p25},
			{"USRP", EProtocol::usrp}, {"URF", EProtocol::urf},
			{"XLXPeer", EProtocol::xlxpeer}, {"G3", EProtocol::g3},
		};
		std::istringstream ss(g_Configure.GetString(g_Keys.dplusclient.blockprotocols));
		std::string token;
		while (std::getline(ss, token, ','))
		{
			token.erase(0, token.find_first_not_of(" \t"));
			token.erase(token.find_last_not_of(" \t") + 1);
			auto it = protoMap.find(token);
			if (it != protoMap.end())
			{
				m_BlockedSources.insert(it->second);
				std::cout << "DPlusClient: blocking protocol " << token << std::endl;
			}
			else if (!token.empty())
				std::cerr << "DPlusClient: unknown protocol in BlockProtocols: " << token << std::endl;
		}
	}

	m_BlockedSources.insert(EProtocol::dplusclient);
	SaveBlockDefaults();

	// Parse static mappings: Map<N>=host,port,remotemod,localmod
	auto &cfgData = g_Configure.GetData();
	for (auto it = cfgData.begin(); it != cfgData.end(); ++it)
	{
		if (it.key().substr(0, 14) == "dplusClientMap")
		{
			std::istringstream ss(it.value().get<std::string>());
			std::string host, portStr, remoteModStr, localModStr;
			if (std::getline(ss, host, ',') && std::getline(ss, portStr, ',') &&
				std::getline(ss, remoteModStr, ',') && std::getline(ss, localModStr, ','))
			{
				host.erase(0, host.find_first_not_of(" \t"));
				host.erase(host.find_last_not_of(" \t") + 1);
				portStr.erase(0, portStr.find_first_not_of(" \t"));
				remoteModStr.erase(0, remoteModStr.find_first_not_of(" \t"));
				localModStr.erase(0, localModStr.find_first_not_of(" \t"));

				uint16_t p = (uint16_t)std::stoul(portStr);
				char remoteMod = remoteModStr[0];
				char localMod = localModStr[0];

				if (IsLetter(remoteMod) && IsLetter(localMod) && g_Reflector.IsValidModule(localMod))
				{
					SDPlusClientMapping mapping;
					mapping.host = host;
					mapping.port = p;
					mapping.ip = CIp(host.c_str(), AF_INET, SOCK_DGRAM, p);
					mapping.remoteModule = remoteMod;
					mapping.localModule = localMod;
					m_Mappings.push_back(mapping);
					std::cout << "DPlusClient: mapping " << host << ":" << p
					          << " module " << remoteMod << " -> local " << localMod << std::endl;
				}
				else
					std::cerr << "DPlusClient: invalid mapping: " << it.value() << std::endl;
			}
		}
	}

	if (!CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	std::cout << "DPlusClient: initialized with callsign " << m_ClientCallsign
	          << ", " << m_Mappings.size() << " mapping(s)" << std::endl;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// task

void CDPlusClientProtocol::Task(void)
{
	CBuffer Buffer;
	CIp     Ip;

	if (Receive4(Buffer, Ip, 20))
		HandleIncoming(Buffer, Ip);

	if (m_ReconnectRequested.exchange(false))
	{
		std::cout << "DPlusClient: reconnect requested" << std::endl;
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		for (auto &m : m_Mappings)
		{
			if (m.connected || m.state != EDPlusState::DISCONNECTED)
			{
				CBuffer buf;
				EncodeDisconnectPacket(&buf);
				Send(buf, m.ip);
				if (m.connected)
				{
					CClients *clients = g_Reflector.GetClients();
					auto client = clients->FindClient(m.ip, EProtocol::dplusclient);
					if (client)
						clients->RemoveClient(client);
					g_Reflector.ReleaseClients();
				}
				m.connected = false;
				m.state = EDPlusState::DISCONNECTED;
			}
			m.stateTimer.start();
		}
	}

	HandleConnections();
	CheckStreamsTimeout();
	HandleQueue();
}

////////////////////////////////////////////////////////////////////////////////////////
// connection management (state machine)

void CDPlusClientProtocol::HandleConnections(void)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);

	for (auto &m : m_Mappings)
	{
		switch (m.state)
		{
		case EDPlusState::DISCONNECTED:
			if (m.stateTimer.time() > DPLUSCLI_RECONNECT_PERIOD)
			{
				m.ip = CIp(m.host.c_str(), AF_INET, SOCK_DGRAM, m.port);
				CBuffer buf;
				EncodeConnectPacket(&buf);
				Send(buf, m.ip);
				m.state = EDPlusState::CONNECTING;
				m.stateTimer.start();
				std::cout << "DPlusClient: connecting to " << m.host << ":" << m.port
				          << " (local " << m.localModule << ")" << std::endl;
			}
			break;

		case EDPlusState::CONNECTING:
			// waiting for connect echo — timeout and retry
			if (m.stateTimer.time() > DPLUSCLI_HANDSHAKE_TIMEOUT)
			{
				std::cout << "DPlusClient: connect timeout for " << m.host << std::endl;
				m.state = EDPlusState::DISCONNECTED;
				m.stateTimer.start();
			}
			break;

		case EDPlusState::LOGGING_IN:
			// waiting for OKRW/BUSY — timeout and retry
			if (m.stateTimer.time() > DPLUSCLI_HANDSHAKE_TIMEOUT)
			{
				std::cout << "DPlusClient: login timeout for " << m.host << std::endl;
				m.state = EDPlusState::DISCONNECTED;
				m.stateTimer.start();
			}
			break;

		case EDPlusState::CONNECTED:
			// send keepalive
			if (m.keepaliveTimer.time() > DPLUSCLI_KEEPALIVE_PERIOD)
			{
				CBuffer buf;
				EncodeKeepAlivePacket(&buf);
				Send(buf, m.ip);
				m.keepaliveTimer.start();
			}
			// check timeout
			if (m.timeoutTimer.time() > DPLUSCLI_KEEPALIVE_TIMEOUT)
			{
				std::cout << "DPlusClient: keepalive timeout for " << m.host << std::endl;
				m.connected = false;
				m.state = EDPlusState::DISCONNECTED;
				m.stateTimer.start();

				CClients *clients = g_Reflector.GetClients();
				auto client = clients->FindClient(m.ip, EProtocol::dplusclient);
				if (client)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();
			}
			break;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// incoming packet handler

void CDPlusClientProtocol::HandleIncoming(const CBuffer &Buffer, const CIp &Ip)
{
	std::unique_ptr<CDvHeaderPacket> Header;
	std::unique_ptr<CDvFramePacket>  Frame;

	if (IsValidDvHeaderPacket(Buffer, Header))
	{
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDPlusClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping && mapping->connected)
		{
			CCallsign rpt1(m_ClientCallsign);
			rpt1.SetCSModule(mapping->localModule);
			Header->SetRpt1Callsign(rpt1);
			Header->SetRpt2Module(mapping->localModule);

			if (g_GateKeeper.MayTransmit(Header->GetMyCallsign(), Ip, EProtocol::dplusclient, mapping->localModule))
				OnDvHeaderPacketIn(Header, Ip);
		}
	}
	else if (IsValidDvFramePacket(Buffer, Frame))
	{
		OnDvFramePacketIn(Frame, &Ip);
	}
	else if (IsValidConnectEcho(Buffer))
	{
		// connect echo received — send login
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDPlusClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping && mapping->state == EDPlusState::CONNECTING)
		{
			CBuffer buf;
			EncodeLoginPacket(&buf);
			Send(buf, Ip);
			mapping->state = EDPlusState::LOGGING_IN;
			mapping->stateTimer.start();
			std::cout << "DPlusClient: connect echo received, sending login to " << mapping->host << std::endl;
		}
	}
	else if (IsValidLoginAck(Buffer))
	{
		// OKRW — login accepted
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDPlusClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping && mapping->state == EDPlusState::LOGGING_IN)
		{
			mapping->state = EDPlusState::CONNECTED;
			mapping->connected = true;
			mapping->keepaliveTimer.start();
			mapping->timeoutTimer.start();
			std::cout << "DPlusClient: logged in to " << mapping->host << ":" << mapping->port
			          << " (local " << mapping->localModule << ")" << std::endl;

			CCallsign cs(m_ClientCallsign);
			cs.SetCSModule(mapping->remoteModule);
			g_Reflector.GetClients()->AddClient(
				std::make_shared<CDPlusClientPeer>(cs, Ip, mapping->localModule));
			g_Reflector.ReleaseClients();
		}
	}
	else if (IsValidLoginNack(Buffer))
	{
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDPlusClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping)
		{
			std::string reason(reinterpret_cast<const char *>(Buffer.data()+4), 4);
			std::cout << "DPlusClient: login rejected (" << reason << ") by " << mapping->host << std::endl;
			mapping->state = EDPlusState::DISCONNECTED;
			mapping->stateTimer.start();
		}
	}
	else if (IsValidKeepAlivePacket(Buffer))
	{
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDPlusClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping && mapping->connected)
		{
			mapping->timeoutTimer.start();

			CClients *clients = g_Reflector.GetClients();
			auto it = clients->begin();
			std::shared_ptr<CClient> client = nullptr;
			while ((client = clients->FindNextClient(EProtocol::dplusclient, it)) != nullptr)
			{
				if (client->GetIp() == Ip)
					client->Alive();
			}
			g_Reflector.ReleaseClients();
		}
	}
	else if (IsValidDisconnectPacket(Buffer))
	{
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDPlusClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping)
		{
			std::cout << "DPlusClient: disconnected by " << mapping->host << std::endl;
			mapping->connected = false;
			mapping->state = EDPlusState::DISCONNECTED;
			mapping->stateTimer.start();

			CClients *clients = g_Reflector.GetClients();
			auto client = clients->FindClient(Ip, EProtocol::dplusclient);
			if (client)
				clients->RemoveClient(client);
			g_Reflector.ReleaseClients();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// stream helpers

void CDPlusClientProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
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
	while ((client = clients->FindNextClient(EProtocol::dplusclient, it)) != nullptr)
	{
		if (client->GetIp() == Ip && client->GetReflectorModule() == module)
			break;
	}
	if (!client)
		client = clients->FindClient(Ip, EProtocol::dplusclient);

	if (client)
	{
		rpt1 = client->GetCallsign();
		client->Alive();
		if ((stream = g_Reflector.OpenStream(Header, client)) != nullptr)
			m_Streams[stream->GetStreamId()] = stream;
	}

	g_Reflector.ReleaseClients();
	g_Reflector.GetUsers()->Hearing(my, rpt1, rpt2, "DPlusClient");
	g_Reflector.ReleaseUsers();
}

////////////////////////////////////////////////////////////////////////////////////////
// outbound queue

void CDPlusClientProtocol::HandleQueue(void)
{
	while (!m_Queue.IsEmpty())
	{
		auto packet = m_Queue.Pop();
		const auto module = packet->GetPacketModule();

		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDPlusClientMapping *mapping = FindMappingByLocal(module);
		if (!mapping || !mapping->connected)
			continue;

		CBuffer buffer;
		bool encoded = false;

		if (packet->IsDvHeader())
		{
			auto &header = (CDvHeaderPacket &)*packet;
			CCallsign rpt1(m_ClientCallsign);
			rpt1.SetCSModule(mapping->remoteModule);
			header.SetRpt1Callsign(rpt1);
			CCallsign rpt2(m_ClientCallsign);
			rpt2.SetCSModule('G');
			header.SetRpt2Callsign(rpt2);

			encoded = EncodeDvHeaderPacket(header, buffer);
		}
		else if (packet->IsDvFrame())
		{
			encoded = EncodeDvFramePacket((const CDvFramePacket &)*packet, buffer);
		}

		if (encoded && buffer.size() > 0)
		{
			// DPlus sends headers multiple times
			int n = packet->IsDvHeader() ? 5 : 1;
			for (int i = 0; i < n; i++)
				Send(buffer, mapping->ip);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// admin API

bool CDPlusClientProtocol::AddMapping(const std::string &host, uint16_t port, char remoteMod, char localMod)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);
	for (auto &m : m_Mappings)
		if (m.localModule == localMod)
			return false;
	if (!g_Reflector.IsValidModule(localMod))
		return false;

	SDPlusClientMapping mapping;
	mapping.host = host;
	mapping.port = port;
	mapping.ip = CIp(host.c_str(), AF_INET, SOCK_DGRAM, port);
	mapping.remoteModule = remoteMod;
	mapping.localModule = localMod;
	mapping.stateTimer.start();
	m_Mappings.push_back(mapping);

	std::cout << "DPlusClient: added mapping " << host << ":" << port
	          << " module " << remoteMod << " -> local " << localMod << std::endl;
	return true;
}

bool CDPlusClientProtocol::RemoveMapping(char localMod)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);
	for (auto it = m_Mappings.begin(); it != m_Mappings.end(); ++it)
	{
		if (it->localModule == localMod)
		{
			if (it->connected)
			{
				CBuffer buf;
				EncodeDisconnectPacket(&buf);
				Send(buf, it->ip);
				CClients *clients = g_Reflector.GetClients();
				auto client = clients->FindClient(it->ip, EProtocol::dplusclient);
				if (client && client->GetReflectorModule() == localMod)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();
			}
			std::cout << "DPlusClient: removed mapping for local module " << localMod << std::endl;
			m_Mappings.erase(it);
			return true;
		}
	}
	return false;
}

std::vector<SDPlusClientMapping> CDPlusClientProtocol::GetMappings(void) const
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);
	return m_Mappings;
}

////////////////////////////////////////////////////////////////////////////////////////
// mapping helpers

SDPlusClientMapping *CDPlusClientProtocol::FindMappingByLocal(char localMod)
{
	for (auto &m : m_Mappings)
		if (m.localModule == localMod) return &m;
	return nullptr;
}

SDPlusClientMapping *CDPlusClientProtocol::FindMappingByIp(const CIp &ip)
{
	for (auto &m : m_Mappings)
		if (m.ip == ip) return &m;
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////
// packet decoding

bool CDPlusClientProtocol::IsValidConnectEcho(const CBuffer &Buffer)
{
	uint8_t tag[] = { 0x05,0x00,0x18,0x00,0x01 };
	return (Buffer == CBuffer(tag, sizeof(tag)));
}

bool CDPlusClientProtocol::IsValidLoginAck(const CBuffer &Buffer)
{
	uint8_t tag[] = { 0x08,0xC0,0x04,0x00,'O','K','R','W' };
	return (Buffer.size() == 8 && 0 == memcmp(Buffer.data(), tag, sizeof(tag)));
}

bool CDPlusClientProtocol::IsValidLoginNack(const CBuffer &Buffer)
{
	uint8_t tagBusy[] = { 0x08,0xC0,0x04,0x00,'B','U','S','Y' };
	uint8_t tagFail[] = { 0x08,0xC0,0x04,0x00,'F','A','I','L' };
	if (Buffer.size() == 8)
	{
		if (0 == memcmp(Buffer.data(), tagBusy, 8)) return true;
		if (0 == memcmp(Buffer.data(), tagFail, 8)) return true;
	}
	return false;
}

bool CDPlusClientProtocol::IsValidDisconnectPacket(const CBuffer &Buffer)
{
	uint8_t tag[] = { 0x05,0x00,0x18,0x00,0x00 };
	return (Buffer == CBuffer(tag, sizeof(tag)));
}

bool CDPlusClientProtocol::IsValidKeepAlivePacket(const CBuffer &Buffer)
{
	uint8_t tag[] = { 0x03,0x60,0x00 };
	return (Buffer == CBuffer(tag, sizeof(tag)));
}

bool CDPlusClientProtocol::IsValidDvHeaderPacket(const CBuffer &Buffer, std::unique_ptr<CDvHeaderPacket> &header)
{
	if (58 == Buffer.size() && 0x3au == Buffer.data()[0] && 0x80u == Buffer.data()[1] &&
		0 == memcmp(Buffer.data()+2, "DSVT", 4) && 0x10u == Buffer.data()[6] && 0x20u == Buffer.data()[10])
	{
		header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket(
			(struct dstar_header *)&(Buffer.data()[17]),
			*((uint16_t *)&(Buffer.data()[14])), 0x80u));
		return (header && header->IsValid());
	}
	return false;
}

bool CDPlusClientProtocol::IsValidDvFramePacket(const CBuffer &Buffer, std::unique_ptr<CDvFramePacket> &dvframe)
{
	if (Buffer.size() >= 29 && 0x80u == Buffer.data()[1] &&
		0 == memcmp(Buffer.data()+2, "DSVT", 4) &&
		0x20u == Buffer.data()[6] && 0x20u == Buffer.data()[10])
	{
		if (29 == Buffer.size() && 0x1du == Buffer.data()[0])
		{
			dvframe = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(
				(SDStarFrame *)&(Buffer.data()[17]),
				*((uint16_t *)&(Buffer.data()[14])), Buffer.data()[16]));
			return true;
		}
		else if (32 == Buffer.size() && 0x20u == Buffer.data()[0])
		{
			dvframe = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(
				(SDStarFrame *)&(Buffer.data()[17]),
				*((uint16_t *)&(Buffer.data()[14])), 0x40u | Buffer.data()[16]));
			return true;
		}
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////
// packet encoding

void CDPlusClientProtocol::EncodeConnectPacket(CBuffer *Buffer)
{
	uint8_t tag[] = { 0x05,0x00,0x18,0x00,0x01 };
	Buffer->Set(tag, sizeof(tag));
}

void CDPlusClientProtocol::EncodeLoginPacket(CBuffer *Buffer)
{
	// 28 bytes: tag(4) + callsign(16, zero-padded) + version(8, "DV019999")
	// ircDDBGateway format: callsign at offset 4, version string at offset 20
	uint8_t tag[] = { 0x1C,0xC0,0x04,0x00 };
	uint8_t cs[CALLSIGN_LEN];
	m_ClientCallsign.GetCallsign(cs);

	Buffer->Set(tag, sizeof(tag));
	Buffer->Append(cs, CALLSIGN_LEN);
	Buffer->Append((uint8_t)0x00, 8);  // zero-pad callsign to 16 bytes
	Buffer->Append((uint8_t *)"DV019999", 8);  // firmware version string
}

void CDPlusClientProtocol::EncodeDisconnectPacket(CBuffer *Buffer)
{
	uint8_t tag[] = { 0x05,0x00,0x18,0x00,0x00 };
	Buffer->Set(tag, sizeof(tag));
}

void CDPlusClientProtocol::EncodeKeepAlivePacket(CBuffer *Buffer)
{
	uint8_t tag[] = { 0x03,0x60,0x00 };
	Buffer->Set(tag, sizeof(tag));
}

bool CDPlusClientProtocol::EncodeDvHeaderPacket(const CDvHeaderPacket &Packet, CBuffer &Buffer) const
{
	uint8_t tag[] = { 0x3A,0x80,0x44,0x53,0x56,0x54,0x10,0x00,0x00,0x00,0x20,0x00,0x01,0x02 };
	struct dstar_header DstarHeader;

	Packet.ConvertToDstarStruct(&DstarHeader);

	Buffer.Set(tag, sizeof(tag));
	Buffer.Append(Packet.GetStreamId());
	Buffer.Append((uint8_t)0x80);
	Buffer.Append((uint8_t *)&DstarHeader, sizeof(struct dstar_header));

	return true;
}

bool CDPlusClientProtocol::EncodeDvFramePacket(const CDvFramePacket &Packet, CBuffer &Buffer) const
{
	if (Packet.IsLastPacket())
	{
		// last frame: 32 bytes (tag byte 0x20)
		uint8_t tag[] = { 0x20,0x80,0x44,0x53,0x56,0x54,0x20,0x00,0x00,0x00,0x20,0x00,0x01,0x02 };
		uint8_t endtag[] = { 0x55,0xC8,0x7A,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x25,0x1A,0xC6 };
		Buffer.Set(tag, sizeof(tag));
		Buffer.Append(Packet.GetStreamId());
		Buffer.Append((uint8_t)((Packet.GetPacketId() % 21) | 0x40));
		Buffer.Append(endtag, sizeof(endtag));
	}
	else
	{
		// normal frame: 29 bytes (tag byte 0x1D)
		uint8_t tag[] = { 0x1D,0x80,0x44,0x53,0x56,0x54,0x20,0x00,0x00,0x00,0x20,0x00,0x01,0x02 };
		Buffer.Set(tag, sizeof(tag));
		Buffer.Append(Packet.GetStreamId());
		Buffer.Append((uint8_t)(Packet.GetPacketId() % 21));
		Buffer.Append((uint8_t *)Packet.GetCodecData(ECodecType::dstar), 9);
		Buffer.Append((uint8_t *)Packet.GetDvData(), 3);
	}

	return true;
}
