// DExtraClientProtocol -- Connects to remote DExtra reflectors as a client
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include <string.h>
#include <sstream>

#include "Global.h"
#include "DExtraClientPeer.h"
#include "DExtraClientProtocol.h"

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CDExtraClientProtocol::Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	if (g_Configure.Contains(g_Keys.dextraclient.callsign))
		m_ClientCallsign.SetCallsign(g_Configure.GetString(g_Keys.dextraclient.callsign), false);
	else
		m_ClientCallsign = m_ReflectorCallsign;

	// Parse BlockProtocols
	if (g_Configure.Contains(g_Keys.dextraclient.blockprotocols))
	{
		const std::map<std::string, EProtocol> protoMap = {
			{"SvxReflector", EProtocol::svxreflector}, {"DExtra", EProtocol::dextra},
			{"DPlus", EProtocol::dplus}, {"DCS", EProtocol::dcs},
			{"DCSClient", EProtocol::dcsclient}, {"DMRPlus", EProtocol::dmrplus},
			{"MMDVM", EProtocol::dmrmmdvm}, {"MMDVMClient", EProtocol::mmdvmclient},
			{"YSF", EProtocol::ysf}, {"YSFClient", EProtocol::ysfclient},
			{"M17", EProtocol::m17}, {"NXDN", EProtocol::nxdn},
			{"P25", EProtocol::p25}, {"USRP", EProtocol::usrp},
			{"URF", EProtocol::urf}, {"XLXPeer", EProtocol::xlxpeer},
			{"G3", EProtocol::g3},
		};
		std::istringstream ss(g_Configure.GetString(g_Keys.dextraclient.blockprotocols));
		std::string token;
		while (std::getline(ss, token, ','))
		{
			token.erase(0, token.find_first_not_of(" \t"));
			token.erase(token.find_last_not_of(" \t") + 1);
			auto it = protoMap.find(token);
			if (it != protoMap.end())
			{
				m_BlockedSources.insert(it->second);
				std::cout << "DExtraClient: blocking protocol " << token << std::endl;
			}
			else if (!token.empty())
				std::cerr << "DExtraClient: unknown protocol in BlockProtocols: " << token << std::endl;
		}
	}

	m_BlockedSources.insert(EProtocol::dextraclient);
	SaveBlockDefaults();

	// Parse static mappings: Map<N>=host,port,remotemod,localmod
	auto &cfgData = g_Configure.GetData();
	for (auto it = cfgData.begin(); it != cfgData.end(); ++it)
	{
		if (it.key().substr(0, 15) == "dextraClientMap")
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
					SDExtraClientMapping mapping;
					mapping.host = host;
					mapping.port = p;
					mapping.ip = CIp(host.c_str(), AF_INET, SOCK_DGRAM, p);
					mapping.remoteModule = remoteMod;
					mapping.localModule = localMod;
					m_Mappings.push_back(mapping);
					std::cout << "DExtraClient: mapping " << host << ":" << p
					          << " module " << remoteMod << " -> local " << localMod << std::endl;
				}
				else
					std::cerr << "DExtraClient: invalid mapping: " << it.value() << std::endl;
			}
		}
	}

	if (!CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	std::cout << "DExtraClient: initialized with callsign " << m_ClientCallsign
	          << ", " << m_Mappings.size() << " mapping(s)" << std::endl;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// task

void CDExtraClientProtocol::Task(void)
{
	CBuffer Buffer;
	CIp     Ip;

	if (Receive4(Buffer, Ip, 20))
		HandleIncoming(Buffer, Ip);

	if (m_ReconnectRequested.exchange(false))
	{
		std::cout << "DExtraClient: reconnect requested" << std::endl;
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		for (auto &m : m_Mappings)
		{
			if (m.connected)
			{
				SendDisconnect(m);
				m.connected = false;
			}
			m.keepaliveTimer.start();
		}
	}

	HandleConnections();
	CheckStreamsTimeout();
	HandleQueue();
}

////////////////////////////////////////////////////////////////////////////////////////
// connection management

void CDExtraClientProtocol::HandleConnections(void)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);

	for (auto &m : m_Mappings)
	{
		if (m.connected)
		{
			if (m.keepaliveTimer.time() > DEXTRACLI_KEEPALIVE_PERIOD)
			{
				SendKeepalive(m);
				m.keepaliveTimer.start();
			}

			if (m.timeoutTimer.time() > DEXTRACLI_KEEPALIVE_TIMEOUT)
			{
				std::cout << "DExtraClient: keepalive timeout for " << m.host
				          << " module " << m.remoteModule << std::endl;
				m.connected = false;

				CClients *clients = g_Reflector.GetClients();
				auto client = clients->FindClient(m.ip, EProtocol::dextraclient);
				if (client)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();

				m.keepaliveTimer.start();
			}
		}
		else
		{
			if (m.keepaliveTimer.time() > DEXTRACLI_RECONNECT_PERIOD)
			{
				m.ip = CIp(m.host.c_str(), AF_INET, SOCK_DGRAM, m.port);
				SendConnect(m);
				m.keepaliveTimer.start();
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// incoming packet handler

void CDExtraClientProtocol::HandleIncoming(const CBuffer &Buffer, const CIp &Ip)
{
	CCallsign Callsign;
	char      Module;
	std::unique_ptr<CDvHeaderPacket> Header;
	std::unique_ptr<CDvFramePacket>  Frame;

	if (IsValidDvHeaderPacket(Buffer, Header))
	{
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDExtraClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping && mapping->connected)
		{
			// remap header
			CCallsign rpt1(m_ClientCallsign);
			rpt1.SetCSModule(mapping->localModule);
			Header->SetRpt1Callsign(rpt1);
			Header->SetRpt2Module(mapping->localModule);

			if (g_GateKeeper.MayTransmit(Header->GetMyCallsign(), Ip, EProtocol::dextraclient, mapping->localModule))
				OnDvHeaderPacketIn(Header, Ip);
		}
	}
	else if (IsValidDvFramePacket(Buffer, Frame))
	{
		OnDvFramePacketIn(Frame, &Ip);
	}
	else if (IsValidConnectAckPacket(Buffer, &Callsign, &Module))
	{
		std::cout << "DExtraClient: ACK from " << Callsign << " module " << Module << std::endl;

		std::lock_guard<std::mutex> lock(m_MappingMutex);
		for (auto &m : m_Mappings)
		{
			if (m.ip == Ip && m.remoteModule == Module)
			{
				m.connected = true;
				m.keepaliveTimer.start();
				m.timeoutTimer.start();

				CCallsign cs(m_ClientCallsign);
				cs.SetCSModule(m.remoteModule);
				g_Reflector.GetClients()->AddClient(
					std::make_shared<CDExtraClientPeer>(cs, Ip, m.localModule));
				g_Reflector.ReleaseClients();
				break;
			}
		}
	}
	else if (IsValidConnectNackPacket(Buffer, &Callsign))
	{
		// NAK is also disconnect confirmation — only log if unexpected
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDExtraClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping && mapping->connected)
			std::cout << "DExtraClient: NAK from " << Callsign << std::endl;
	}
	else if (IsValidKeepAlivePacket(Buffer, &Callsign))
	{
		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDExtraClientMapping *mapping = FindMappingByIp(Ip);
		if (mapping)
		{
			mapping->timeoutTimer.start();

			CClients *clients = g_Reflector.GetClients();
			auto it = clients->begin();
			std::shared_ptr<CClient> client = nullptr;
			while ((client = clients->FindNextClient(EProtocol::dextraclient, it)) != nullptr)
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

void CDExtraClientProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
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
	while ((client = clients->FindNextClient(EProtocol::dextraclient, it)) != nullptr)
	{
		if (client->GetIp() == Ip && client->GetReflectorModule() == module)
			break;
	}
	if (!client)
		client = clients->FindClient(Ip, EProtocol::dextraclient);

	if (client)
	{
		rpt1 = client->GetCallsign();
		client->Alive();
		if ((stream = g_Reflector.OpenStream(Header, client)) != nullptr)
			m_Streams[stream->GetStreamId()] = stream;
	}

	g_Reflector.ReleaseClients();
	g_Reflector.GetUsers()->Hearing(my, rpt1, rpt2, "DExtraClient");
	g_Reflector.ReleaseUsers();
}

////////////////////////////////////////////////////////////////////////////////////////
// outbound queue

void CDExtraClientProtocol::HandleQueue(void)
{
	while (!m_Queue.IsEmpty())
	{
		auto packet = m_Queue.Pop();
		const auto module = packet->GetPacketModule();

		std::lock_guard<std::mutex> lock(m_MappingMutex);
		SDExtraClientMapping *mapping = FindMappingByLocal(module);
		if (!mapping || !mapping->connected)
			continue;

		CBuffer buffer;
		bool encoded = false;

		if (packet->IsDvHeader())
		{
			// rewrite RPT1/RPT2
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
			// DExtra sends headers 5 times for reliability
			int n = packet->IsDvHeader() ? 5 : 1;
			for (int i = 0; i < n; i++)
				Send(buffer, mapping->ip);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// admin API

bool CDExtraClientProtocol::AddMapping(const std::string &host, uint16_t port, char remoteMod, char localMod)
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);
	for (auto &m : m_Mappings)
		if (m.localModule == localMod)
			return false;
	if (!g_Reflector.IsValidModule(localMod))
		return false;

	SDExtraClientMapping mapping;
	mapping.host = host;
	mapping.port = port;
	mapping.ip = CIp(host.c_str(), AF_INET, SOCK_DGRAM, port);
	mapping.remoteModule = remoteMod;
	mapping.localModule = localMod;
	mapping.keepaliveTimer.start();
	m_Mappings.push_back(mapping);

	std::cout << "DExtraClient: added mapping " << host << ":" << port
	          << " module " << remoteMod << " -> local " << localMod << std::endl;
	return true;
}

bool CDExtraClientProtocol::RemoveMapping(char localMod)
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
				auto client = clients->FindClient(it->ip, EProtocol::dextraclient);
				if (client && client->GetReflectorModule() == localMod)
					clients->RemoveClient(client);
				g_Reflector.ReleaseClients();
			}
			std::cout << "DExtraClient: removed mapping for local module " << localMod << std::endl;
			m_Mappings.erase(it);
			return true;
		}
	}
	return false;
}

