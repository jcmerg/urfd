// SvxReflectorProtocol -- Connects to an SvxLink SvxReflector via TCP/UDP
// Implements SvxReflector protocol V2 with HMAC-SHA1 auth and OPUS audio
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <iostream>

#include <openssl/hmac.h>

#include "SvxReflectorProtocol.h"
#include "SvxReflectorClient.h"
#include "Global.h"

// MSG_NOSIGNAL is not available on macOS; use SO_NOSIGPIPE instead
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

////////////////////////////////////////////////////////////////////////////////////////
// constructor / destructor

CSvxReflectorProtocol::CSvxReflectorProtocol()
	: m_State(EState::disconnected)
	, m_TcpFd(-1)
	, m_ReconnectBackoff(SVX_RECONNECT_PERIOD)
	, m_ClientId(0)
	, m_OpusEncoder(nullptr)
	, m_OpusDecoder(nullptr)
{
	m_InStream.tg = 0;
	m_InStream.module = ' ';
	m_InStream.streamId = 0;
	m_InStream.open = false;
}

CSvxReflectorProtocol::~CSvxReflectorProtocol()
{
	TcpDisconnect();
	if (m_OpusEncoder) opus_encoder_destroy(m_OpusEncoder);
	if (m_OpusDecoder) opus_decoder_destroy(m_OpusDecoder);
}

////////////////////////////////////////////////////////////////////////////////////////
// serialization helpers (big-endian)

void CSvxReflectorProtocol::PackUint16(std::vector<uint8_t> &buf, uint16_t val)
{
	buf.push_back((val >> 8) & 0xFF);
	buf.push_back(val & 0xFF);
}

void CSvxReflectorProtocol::PackUint32(std::vector<uint8_t> &buf, uint32_t val)
{
	buf.push_back((val >> 24) & 0xFF);
	buf.push_back((val >> 16) & 0xFF);
	buf.push_back((val >> 8) & 0xFF);
	buf.push_back(val & 0xFF);
}

void CSvxReflectorProtocol::PackString(std::vector<uint8_t> &buf, const std::string &str)
{
	PackUint16(buf, (uint16_t)str.size());
	buf.insert(buf.end(), str.begin(), str.end());
}

uint16_t CSvxReflectorProtocol::UnpackUint16(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint16_t val = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
	pos += 2;
	return val;
}

uint32_t CSvxReflectorProtocol::UnpackUint32(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint32_t val = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16)
	             | ((uint32_t)buf[pos+2] << 8) | buf[pos+3];
	pos += 4;
	return val;
}

std::string CSvxReflectorProtocol::UnpackString(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint16_t len = UnpackUint16(buf, pos);
	std::string str((const char *)&buf[pos], len);
	pos += len;
	return str;
}

////////////////////////////////////////////////////////////////////////////////////////
// TG mapping

char CSvxReflectorProtocol::TGToModule(uint32_t tg) const
{
	auto it = m_TGToModule.find(tg);
	return (it != m_TGToModule.end()) ? it->second : ' ';
}

uint32_t CSvxReflectorProtocol::ModuleToTG(char module) const
{
	auto it = m_ModuleToTG.find(module);
	return (it != m_ModuleToTG.end()) ? it->second : 0;
}

