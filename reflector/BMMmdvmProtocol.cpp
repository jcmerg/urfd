// BMMmdvmProtocol -- Connects to Brandmeister via the MMDVM protocol
// URFD acts as a virtual MMDVM repeater client to a BM master server.
//
// Copyright (C) 2024-2026
// Licensed under the GNU General Public License v3 or later

#include <string.h>
#include <cstdlib>

#include "Global.h"
#include "BMMmdvmClient.h"
#include "BMMmdvmProtocol.h"
#include "BPTC19696.h"
#include "RS129.h"
#include "Golay2087.h"
#include "QR1676.h"

// DMR sync patterns
static uint8_t g_DmrSyncBSVoice[] = { 0x07,0x55,0xFD,0x7D,0xF7,0x5F,0x70 };
static uint8_t g_DmrSyncBSData[]  = { 0x0D,0xFF,0x57,0xD7,0x5D,0xF5,0xD0 };

// Note: DMR_VOICE_LC_HEADER_CRC_MASK, DMR_TERMINATOR_WITH_LC_CRC_MASK,
// DMR_DT_VOICE_LC_HEADER, DMR_DT_TERMINATOR_WITH_LC are defined in Protocol.h

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CBMMmdvmProtocol::Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	m_uiDmrId = g_Configure.GetUnsigned(g_Keys.bmhb.dmrid);
	m_Password = g_Configure.GetString(g_Keys.bmhb.password);
	m_Callsign = g_Configure.GetString(g_Keys.bmhb.callsign);
	m_MasterPort = (uint16_t)g_Configure.GetUnsigned(g_Keys.bmhb.port);

	// Store DMR ID as big-endian 4 bytes
	m_uiId[0] = (uint8_t)(m_uiDmrId >> 24);
	m_uiId[1] = (uint8_t)(m_uiDmrId >> 16);
	m_uiId[2] = (uint8_t)(m_uiDmrId >> 8);
	m_uiId[3] = (uint8_t)(m_uiDmrId);

	// Resolve master address
	auto addr = g_Configure.GetString(g_Keys.bmhb.address);
	m_MasterIp = CIp(addr.c_str(), AF_INET, SOCK_DGRAM, m_MasterPort);

	// Load TG mappings
	if (!m_TGMap.LoadFromConfig())
	{
		std::cerr << "BMMmdvm: failed to load TG mappings" << std::endl;
		return false;
	}

	m_State = EHBState::DISCONNECTED;

	if (!CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	m_RetryTimer.start();
	m_TimeoutTimer.start();
	m_PingTimer.start();

	std::cout << "BMMmdvm: initialized, DMR ID " << m_uiDmrId
	          << ", master " << addr << ":" << m_MasterPort << std::endl;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// task

void CBMMmdvmProtocol::Task(void)
{
	CBuffer Buffer;
	CIp     Ip;

	if (Receive4(Buffer, Ip, 20))
	{
		HandleIncoming(Buffer, Ip);
	}

	HandleStateMachine();
	CheckStreamsTimeout();

	if (m_State == EHBState::RUNNING)
	{
		HandleQueue();
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// state machine

void CBMMmdvmProtocol::HandleStateMachine(void)
{
	switch (m_State)
	{
	case EHBState::DISCONNECTED:
		if (m_RetryTimer.time() > BMHB_RETRY_PERIOD)
		{
			SendLogin();
			m_State = EHBState::WAITING_LOGIN;
			m_TimeoutTimer.start();
			m_RetryTimer.start();
		}
		break;

	case EHBState::WAITING_LOGIN:
	case EHBState::WAITING_AUTH:
	case EHBState::WAITING_CONFIG:
	case EHBState::WAITING_OPTIONS:
		if (m_RetryTimer.time() > BMHB_RETRY_PERIOD)
		{
			switch (m_State)
			{
			case EHBState::WAITING_LOGIN:  SendLogin();   break;
			case EHBState::WAITING_AUTH:   SendAuth();    break;
			case EHBState::WAITING_CONFIG: SendConfig();  break;
			case EHBState::WAITING_OPTIONS:SendOptions(); break;
			default: break;
			}
			m_RetryTimer.start();
		}
		if (m_TimeoutTimer.time() > BMHB_TIMEOUT_PERIOD)
		{
			std::cout << "BMMmdvm: handshake timeout, reconnecting" << std::endl;
			m_State = EHBState::DISCONNECTED;
			m_RetryTimer.start();
		}
		break;

	case EHBState::RUNNING:
		if (m_PingTimer.time() > BMHB_PING_PERIOD)
		{
			SendPing();
			m_PingTimer.start();
		}
		if (m_TimeoutTimer.time() > BMHB_TIMEOUT_PERIOD)
		{
			std::cout << "BMMmdvm: keepalive timeout, reconnecting" << std::endl;
			m_State = EHBState::DISCONNECTED;
			m_RetryTimer.start();
		}
		break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// incoming packet handler

void CBMMmdvmProtocol::HandleIncoming(const CBuffer &Buffer, const CIp &Ip)
{
	// Update master IP with actual source address (port may differ from config)
	m_MasterIp = Ip;

	// RPTACK
	if (Buffer.size() >= 6 && 0 == Buffer.Compare((uint8_t *)"RPTACK", 6))
	{
		switch (m_State)
		{
		case EHBState::WAITING_LOGIN:
			if (Buffer.size() >= 10)
			{
				::memcpy(m_uiSalt, Buffer.data() + 6, 4);
				SendAuth();
				m_State = EHBState::WAITING_AUTH;
				m_TimeoutTimer.start();
				m_RetryTimer.start();
			}
			break;

		case EHBState::WAITING_AUTH:
			std::cout << "BMMmdvm: authentication successful" << std::endl;
			SendConfig();
			m_State = EHBState::WAITING_CONFIG;
			m_TimeoutTimer.start();
			m_RetryTimer.start();
			break;

		case EHBState::WAITING_CONFIG:
		{
			std::string opts = m_TGMap.GetOptionsString();
			if (!opts.empty())
			{
				std::cout << "BMMmdvm: config accepted, sending options: " << opts << std::endl;
				SendOptions();
				m_State = EHBState::WAITING_OPTIONS;
			}
			else
			{
				std::cout << "BMMmdvm: connected" << std::endl;
				m_State = EHBState::RUNNING;
			}
			m_TimeoutTimer.start();
			m_RetryTimer.start();
			m_PingTimer.start();
			break;
		}

		case EHBState::WAITING_OPTIONS:
			std::cout << "BMMmdvm: connected" << std::endl;
			m_State = EHBState::RUNNING;
			m_TimeoutTimer.start();
			m_PingTimer.start();
			break;

		default:
			break;
		}
	}
	// RPTSBKN - beacon request (implicit ACK for options)
	else if (Buffer.size() >= 7 && 0 == Buffer.Compare((uint8_t *)"RPTSBKN", 7))
	{
		if (m_State == EHBState::WAITING_OPTIONS || m_State == EHBState::WAITING_CONFIG)
		{
			std::cout << "BMMmdvm: connected (beacon)" << std::endl;
			m_State = EHBState::RUNNING;
			m_TimeoutTimer.start();
			m_PingTimer.start();
		}
	}
	// MSTPONG
	else if (Buffer.size() >= 7 && 0 == Buffer.Compare((uint8_t *)"MSTPONG", 7))
	{
		m_TimeoutTimer.start();
	}
	// MSTNAK
	else if (Buffer.size() >= 6 && 0 == Buffer.Compare((uint8_t *)"MSTNAK", 6))
	{
		std::cout << "BMMmdvm: NAK in state " << (int)m_State << std::endl;
		if (m_State == EHBState::RUNNING)
		{
			m_State = EHBState::WAITING_LOGIN;
			SendLogin();
			m_RetryTimer.start();
			m_TimeoutTimer.start();
		}
		else
		{
			m_State = EHBState::DISCONNECTED;
			m_RetryTimer.start();
		}
	}
	// MSTCL
	else if (Buffer.size() >= 5 && 0 == Buffer.Compare((uint8_t *)"MSTCL", 5))
	{
		std::cout << "BMMmdvm: master closed connection" << std::endl;
		m_State = EHBState::DISCONNECTED;
		m_RetryTimer.start();
	}
	// DMRD
	else if (Buffer.size() == HOMEBREW_DATA_PACKET_LENGTH && 0 == Buffer.Compare((uint8_t *)"DMRD", 4))
	{
		if (m_State == EHBState::WAITING_OPTIONS)
		{
			m_State = EHBState::RUNNING;
			m_TimeoutTimer.start();
			m_PingTimer.start();
		}
		if (m_State == EHBState::RUNNING)
		{
			OnDMRDPacketIn(Buffer);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// auth packet helpers

void CBMMmdvmProtocol::SendLogin(void)
{
	CBuffer buf;
	buf.Set((uint8_t *)"RPTL", 4);
	buf.Append(m_uiId, 4);
	Send(buf, m_MasterIp);
}

void CBMMmdvmProtocol::SendAuth(void)
{
	size_t pwlen = m_Password.size();
	std::vector<uint8_t> input(4 + pwlen);
	::memcpy(input.data(), m_uiSalt, 4);
	::memcpy(input.data() + 4, m_Password.c_str(), pwlen);

	uint8_t digest[32];
	m_SHA256.buffer(input.data(), input.size(), digest);

	CBuffer buf;
	buf.Set((uint8_t *)"RPTK", 4);
	buf.Append(m_uiId, 4);
	buf.Append(digest, 32);
	Send(buf, m_MasterIp);
}

void CBMMmdvmProtocol::SendConfig(void)
{
	// Read config values with defaults
	double lat = 0.0, lon = 0.0;
	unsigned rxfreq = 439000000, txfreq = 439000000;
	std::string location, description, url;

	if (g_Configure.Contains(g_Keys.bmhb.latitude))
		lat = std::stod(g_Configure.GetString(g_Keys.bmhb.latitude));
	if (g_Configure.Contains(g_Keys.bmhb.longitude))
		lon = std::stod(g_Configure.GetString(g_Keys.bmhb.longitude));
	if (g_Configure.Contains(g_Keys.bmhb.rxfreq))
		rxfreq = g_Configure.GetUnsigned(g_Keys.bmhb.rxfreq);
	if (g_Configure.Contains(g_Keys.bmhb.txfreq))
		txfreq = g_Configure.GetUnsigned(g_Keys.bmhb.txfreq);
	if (g_Configure.Contains(g_Keys.bmhb.location))
		location = g_Configure.GetString(g_Keys.bmhb.location);
	if (g_Configure.Contains(g_Keys.bmhb.description))
		description = g_Configure.GetString(g_Keys.bmhb.description);
	if (g_Configure.Contains(g_Keys.bmhb.url))
		url = g_Configure.GetString(g_Keys.bmhb.url);

	char config[400];
	::memset(config, 0, sizeof(config));

	int n = ::snprintf(config, sizeof(config),
		"%-8.8s%09u%09u%02u%02u%+08.4f%+09.4f%03d%-20.20s%-19.19s%c%-124.124s%-40.40s%-40.40s",
		m_Callsign.c_str(), rxfreq, txfreq,
		1U, (unsigned)BMHB_COLOUR_CODE,
		lat, lon, 0,
		location.empty() ? "URFD Reflector" : location.c_str(),
		description.empty() ? g_Configure.GetString(g_Keys.names.callsign).c_str() : description.c_str(),
		'3',  // duplex (required by BM)
		url.c_str(),
		g_Configure.Contains(g_Keys.bmhb.firmware) ? g_Configure.GetString(g_Keys.bmhb.firmware).c_str() : "20260325_URFD",
		g_Configure.Contains(g_Keys.bmhb.software) ? g_Configure.GetString(g_Keys.bmhb.software).c_str() : "MMDVM_URFD_Virtual"
	);

	CBuffer buf;
	buf.Set((uint8_t *)"RPTC", 4);
	buf.Append(m_uiId, 4);
	buf.Append((uint8_t *)config, n);
	Send(buf, m_MasterIp);
}

void CBMMmdvmProtocol::SendOptions(void)
{
	std::string opts = m_TGMap.GetOptionsString();
	if (opts.empty())
		return;

	CBuffer buf;
	buf.Set((uint8_t *)"RPTO", 4);
	buf.Append(m_uiId, 4);
	buf.Append((uint8_t *)opts.c_str(), opts.size());
	Send(buf, m_MasterIp);
}

void CBMMmdvmProtocol::SendPing(void)
{
	CBuffer buf;
	buf.Set((uint8_t *)"RPTPING", 7);
	buf.Append(m_uiId, 4);
	Send(buf, m_MasterIp);
}

void CBMMmdvmProtocol::SendClose(void)
{
	CBuffer buf;
	buf.Set((uint8_t *)"RPTCL", 5);
	buf.Append(m_uiId, 4);
	Send(buf, m_MasterIp);
}

////////////////////////////////////////////////////////////////////////////////////////
// DMRD incoming: BM -> Reflector

void CBMMmdvmProtocol::OnDMRDPacketIn(const CBuffer &Buffer)
{
	uint32_t srcId = ((uint32_t)Buffer.data()[5] << 16) |
	                 ((uint32_t)Buffer.data()[6] << 8) |
	                  (uint32_t)Buffer.data()[7];
	uint32_t dstId = ((uint32_t)Buffer.data()[8] << 16) |
	                 ((uint32_t)Buffer.data()[9] << 8) |
	                  (uint32_t)Buffer.data()[10];
	uint32_t streamId;
	::memcpy(&streamId, Buffer.data() + 16, 4);
	if (streamId == 0) streamId = 1;

	uint8_t flags = Buffer.data()[15];

	if (!m_TGMap.IsTGMapped(dstId))
		return;

	bool isDataSync = (flags & BMHB_FLAG_DATA_SYNC) != 0;
	uint8_t dataType = flags & 0x0F;

	if (isDataSync)
	{
		if (dataType == DMR_DT_VOICE_LC_HEADER)
			OnDMRDVoiceHeaderIn(Buffer, srcId, dstId, streamId);
		else if (dataType == DMR_DT_TERMINATOR_WITH_LC)
			OnDMRDTerminatorIn(Buffer, srcId, dstId, streamId);
	}
	else
	{
		OnDMRDVoiceFrameIn(Buffer, srcId, dstId, streamId);
	}
}

void CBMMmdvmProtocol::OnDMRDVoiceHeaderIn(const CBuffer &Buffer, uint32_t srcId, uint32_t dstId, uint32_t streamId)
{
	char module = m_TGMap.TGToModule(dstId);
	if (module == ' ')
		return;

	CCallsign rpt1;
	rpt1.SetCallsign(m_Callsign, false);
	rpt1.SetCSModule(module);
	CCallsign rpt2(g_Reflector.GetCallsign());
	rpt2.SetCSModule(module);

	// Check if this BM streamId is already mapped to a valid URFD stream
	auto existing = m_IncomingStreams.find(streamId);
	if (existing != m_IncomingStreams.end())
	{
		// Already mapped - tickle the stream if it still exists
		auto stream = GetStream(existing->second, &m_MasterIp);
		if (stream)
		{
			stream->Tickle();
			return;
		}
		// Stream closed - remove stale mapping so we can re-open
		m_IncomingStreams.erase(existing);
	}

	// Generate unique URFD stream ID
	static uint16_t s_nextId = 0x100;
	uint16_t urfStreamId = s_nextId;
	s_nextId += 0x100;
	if (s_nextId == 0) s_nextId = 0x100;

	m_IncomingStreams[streamId] = urfStreamId;

	CCallsign my = DmrIdToCallsign(srcId);
	std::cout << "BMMmdvm: voice from " << my << " (ID " << srcId << ") TG" << dstId << " -> Module " << module << std::endl;

	auto header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket(srcId, CCallsign("CQCQCQ"), rpt1, rpt2, urfStreamId, 0, 0));

	OnDvHeaderPacketIn(header, m_MasterIp);
}

void CBMMmdvmProtocol::OnDMRDVoiceFrameIn(const CBuffer &Buffer, uint32_t srcId, uint32_t dstId, uint32_t streamId)
{
	auto it = m_IncomingStreams.find(streamId);
	if (it == m_IncomingStreams.end())
	{
		OnDMRDVoiceHeaderIn(Buffer, srcId, dstId, streamId);
		it = m_IncomingStreams.find(streamId);
		if (it == m_IncomingStreams.end())
			return;
	}

	uint16_t urfStreamId = it->second;
	const uint8_t *dmrframe = Buffer.data() + 20;

	// Extract 3 x 9-byte AMBE2+ from interleaved DMR frame
	uint8_t dmr3ambe[27];
	::memcpy(dmr3ambe, dmrframe, 13);
	dmr3ambe[13] = (dmr3ambe[13] & 0xF0) | (dmrframe[19] & 0x0F);
	::memcpy(dmr3ambe + 14, dmrframe + 20, 13);

	uint8_t dmrsync[7];
	dmrsync[0] = dmrframe[13] & 0x0F;
	::memcpy(dmrsync + 1, dmrframe + 14, 5);
	dmrsync[6] = dmrframe[19] & 0xF0;

	uint8_t flags = Buffer.data()[15];
	uint8_t pid = flags & 0x0F;
	if (flags & BMHB_FLAG_VOICE_SYNC)
		pid = 0;

	// Push frames directly to the stream
	auto stream = GetStream(urfStreamId, &m_MasterIp);
	if (!stream)
		return;

	for (int i = 0; i < 3; i++)
	{
		auto frame = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(
			&dmr3ambe[i * 9], dmrsync,
			urfStreamId, (uint8_t)(pid * 3 + i), (uint8_t)i, false));
		frame->SetPacketModule(stream->GetOwnerClient()->GetReflectorModule());
		stream->Push(std::move(frame));
	}
}

void CBMMmdvmProtocol::OnDMRDTerminatorIn(const CBuffer &Buffer, uint32_t srcId, uint32_t dstId, uint32_t streamId)
{
	auto it = m_IncomingStreams.find(streamId);
	if (it == m_IncomingStreams.end())
		return;

	uint16_t urfStreamId = it->second;
	m_IncomingStreams.erase(it);

	auto stream = GetStream(urfStreamId, &m_MasterIp);
	if (!stream)
		return;

	uint8_t silence[9] = { 0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B };
	uint8_t sync[7] = { 0 };
	auto frame = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(
		silence, sync, urfStreamId, 0, 0, true));
	frame->SetPacketModule(stream->GetOwnerClient()->GetReflectorModule());
	stream->Push(std::move(frame));
}

////////////////////////////////////////////////////////////////////////////////////////
// stream helper for incoming

void CBMMmdvmProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
{
	auto stream = GetStream(Header->GetStreamId(), &m_MasterIp);
	if (stream)
	{
		stream->Tickle();
		return;
	}

	CCallsign my(Header->GetMyCallsign());
	CCallsign rpt1(Header->GetRpt1Callsign());
	CCallsign rpt2(Header->GetRpt2Callsign());

	CClients *clients = g_Reflector.GetClients();
	std::shared_ptr<CClient> client = clients->FindClient(Ip, EProtocol::bmhomebrew);
	if (client == nullptr)
	{
		CCallsign cs;
		cs.SetCallsign(m_Callsign, false);
		cs.SetCSModule(Header->GetRpt2Module());
		clients->AddClient(std::make_shared<CBMMmdvmClient>(cs, Ip, Header->GetRpt2Module()));
		client = clients->FindClient(Ip, EProtocol::bmhomebrew);
	}

	if (client)
	{
		client->SetReflectorModule(Header->GetRpt2Module());
		if ((stream = g_Reflector.OpenStream(Header, client)) != nullptr)
		{
			m_Streams[stream->GetStreamId()] = stream;
		}
	}

	g_Reflector.ReleaseClients();

	g_Reflector.GetUsers()->Hearing(my, rpt1, rpt2, "BMMmdvm");
	g_Reflector.ReleaseUsers();
}

////////////////////////////////////////////////////////////////////////////////////////
// outbound queue: Reflector -> BM

uint8_t CBMMmdvmProtocol::SlotFlag(char module) const
{
	return (m_TGMap.ModuleToTimeslot(module) == 2) ? BMHB_FLAG_SLOT2 : BMHB_FLAG_SLOT1;
}

void CBMMmdvmProtocol::HandleQueue(void)
{
	while (!m_Queue.IsEmpty())
	{
		auto packet = m_Queue.Pop();

		if (packet->IsDvHeader())
		{
			auto &header = (CDvHeaderPacket &)*packet;
			char module = header.GetPacketModule();

			uint32_t tg = m_TGMap.ModuleToTG(module);
			if (tg == 0)
				continue;

			// Resolve user DMR ID
			uint32_t userSrcId = CallsignToDmrId(header.GetMyCallsign());
			if (userSrcId == 0U)
				userSrcId = header.GetMyCallsign().GetDmrid();
			if (userSrcId == 0U)
				userSrcId = m_uiDmrId;

			SBMHBStreamCache cache;
			cache.header = header;
			cache.frameCount = 0;
			cache.seqNo = 0;
			cache.pktSeqNo = 0;
			cache.streamId = (uint32_t)::rand();
			cache.srcId = userSrcId;
			m_OutboundCache[module] = cache;

			CBuffer buf;
			if (EncodeDMRDHeader(header, module, buf))
			{
				Send(buf, m_MasterIp);
			}
		}
		else if (packet->IsDvFrame())
		{
			auto &frame = (CDvFramePacket &)*packet;
			char module = frame.GetPacketModule();

			auto it = m_OutboundCache.find(module);
			if (it == m_OutboundCache.end())
			{
				// Late entry: create cache and send header
				uint32_t tg = m_TGMap.ModuleToTG(module);
				if (tg == 0)
					continue;

				SBMHBStreamCache cache;
				cache.frameCount = 0;
				cache.seqNo = 0;
				cache.pktSeqNo = 0;
				cache.streamId = (uint32_t)::rand();
				cache.srcId = m_uiDmrId;
				m_OutboundCache[module] = cache;
				it = m_OutboundCache.find(module);

				CBuffer buf;
				buf.Set((uint8_t *)"DMRD", 4);
				buf.Append(cache.pktSeqNo++);
				uint32_t srcId = m_uiDmrId;
				buf.Append((uint8_t)(srcId >> 16));
				buf.Append((uint8_t)(srcId >> 8));
				buf.Append((uint8_t)(srcId));
				buf.Append((uint8_t)(tg >> 16));
				buf.Append((uint8_t)(tg >> 8));
				buf.Append((uint8_t)(tg));
				buf.Append(m_uiId, 4);
				buf.Append((uint8_t)(SlotFlag(module) | BMHB_FLAG_DATA_SYNC | DMR_DT_VOICE_LC_HEADER));
				buf.Append((uint8_t *)&cache.streamId, 4);
				AppendVoiceLCToBuffer(&buf, srcId, tg);
				buf.Append((uint8_t)0);
				buf.Append((uint8_t)0x32);
				Send(buf, m_MasterIp);
			}

			auto &c = it->second;

			if (frame.IsLastPacket())
			{
				CBuffer buf;
				if (EncodeDMRDTerminator(c.streamId, module, buf))
					Send(buf, m_MasterIp);
				m_OutboundCache.erase(it);
				continue;
			}

			// Buffer frames in triplets (3 AMBE frames per DMRD packet)
			c.frames[c.frameCount++] = frame;

			if (c.frameCount >= 3)
			{
				CBuffer buf;
				if (EncodeDMRDVoiceFrame(c.frames[0], c.frames[1], c.frames[2],
				                          c.seqNo, c.streamId, module, buf))
				{
					Send(buf, m_MasterIp);
				}
				c.seqNo++;
				c.frameCount = 0;
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// DMRD encoding: Reflector -> BM

bool CBMMmdvmProtocol::EncodeDMRDHeader(const CDvHeaderPacket &header, char module, CBuffer &buffer)
{
	uint32_t tg = m_TGMap.ModuleToTG(module);
	auto it = m_OutboundCache.find(module);
	if (it == m_OutboundCache.end())
		return false;

	uint32_t srcId = it->second.srcId;
	uint32_t streamId = it->second.streamId;

	buffer.Set((uint8_t *)"DMRD", 4);
	buffer.Append(it->second.pktSeqNo++);
	buffer.Append((uint8_t)(srcId >> 16));
	buffer.Append((uint8_t)(srcId >> 8));
	buffer.Append((uint8_t)(srcId));
	buffer.Append((uint8_t)(tg >> 16));
	buffer.Append((uint8_t)(tg >> 8));
	buffer.Append((uint8_t)(tg));
	buffer.Append(m_uiId, 4);
	buffer.Append((uint8_t)(SlotFlag(module) | BMHB_FLAG_DATA_SYNC | DMR_DT_VOICE_LC_HEADER));
	buffer.Append((uint8_t *)&streamId, 4);
	AppendVoiceLCToBuffer(&buffer, srcId, tg);
	buffer.Append((uint8_t)0);
	buffer.Append((uint8_t)0x32);

	return true;
}

bool CBMMmdvmProtocol::EncodeDMRDVoiceFrame(
	const CDvFramePacket &frame0, const CDvFramePacket &frame1, const CDvFramePacket &frame2,
	uint8_t seqNo, uint32_t streamId, char module, CBuffer &buffer)
{
	uint32_t tg = m_TGMap.ModuleToTG(module);
	auto cit = m_OutboundCache.find(module);
	uint32_t srcId = (cit != m_OutboundCache.end()) ? cit->second.srcId : m_uiDmrId;

	buffer.Set((uint8_t *)"DMRD", 4);
	buffer.Append(cit->second.pktSeqNo++);
	buffer.Append((uint8_t)(srcId >> 16));
	buffer.Append((uint8_t)(srcId >> 8));
	buffer.Append((uint8_t)(srcId));
	buffer.Append((uint8_t)(tg >> 16));
	buffer.Append((uint8_t)(tg >> 8));
	buffer.Append((uint8_t)(tg));
	buffer.Append(m_uiId, 4);

	uint8_t voiceSeq = seqNo % 6;
	uint8_t flags = SlotFlag(module);
	flags |= (voiceSeq == 0) ? BMHB_FLAG_VOICE_SYNC : voiceSeq;
	buffer.Append(flags);

	buffer.Append((uint8_t *)&streamId, 4);

	// Build 33-byte DMR voice payload from 3 x 9 AMBE frames
	buffer.resize(53);
	::memset(buffer.data() + 20, 0, 33);
	buffer.ReplaceAt(20, frame0.GetCodecData(ECodecType::dmr), 9);
	buffer.ReplaceAt(29, frame1.GetCodecData(ECodecType::dmr), 5);
	buffer.ReplaceAt(33, (uint8_t)(buffer.at(33) & 0xF0));
	buffer.ReplaceAt(39, frame1.GetCodecData(ECodecType::dmr) + 4, 5);
	buffer.ReplaceAt(39, (uint8_t)(buffer.at(39) & 0x0F));
	buffer.ReplaceAt(44, frame2.GetCodecData(ECodecType::dmr), 9);
	ReplaceEMBInBuffer(&buffer, voiceSeq);

	buffer.Append((uint8_t)0);
	buffer.Append((uint8_t)0x32);

	return true;
}

bool CBMMmdvmProtocol::EncodeDMRDTerminator(uint32_t streamId, char module, CBuffer &buffer)
{
	uint32_t tg = m_TGMap.ModuleToTG(module);

	buffer.Set((uint8_t *)"DMRD", 4);
	buffer.Append((uint8_t)0);
	buffer.Append((uint8_t)(m_uiDmrId >> 16));
	buffer.Append((uint8_t)(m_uiDmrId >> 8));
	buffer.Append((uint8_t)(m_uiDmrId));
	buffer.Append((uint8_t)(tg >> 16));
	buffer.Append((uint8_t)(tg >> 8));
	buffer.Append((uint8_t)(tg));
	buffer.Append(m_uiId, 4);
	buffer.Append((uint8_t)(SlotFlag(module) | BMHB_FLAG_DATA_SYNC | DMR_DT_TERMINATOR_WITH_LC));
	buffer.Append((uint8_t *)&streamId, 4);
	AppendTerminatorLCToBuffer(&buffer, m_uiDmrId, tg);
	buffer.Append((uint8_t)0);
	buffer.Append((uint8_t)0x32);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// DMR frame construction helpers (adapted from CDmrmmdvmProtocol)

void CBMMmdvmProtocol::AppendVoiceLCToBuffer(CBuffer *buffer, uint32_t srcId, uint32_t dstId) const
{
	uint8_t payload[33];
	CBPTC19696 bptc;
	::memset(payload, 0, sizeof(payload));

	uint8_t lc[12];
	::memset(lc, 0, sizeof(lc));
	lc[3] = (uint8_t)LOBYTE(HIWORD(dstId));
	lc[4] = (uint8_t)HIBYTE(LOWORD(dstId));
	lc[5] = (uint8_t)LOBYTE(LOWORD(dstId));
	lc[6] = (uint8_t)LOBYTE(HIWORD(srcId));
	lc[7] = (uint8_t)HIBYTE(LOWORD(srcId));
	lc[8] = (uint8_t)LOBYTE(LOWORD(srcId));
	uint8_t parity[4];
	CRS129::encode(lc, 9, parity);
	lc[9]  = parity[2] ^ DMR_VOICE_LC_HEADER_CRC_MASK;
	lc[10] = parity[1] ^ DMR_VOICE_LC_HEADER_CRC_MASK;
	lc[11] = parity[0] ^ DMR_VOICE_LC_HEADER_CRC_MASK;

	::memcpy(payload + 13, g_DmrSyncBSData, 7);
	uint8_t slottype[3];
	::memset(slottype, 0, sizeof(slottype));
	slottype[0]  = (BMHB_COLOUR_CODE << 4) & 0xF0;
	slottype[0] |= DMR_DT_VOICE_LC_HEADER & 0x0F;
	CGolay2087::encode(slottype);
	payload[12] = (payload[12] & 0xC0) | ((slottype[0] >> 2) & 0x3F);
	payload[13] = (payload[13] & 0x0F) | ((slottype[0] << 6) & 0xC0) | ((slottype[1] >> 2) & 0x30);
	payload[19] = (payload[19] & 0xF0) | ((slottype[1] >> 2) & 0x0F);
	payload[20] = (payload[20] & 0x03) | ((slottype[1] << 6) & 0xC0) | ((slottype[2] >> 2) & 0x3C);

	bptc.encode(lc, payload);
	buffer->Append(payload, sizeof(payload));
}

void CBMMmdvmProtocol::AppendTerminatorLCToBuffer(CBuffer *buffer, uint32_t srcId, uint32_t dstId) const
{
	uint8_t payload[33];
	CBPTC19696 bptc;
	::memset(payload, 0, sizeof(payload));

	uint8_t lc[12];
	::memset(lc, 0, sizeof(lc));
	lc[3] = (uint8_t)LOBYTE(HIWORD(dstId));
	lc[4] = (uint8_t)HIBYTE(LOWORD(dstId));
	lc[5] = (uint8_t)LOBYTE(LOWORD(dstId));
	lc[6] = (uint8_t)LOBYTE(HIWORD(srcId));
	lc[7] = (uint8_t)HIBYTE(LOWORD(srcId));
	lc[8] = (uint8_t)LOBYTE(LOWORD(srcId));
	uint8_t parity[4];
	CRS129::encode(lc, 9, parity);
	lc[9]  = parity[2] ^ DMR_TERMINATOR_WITH_LC_CRC_MASK;
	lc[10] = parity[1] ^ DMR_TERMINATOR_WITH_LC_CRC_MASK;
	lc[11] = parity[0] ^ DMR_TERMINATOR_WITH_LC_CRC_MASK;

	::memcpy(payload + 13, g_DmrSyncBSData, 7);
	uint8_t slottype[3];
	::memset(slottype, 0, sizeof(slottype));
	slottype[0]  = (BMHB_COLOUR_CODE << 4) & 0xF0;
	slottype[0] |= DMR_DT_TERMINATOR_WITH_LC & 0x0F;
	CGolay2087::encode(slottype);
	payload[12] = (payload[12] & 0xC0) | ((slottype[0] >> 2) & 0x3F);
	payload[13] = (payload[13] & 0x0F) | ((slottype[0] << 6) & 0xC0) | ((slottype[1] >> 2) & 0x30);
	payload[19] = (payload[19] & 0xF0) | ((slottype[1] >> 2) & 0x0F);
	payload[20] = (payload[20] & 0x03) | ((slottype[1] << 6) & 0xC0) | ((slottype[2] >> 2) & 0x3C);

	bptc.encode(lc, payload);
	buffer->Append(payload, sizeof(payload));
}

void CBMMmdvmProtocol::ReplaceEMBInBuffer(CBuffer *buffer, uint8_t uiDmrPacketId) const
{
	if (uiDmrPacketId == 0)
	{
		buffer->ReplaceAt(33, (uint8_t)(buffer->at(33) | (g_DmrSyncBSVoice[0] & 0x0F)));
		buffer->ReplaceAt(34, g_DmrSyncBSVoice + 1, 5);
		buffer->ReplaceAt(39, (uint8_t)(buffer->at(39) | (g_DmrSyncBSVoice[6] & 0xF0)));
	}
	else
	{
		uint8_t emb[2];
		emb[0]  = (BMHB_COLOUR_CODE << 4) & 0xF0;
		emb[1]  = 0x00;
		CQR1676::encode(emb);
		buffer->ReplaceAt(33, (uint8_t)((buffer->at(33) & 0xF0) | ((emb[0] >> 4) & 0x0F)));
		buffer->ReplaceAt(34, (uint8_t)(buffer->at(34) & 0xF0));
		buffer->ReplaceAt(35, (uint8_t)0);
		buffer->ReplaceAt(36, (uint8_t)0);
		buffer->ReplaceAt(37, (uint8_t)0);
		buffer->ReplaceAt(38, (uint8_t)((buffer->at(38) & 0xF0) | ((emb[1] >> 4) & 0x0F)));
		buffer->ReplaceAt(39, (uint8_t)((buffer->at(39) & 0x0F) | ((emb[1] << 4) & 0xF0)));
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// DMR ID / Callsign helpers

uint32_t CBMMmdvmProtocol::CallsignToDmrId(const CCallsign &cs) const
{
	return g_LDid.FindDmrid(cs.GetKey());
}

CCallsign CBMMmdvmProtocol::DmrIdToCallsign(uint32_t id) const
{
	const UCallsign *ucs = g_LDid.FindCallsign(id);
	if (ucs)
		return CCallsign(*ucs);
	return CCallsign();
}

void CBMMmdvmProtocol::AppendDmrIdToBuffer(CBuffer *buffer, uint32_t id) const
{
	buffer->Append((uint8_t)(id >> 16));
	buffer->Append((uint8_t)(id >> 8));
	buffer->Append((uint8_t)(id));
}

void CBMMmdvmProtocol::AppendDmrRptrIdToBuffer(CBuffer *buffer, uint32_t id) const
{
	buffer->Append((uint8_t)(id >> 24));
	buffer->Append((uint8_t)(id >> 16));
	buffer->Append((uint8_t)(id >> 8));
	buffer->Append((uint8_t)(id));
}