std::vector<SDExtraClientMapping> CDExtraClientProtocol::GetMappings(void) const
{
	std::lock_guard<std::mutex> lock(m_MappingMutex);
	return m_Mappings;
}

////////////////////////////////////////////////////////////////////////////////////////
// mapping helpers

SDExtraClientMapping *CDExtraClientProtocol::FindMappingByLocal(char localMod)
{
	for (auto &m : m_Mappings)
		if (m.localModule == localMod) return &m;
	return nullptr;
}

SDExtraClientMapping *CDExtraClientProtocol::FindMappingByIp(const CIp &ip)
{
	for (auto &m : m_Mappings)
		if (m.ip == ip) return &m;
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////
// connection helpers

void CDExtraClientProtocol::SendConnect(SDExtraClientMapping &mapping)
{
	CBuffer buffer;
	EncodeConnectPacket(&buffer, mapping.localModule, mapping.remoteModule);
	Send(buffer, mapping.ip);
	std::cout << "DExtraClient: connecting to " << mapping.host << ":" << mapping.port
	          << " module " << mapping.remoteModule << " (local " << mapping.localModule << ")" << std::endl;
}

void CDExtraClientProtocol::SendDisconnect(SDExtraClientMapping &mapping)
{
	CBuffer buffer;
	EncodeDisconnectPacket(&buffer, mapping.localModule);
	Send(buffer, mapping.ip);
	std::cout << "DExtraClient: disconnecting from " << mapping.host
	          << " module " << mapping.remoteModule << std::endl;
}

void CDExtraClientProtocol::SendKeepalive(SDExtraClientMapping &mapping)
{
	CBuffer buffer;
	EncodeKeepAlivePacket(&buffer);
	Send(buffer, mapping.ip);
}

////////////////////////////////////////////////////////////////////////////////////////
// packet decoding

bool CDExtraClientProtocol::IsValidConnectAckPacket(const CBuffer &Buffer, CCallsign *callsign, char *reflectormodule)
{
	// DExtra ACK: 14 bytes, "ACK" at offset 10 or 11
	if (Buffer.size() == 14)
	{
		if (Buffer.data()[10] == 'A' && Buffer.data()[11] == 'C' && Buffer.data()[12] == 'K')
		{
			callsign->SetCallsign(Buffer.data(), 8);
			*reflectormodule = Buffer.data()[9];
			return callsign->IsValid();
		}
	}
	// Also accept 11-byte connect echo (XRF-style ACK: same format as connect packet)
	if (Buffer.size() == 11 && Buffer.data()[9] != ' ')
	{
		callsign->SetCallsign(Buffer.data(), 8);
		*reflectormodule = Buffer.data()[9];
		return callsign->IsValid();
	}
	return false;
}

bool CDExtraClientProtocol::IsValidConnectNackPacket(const CBuffer &Buffer, CCallsign *callsign)
{
	if (Buffer.size() == 14)
	{
		if (Buffer.data()[10] == 'N' && Buffer.data()[11] == 'A' && Buffer.data()[12] == 'K')
		{
			callsign->SetCallsign(Buffer.data(), 8);
			return callsign->IsValid();
		}
	}
	// "DISCONNECTED" packet (12 bytes)
	if (Buffer.size() == 12 && 0 == memcmp(Buffer.data(), "DISCONNECTED", 12))
	{
		return true;
	}
	return false;
}

bool CDExtraClientProtocol::IsValidKeepAlivePacket(const CBuffer &Buffer, CCallsign *callsign)
{
	if (Buffer.size() == 9)
	{
		callsign->SetCallsign(Buffer.data(), 8);
		return callsign->IsValid();
	}
	return false;
}

bool CDExtraClientProtocol::IsValidDvHeaderPacket(const CBuffer &Buffer, std::unique_ptr<CDvHeaderPacket> &header)
{
	if (56 == Buffer.size() && 0 == Buffer.Compare((uint8_t *)"DSVT", 4) &&
		0x10U == Buffer.data()[4] && 0x20U == Buffer.data()[8])
	{
		header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket(
			(struct dstar_header *)&(Buffer.data()[15]),
			*((uint16_t *)&(Buffer.data()[12])), 0x80));
		if (header && header->IsValid())
			return true;
	}
	return false;
}