void CSvxReflectorProtocol::LoadTGMap(void)
{
	m_TGToModule.clear();
	m_ModuleToTG.clear();
	const auto &jdata = g_Configure.GetData();
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, 5) == "svxTG")
		{
			try {
				uint32_t tg = std::stoul(key.substr(5));
				char mod = it.value().get<std::string>()[0];
				m_TGToModule[tg] = mod;
				m_ModuleToTG[mod] = tg;
				std::cout << "SvxReflector TG mapping: TG" << tg << " <-> Module " << mod << std::endl;
			} catch (...) {}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// TCP connection

bool CSvxReflectorProtocol::TcpConnect(void)
{
	// DNS resolve
	struct addrinfo hints{}, *res;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	std::string portStr = std::to_string(g_Configure.GetUnsigned(g_Keys.svx.port));
	int err = getaddrinfo(m_Host.c_str(), portStr.c_str(), &hints, &res);
	if (err != 0)
	{
		std::cerr << "SvxReflector: DNS resolution failed for " << m_Host << ": " << gai_strerror(err) << std::endl;
		return false;
	}

	m_TcpFd = socket(res->ai_family, SOCK_STREAM, 0);
	if (m_TcpFd < 0)
	{
		freeaddrinfo(res);
		return false;
	}

	// Save server IP for UDP sends (resolve hostname to IP string)
	char ipstr[INET_ADDRSTRLEN];
	struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
	inet_ntop(AF_INET, &sa->sin_addr, ipstr, sizeof(ipstr));
	uint16_t udpPort = (uint16_t)g_Configure.GetUnsigned(g_Keys.svx.port);
	m_ServerIp = CIp(AF_INET, udpPort, ipstr);

	// non-blocking connect with timeout
	fcntl(m_TcpFd, F_SETFL, O_NONBLOCK);
	int ret = connect(m_TcpFd, res->ai_addr, res->ai_addrlen);

	freeaddrinfo(res);

	if (ret < 0 && errno != EINPROGRESS)
	{
		close(m_TcpFd);
		m_TcpFd = -1;
		return false;
	}

	// wait for connect (5s timeout)
	struct pollfd pfd = { m_TcpFd, POLLOUT, 0 };
	ret = poll(&pfd, 1, 5000);
	if (ret <= 0 || !(pfd.revents & POLLOUT))
	{
		close(m_TcpFd);
		m_TcpFd = -1;
		return false;
	}

	// check for connect error
	int sockerr = 0;
	socklen_t slen = sizeof(sockerr);
	getsockopt(m_TcpFd, SOL_SOCKET, SO_ERROR, &sockerr, &slen);
	if (sockerr != 0)
	{
		close(m_TcpFd);
		m_TcpFd = -1;
		return false;
	}

	// back to blocking with short timeouts for reads
	int flags = fcntl(m_TcpFd, F_GETFL);
	fcntl(m_TcpFd, F_SETFL, flags & ~O_NONBLOCK);
	struct timeval tv = { 0, 100000 }; // 100ms read timeout
	setsockopt(m_TcpFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	std::cout << "SvxReflector: TCP connected to " << m_Host << ":" << portStr << std::endl;
	return true;
}

void CSvxReflectorProtocol::TcpDisconnect(void)
{
	if (m_TcpFd >= 0)
	{
		close(m_TcpFd);
		m_TcpFd = -1;
	}
	m_State = EState::disconnected;
	m_InStream.open = false;
	m_ClientId = 0;
}

bool CSvxReflectorProtocol::TcpSendFrame(const uint8_t *data, uint32_t len)
{
	if (m_TcpFd < 0) return false;

	// 4-byte big-endian length header
	uint8_t hdr[4];
	hdr[0] = (len >> 24) & 0xFF;
	hdr[1] = (len >> 16) & 0xFF;
	hdr[2] = (len >> 8) & 0xFF;
	hdr[3] = len & 0xFF;

	if (send(m_TcpFd, hdr, 4, MSG_NOSIGNAL) != 4) return false;
	if (send(m_TcpFd, data, len, MSG_NOSIGNAL) != (ssize_t)len) return false;

	m_TcpHeartbeatTimer.start();
	return true;
}

bool CSvxReflectorProtocol::TcpReceiveFrame(std::vector<uint8_t> &frame)
{
	if (m_TcpFd < 0) return false;

	uint8_t hdr[4];
	ssize_t n = recv(m_TcpFd, hdr, 4, MSG_WAITALL);
	if (n == 0)
	{
		// Peer closed connection
		std::cerr << "SvxReflector: TCP connection closed by server" << std::endl;
		TcpDisconnect();
		return false;
	}
	if (n != 4)
	{
		// EAGAIN/EWOULDBLOCK means no data yet (SO_RCVTIMEO timeout) — not an error
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return false;
		// Real error
		std::cerr << "SvxReflector: TCP recv error: " << strerror(errno) << std::endl;
		TcpDisconnect();
		return false;
	}

	uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16)
	             | ((uint32_t)hdr[2] << 8) | hdr[3];

	if (len > 32768) return false; // safety limit

	frame.resize(len);
	n = recv(m_TcpFd, frame.data(), len, MSG_WAITALL);
	if (n != (ssize_t)len)
	{
		std::cerr << "SvxReflector: TCP recv incomplete payload" << std::endl;
		TcpDisconnect();
		return false;
	}

	m_TcpLastReceiveTimer.start();
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// V2 authentication

void CSvxReflectorProtocol::OnAuthChallenge(const std::vector<uint8_t> &payload)
{
	// payload: type(2) + vector_length(2) + nonce(20)
	if (payload.size() < 24) return;

	// Skip type(2) and vector length prefix(2), nonce starts at offset 4
	const uint8_t *nonce = &payload[4];

	// HMAC-SHA1(password, nonce)
	unsigned int digest_len = 20;
	uint8_t digest[20];
	HMAC(EVP_sha1(),
	     m_Password.c_str(), (int)m_Password.size(),
	     nonce, 20,
	     digest, &digest_len);

	// Build MsgAuthResponse: type(2) + callsign(string) + digest(vector<uint8_t>)
	// Field order matches ASYNC_MSG_MEMBERS(m_callsign, m_digest)
	std::vector<uint8_t> resp;
	PackUint16(resp, SVX_TCP_MSG_AUTH_RESPONSE);
	PackString(resp, m_Callsign);          // callsign FIRST (length-prefixed string)
	PackUint16(resp, 20);                  // vector<uint8_t> length prefix
	resp.insert(resp.end(), digest, digest + 20); // 20 bytes HMAC digest

	TcpSendFrame(resp.data(), (uint32_t)resp.size());
	std::cout << "SvxReflector: auth response sent for " << m_Callsign << std::endl;
}

void CSvxReflectorProtocol::OnAuthOk(void)
{
	std::cout << "SvxReflector: authentication successful" << std::endl;
	m_State = EState::connected;
}

void CSvxReflectorProtocol::OnServerInfo(const std::vector<uint8_t> &payload)
{
	if (payload.size() < 4) return;
	size_t pos = 2; // skip type
	m_ClientId = UnpackUint16(payload, pos);
	std::cout << "SvxReflector: received ServerInfo, client_id=" << m_ClientId << std::endl;

	// Send MsgNodeInfo (JSON metadata)
	std::string json = "{\"sw\":\"urfd\",\"callsign\":\"" + m_Callsign + "\"}";
	std::vector<uint8_t> msg;
	PackUint16(msg, SVX_TCP_MSG_NODE_INFO);
	PackString(msg, json);
	TcpSendFrame(msg.data(), (uint32_t)msg.size());

	// Select all configured TGs
	for (const auto &tg : m_TGToModule)
	{
		std::vector<uint8_t> sel;
		PackUint16(sel, SVX_TCP_MSG_SELECT_TG);
		PackUint32(sel, tg.first);
		TcpSendFrame(sel.data(), (uint32_t)sel.size());
		std::cout << "SvxReflector: selected TG" << tg.first << std::endl;
	}

	// Start UDP heartbeats
	m_UdpHeartbeatTimer.start();
	m_UdpLastReceiveTimer.start();

	// Reset reconnect backoff
	m_ReconnectBackoff = SVX_RECONNECT_PERIOD;
}

////////////////////////////////////////////////////////////////////////////////////////
// TCP message dispatcher

void CSvxReflectorProtocol::OnTcpMessage(uint16_t type, const std::vector<uint8_t> &payload)
{
	switch (type)
	{
		case SVX_TCP_MSG_HEARTBEAT:
			break; // timer already reset by TcpReceiveFrame
		case SVX_TCP_MSG_AUTH_CHALLENGE:
			OnAuthChallenge(payload);
			break;
		case SVX_TCP_MSG_AUTH_OK:
			OnAuthOk();
			break;
		case SVX_TCP_MSG_SERVER_INFO:
			OnServerInfo(payload);
			break;
		case SVX_TCP_MSG_TALKER_START:
			OnTalkerStart(payload);
			break;
		case SVX_TCP_MSG_TALKER_STOP:
			OnTalkerStop(payload);
			break;
		case SVX_TCP_MSG_ERROR:
		{
			size_t pos = 2;
			std::string errmsg = (payload.size() > 2) ? UnpackString(payload, pos) : "unknown";
			std::cerr << "SvxReflector: error from server: " << errmsg << std::endl;
			TcpDisconnect();
			m_ReconnectBackoff = 300; // 5min on server error
			break;
		}
		case SVX_TCP_MSG_NODE_LIST:
		case SVX_TCP_MSG_NODE_JOINED:
		case SVX_TCP_MSG_NODE_LEFT:
			break; // informational, ignore
		default:
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CSvxReflectorProtocol::Initialize(const char *type, const EProtocol ptype,
	const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	m_Host = g_Configure.GetString(g_Keys.svx.host);
	m_Password = g_Configure.GetString(g_Keys.svx.password);
	m_Callsign = g_Configure.GetString(g_Keys.svx.callsign);

	LoadTGMap();

	// Initialize OPUS encoder/decoder at 8kHz mono
	int err;
	m_OpusEncoder = opus_encoder_create(8000, 1, OPUS_APPLICATION_VOIP, &err);
	if (err != OPUS_OK)
	{
		std::cerr << "SvxReflector: opus_encoder_create failed: " << opus_strerror(err) << std::endl;
		return false;
	}
	m_OpusDecoder = opus_decoder_create(8000, 1, &err);
	if (err != OPUS_OK)
	{
		std::cerr << "SvxReflector: opus_decoder_create failed: " << opus_strerror(err) << std::endl;
		return false;
	}

	// Initialize base protocol (opens UDP socket)
	if (!CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	// Start timers
	m_ReconnectTimer.start();

	std::cout << "SvxReflector: initialized, connecting to " << m_Host << std::endl;
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// task (main loop)

void CSvxReflectorProtocol::Task(void)
{
	// Handle TCP connection state machine
	switch (m_State)
	{
		case EState::disconnected:
			if (m_ReconnectTimer.time() >= m_ReconnectBackoff)
			{
				std::cout << "SvxReflector: attempting TCP connect to " << m_Host << std::endl;
				m_ReconnectTimer.start();
				if (TcpConnect())
				{
					// Send ProtoVer(2,0)
					std::vector<uint8_t> ver;
					PackUint16(ver, SVX_TCP_MSG_PROTO_VER);
					PackUint16(ver, 2); // major
					PackUint16(ver, 0); // minor
					TcpSendFrame(ver.data(), (uint32_t)ver.size());
					m_State = EState::authenticating;
					m_TcpHeartbeatTimer.start();
					m_TcpLastReceiveTimer.start();
				}
				else
				{
					std::cerr << "SvxReflector: TCP connect failed, next attempt in " << std::min(m_ReconnectBackoff * 2, 60) << "s" << std::endl;
					// Increase backoff: 5, 10, 20, 40, 60, 60, ...
					if (m_ReconnectBackoff < 60)
						m_ReconnectBackoff = std::min(m_ReconnectBackoff * 2, 60);
				}
			}
			break;

		case EState::authenticating:
		case EState::connected:
		{
			// Receive TCP frames
			std::vector<uint8_t> frame;
			while (TcpReceiveFrame(frame))
			{
				if (frame.size() >= 2)
				{
					uint16_t type = ((uint16_t)frame[0] << 8) | frame[1];
					OnTcpMessage(type, frame);
				}
			}

			// Check TCP timeout
			if (m_TcpLastReceiveTimer.time() > SVX_TCP_KEEPALIVE_TIMEOUT)
			{
				std::cerr << "SvxReflector: TCP heartbeat timeout" << std::endl;
				TcpDisconnect();
				m_ReconnectTimer.start();
				break;
			}

			// Send TCP heartbeat
			if (m_TcpHeartbeatTimer.time() >= SVX_TCP_KEEPALIVE_PERIOD)
			{
				std::vector<uint8_t> hb;
				PackUint16(hb, SVX_TCP_MSG_HEARTBEAT);
				TcpSendFrame(hb.data(), (uint32_t)hb.size());
			}

			if (m_State == EState::connected)
			{
				// Receive UDP audio
				CBuffer buffer;
				CIp ip;
				if (Receive4(buffer, ip, 20))
				{
					m_UdpLastReceiveTimer.start();
					if (buffer.size() >= 4)
					{
						uint16_t type = ((uint16_t)buffer.data()[0] << 8) | buffer.data()[1];
						if (type == SVX_UDP_MSG_AUDIO)
							OnUdpAudio(buffer);
						else if (type == SVX_UDP_MSG_FLUSH_SAMPLES)
							OnUdpFlush();
					}
				}

				// Send UDP heartbeat
				if (m_UdpHeartbeatTimer.time() >= SVX_UDP_KEEPALIVE_PERIOD)
				{
					CBuffer hb;
					uint8_t hbdata[4];
					hbdata[0] = 0; hbdata[1] = SVX_UDP_MSG_HEARTBEAT;
					hbdata[2] = (m_ClientId >> 8) & 0xFF;
					hbdata[3] = m_ClientId & 0xFF;
					hb.Set(hbdata, 4);
					Send(hb, m_ServerIp);
					m_UdpHeartbeatTimer.start();
				}

				// Check UDP timeout
				if (m_UdpLastReceiveTimer.time() > SVX_UDP_KEEPALIVE_TIMEOUT)
				{
					std::cerr << "SvxReflector: UDP heartbeat timeout" << std::endl;
					TcpDisconnect();
					m_ReconnectTimer.start();
				}

				// Handle outbound queue
				HandleQueue();
			}
			break;
		}

		default:
			break;
	}
}

void CSvxReflectorProtocol::HandleKeepalives(void)
{
	// handled in Task() directly
}

////////////////////////////////////////////////////////////////////////////////////////
// incoming audio path: SvxReflector -> urfd streams

void CSvxReflectorProtocol::OnTalkerStart(const std::vector<uint8_t> &payload)
{
	// payload: type(2) + tg(4) + ...
	if (payload.size() < 6) return;
	size_t pos = 2;
	uint32_t tg = UnpackUint32(payload, pos);
	char module = TGToModule(tg);
	if (module == ' ')
	{
		std::cout << "SvxReflector: talker start on unmapped TG" << tg << ", ignoring" << std::endl;
		return;
	}

	// Generate a stream ID in range 0x5C00-0x5CFF
	static uint8_t counter = 0;
	uint16_t streamId = 0x5C00 | (counter++ & 0xFF);

	m_InStream.tg = tg;
	m_InStream.module = module;
	m_InStream.streamId = streamId;
	m_InStream.open = false;

	std::cout << "SvxReflector: talker start on TG" << tg << " -> Module " << module << std::endl;
}

void CSvxReflectorProtocol::OnTalkerStop(const std::vector<uint8_t> &payload)
{
	uint32_t tg = m_InStream.tg;
	if (m_InStream.open)
	{
		OnUdpFlush();
	}
	m_InStream.tg = 0;
	m_InStream.module = ' ';
	m_InStream.streamId = 0;
	m_InStream.open = false;
	std::cout << "SvxReflector: talker stop on TG" << tg << std::endl;
}

void CSvxReflectorProtocol::OnUdpAudio(const CBuffer &buffer)
{
	if (m_InStream.tg == 0)
		return;

	// UDP audio format from server: type(2) + opus_data
	// Minimum: type(2) + at least 1 byte of opus data
	if (buffer.size() < 3)
		return;

	// OPUS decode to PCM
	int16_t pcm[160];
	int samples = opus_decode(m_OpusDecoder, buffer.data() + 2, (int)(buffer.size() - 2), pcm, 160, 0);
	if (samples <= 0)
	{
		std::cerr << "SvxReflector: opus_decode failed: " << opus_strerror(samples) << std::endl;
		return;
	}

	// On first audio frame, open a stream with a header packet
	if (!m_InStream.open)
	{
		CCallsign my;
		my.SetCallsign(m_Callsign, false);
		CCallsign rpt1;
		rpt1.SetCallsign(m_Callsign, false);
		rpt1.SetCSModule(m_InStream.module);
		CCallsign rpt2 = m_ReflectorCallsign;
		rpt2.SetCSModule(m_InStream.module);
		auto header = std::unique_ptr<CDvHeaderPacket>(
			new CDvHeaderPacket(my, CCallsign("CQCQCQ"), rpt1, rpt2, m_InStream.streamId, true));
		OnDvHeaderPacketIn(header, m_ServerIp);
		m_InStream.open = true;
	}

	// Create frame and push to stream
	auto frame = std::unique_ptr<CDvFramePacket>(
		new CDvFramePacket(pcm, m_InStream.streamId, false));
	OnDvFramePacketIn(frame, &m_ServerIp);
}

void CSvxReflectorProtocol::OnUdpFlush(void)
{
	if (!m_InStream.open)
		return;

	// Send a last packet to close the stream
	int16_t silence[160] = {};
	auto frame = std::unique_ptr<CDvFramePacket>(
		new CDvFramePacket(silence, m_InStream.streamId, true));
	frame->SetLastPacket(true);
	OnDvFramePacketIn(frame, &m_ServerIp);

	// Reset state
	m_InStream.open = false;
	m_InStream.tg = 0;
	m_InStream.module = ' ';
	m_InStream.streamId = 0;
}

void CSvxReflectorProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
{
	// Check if stream already open
	auto stream = GetStream(Header->GetStreamId(), &Ip);
	if (stream)
	{
		stream->Tickle();
		return;
	}

	CCallsign my(Header->GetMyCallsign());
	CCallsign rpt1(Header->GetRpt1Callsign());
	CCallsign rpt2(Header->GetRpt2Callsign());

	// Find or create client
	CClients *clients = g_Reflector.GetClients();
	char module = Header->GetRpt2Module();
	CCallsign cs;
	cs.SetCallsign(m_Callsign, false);
	cs.SetCSModule(module);
	std::shared_ptr<CClient> client = clients->FindClient(cs, module, Ip, EProtocol::svxreflector);
	if (client == nullptr)
	{
		clients->AddClient(std::make_shared<CSvxReflectorClient>(cs, Ip, module));
		client = clients->FindClient(cs, module, Ip, EProtocol::svxreflector);
	}

	if (client)
	{
		client->Alive();
		client->SetReflectorModule(module);
		if ((stream = g_Reflector.OpenStream(Header, client)) != nullptr)
		{
			m_Streams[stream->GetStreamId()] = stream;
		}
	}

	g_Reflector.ReleaseClients();

	g_Reflector.GetUsers()->Hearing(my, rpt1, rpt2, "SvxReflector");
	g_Reflector.ReleaseUsers();
}

////////////////////////////////////////////////////////////////////////////////////////
// outgoing audio path: urfd streams -> SvxReflector

void CSvxReflectorProtocol::HandleQueue(void)
{
	while (!m_Queue.IsEmpty())
	{
		auto packet = m_Queue.Pop();
		const char module = packet->GetPacketModule();

		if (packet->IsDvHeader())
		{
			// Look up TG for this module
			uint32_t tg = ModuleToTG(module);
			if (tg != 0)
			{
				m_OutStreamTG[module] = tg;
			}
		}
		else if (packet->IsDvFrame())
		{
			auto it = m_OutStreamTG.find(module);
			if (it == m_OutStreamTG.end())
				continue;

			uint32_t tg = it->second;
			const CDvFramePacket &frame = (const CDvFramePacket &)*packet;

			if (packet->IsLastPacket())
			{
				// Send UDP flush to signal end of transmission
				CBuffer flushBuf;
				uint8_t flushData[2];
				flushData[0] = (SVX_UDP_MSG_FLUSH_SAMPLES >> 8) & 0xFF;
				flushData[1] = SVX_UDP_MSG_FLUSH_SAMPLES & 0xFF;
				flushBuf.Set(flushData, 2);
				Send(flushBuf, m_ServerIp);

				// Cleanup
				m_OutStreamTG.erase(it);
			}
			else
			{
				// Get PCM data and encode/send
				const int16_t *pcm = (const int16_t *)frame.GetCodecData(ECodecType::usrp);
				if (pcm)
				{
					EncodeAndSendAudio(pcm, tg);
				}
			}
		}
	}
}

void CSvxReflectorProtocol::EncodeAndSendAudio(const int16_t *pcm, uint32_t tg)
{
	// OPUS encode
	uint8_t opusBuf[512];
	int opusLen = opus_encode(m_OpusEncoder, pcm, 160, opusBuf, sizeof(opusBuf));
	if (opusLen <= 0)
	{
		std::cerr << "SvxReflector: opus_encode failed: " << opus_strerror(opusLen) << std::endl;
		return;
	}

	// Build UDP packet: type(2) + opus_data
	CBuffer buf;
	uint8_t hdr[2];
	hdr[0] = (SVX_UDP_MSG_AUDIO >> 8) & 0xFF;
	hdr[1] = SVX_UDP_MSG_AUDIO & 0xFF;
	buf.Set(hdr, 2);
	buf.Append(opusBuf, opusLen);

	Send(buf, m_ServerIp);
}