bool CDExtraClientProtocol::IsValidDvFramePacket(const CBuffer &Buffer, std::unique_ptr<CDvFramePacket> &dvframe)
{
	if (27 == Buffer.size() && 0 == Buffer.Compare((uint8_t *)"DSVT", 4) &&
		0x20U == Buffer.data()[4] && 0x20U == Buffer.data()[8])
	{
		dvframe = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(
			(SDStarFrame *)&(Buffer.data()[15]),
			*((uint16_t *)&(Buffer.data()[12])), Buffer.data()[14]));
		if (dvframe && dvframe->IsValid())
			return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////
// packet encoding

void CDExtraClientProtocol::EncodeConnectPacket(CBuffer *Buffer, char localModule, char remoteModule)
{
	// DExtra connect: 11 bytes
	// Bytes 0-7: Callsign (8 chars, space-padded)
	// Byte 8:    Our module (SSID)
	// Byte 9:    Remote module (target)
	// Byte 10:   0x00 (revision: original)
	uint8_t cs[CALLSIGN_LEN];
	m_ClientCallsign.GetCallsign(cs);
	Buffer->Set(cs, CALLSIGN_LEN);
	Buffer->Append((uint8_t)localModule);
	Buffer->Append((uint8_t)remoteModule);
	Buffer->Append((uint8_t)0x00);
}

void CDExtraClientProtocol::EncodeDisconnectPacket(CBuffer *Buffer, char localModule)
{
	// DExtra disconnect: 11 bytes (same as connect but byte 9 = space)
	uint8_t cs[CALLSIGN_LEN];
	m_ClientCallsign.GetCallsign(cs);
	Buffer->Set(cs, CALLSIGN_LEN);
	Buffer->Append((uint8_t)localModule);
	Buffer->Append((uint8_t)' ');
	Buffer->Append((uint8_t)0x00);
}

void CDExtraClientProtocol::EncodeKeepAlivePacket(CBuffer *Buffer)
{
	// DExtra keepalive: 9 bytes (callsign + null)
	uint8_t cs[CALLSIGN_LEN];
	m_ClientCallsign.GetCallsign(cs);
	Buffer->Set(cs, CALLSIGN_LEN);
	Buffer->Append((uint8_t)0x00);
}

bool CDExtraClientProtocol::EncodeDvHeaderPacket(const CDvHeaderPacket &Packet, CBuffer &Buffer) const
{
	// DSVT header: 56 bytes
	uint8_t tag[] = { 'D','S','V','T',0x10,0x00,0x00,0x00,0x20,0x00,0x01,0x02 };
	struct dstar_header DstarHeader;

	Packet.ConvertToDstarStruct(&DstarHeader);

	Buffer.Set(tag, sizeof(tag));
	Buffer.Append(Packet.GetStreamId());
	Buffer.Append((uint8_t)0x80);
	Buffer.Append((uint8_t *)&DstarHeader, sizeof(struct dstar_header));

	return true;
}

bool CDExtraClientProtocol::EncodeDvFramePacket(const CDvFramePacket &Packet, CBuffer &Buffer) const
{
	// DSVT frame: 27 bytes
	uint8_t tag[] = { 'D','S','V','T',0x20,0x00,0x00,0x00,0x20,0x00,0x01,0x02 };

	Buffer.Set(tag, sizeof(tag));
	Buffer.Append(Packet.GetStreamId());
	uint8_t id = Packet.GetDstarPacketId() % 21;
	if (Packet.IsLastPacket())
		id |= 0x40U;
	Buffer.Append(id);
	Buffer.Append((uint8_t *)Packet.GetCodecData(ECodecType::dstar), 9);
	Buffer.Append((uint8_t *)Packet.GetDvData(), 3);

	return true;
}
