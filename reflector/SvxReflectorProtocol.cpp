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
#include <regex>
#include <sstream>
#include <map>

#include "SvxReflectorProtocol.h"
#include "SvxReflectorClient.h"
#include "Global.h"

// Extract valid amateur radio callsign from SvxLink callsign
// e.g. "DL4JC-APP" -> "DL4JC", "DG1RPN-HS" -> "DG1RPN", "F1ABC" -> "F1ABC"
// Pattern: 1-2 letters + 1+ digits + 1-4 letters
static std::string ExtractCallsign(const std::string &svxCallsign)
{
	static const std::regex csRegex("^([A-Z]{1,2}[0-9]+[A-Z]{1,4})");
	std::smatch match;
	if (std::regex_search(svxCallsign, match, csRegex))
		return match[1].str();
	// Fallback: strip everything from first dash
	auto dash = svxCallsign.find('-');
	if (dash != std::string::npos && dash >= 3)
		return svxCallsign.substr(0, dash);
	return svxCallsign;
}

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
	, m_UdpSeq(0)
	, m_OpusEncoder(nullptr)
	, m_OpusDecoder(nullptr)
	, m_RxGainNum(256)
	, m_TxGainNum(256)
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
	std::lock_guard<std::mutex> lock(m_TGMutex);
	auto it = m_TGToModule.find(tg);
	return (it != m_TGToModule.end()) ? it->second : ' ';
}

uint32_t CSvxReflectorProtocol::ModuleToTG(char module) const
{
	std::lock_guard<std::mutex> lock(m_TGMutex);
	auto it = m_ModuleToTG.find(module);
	return (it != m_ModuleToTG.end()) ? it->second : 0;
}

void CSvxReflectorProtocol::RefreshActivityByModule(char module)
{
	std::lock_guard<std::mutex> lock(m_TGMutex);
	auto modIt = m_ModuleToTG.find(module);
	if (modIt == m_ModuleToTG.end())
		return;
	uint32_t tg = modIt->second;
	auto dynIt = m_DynTGs.find(tg);
	if (dynIt != m_DynTGs.end())
	{
		auto newExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(900);
		if (newExpiry > dynIt->second.expires)
			dynIt->second.expires = newExpiry;
	}
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

void CSvxReflectorProtocol::ReloadStaticTGMap(void)
{
	std::lock_guard<std::mutex> lock(m_TGMutex);

	// Collect dynamic entries to preserve
	std::unordered_map<uint32_t, SDynTG> savedDynTGs = m_DynTGs;
	std::unordered_map<uint32_t, char> savedDynMappings;
	for (auto &[tg, dynInfo] : m_DynTGs)
	{
		auto it = m_TGToModule.find(tg);
		if (it != m_TGToModule.end())
			savedDynMappings[tg] = it->second;
	}

	// Reload static from config
	m_TGToModule.clear();
	m_ModuleToTG.clear();
	const auto &jdata = g_Configure.GetData();
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, 5) != "svxTG") continue;
		try {
			uint32_t tg = std::stoul(key.substr(5));
			char mod = it.value().get<std::string>()[0];
			m_TGToModule[tg] = mod;
			m_ModuleToTG[mod] = tg;
		} catch (...) {}
	}

	// Re-add dynamic entries (skip if TG now exists as static)
	m_DynTGs.clear();
	for (auto &[tg, dynInfo] : savedDynTGs)
	{
		if (m_TGToModule.find(tg) != m_TGToModule.end())
			continue;  // now static
		auto mapIt = savedDynMappings.find(tg);
		if (mapIt == savedDynMappings.end())
			continue;
		char mod = mapIt->second;
		m_TGToModule[tg] = mod;
		if (m_ModuleToTG.find(mod) == m_ModuleToTG.end())
			m_ModuleToTG[mod] = tg;
		m_DynTGs[tg] = dynInfo;
	}

	std::cout << "SvxReflector: TG mappings reloaded (" << m_TGToModule.size() << " total)" << std::endl;
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
	struct timeval tv = { 0, 5000 }; // 5ms read timeout — keep short for UDP audio
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

	// Non-blocking check: return immediately if no TCP data waiting
	struct pollfd pfd = { m_TcpFd, POLLIN, 0 };
	if (poll(&pfd, 1, 0) <= 0)
		return false;

	uint8_t hdr[4];
	ssize_t n = recv(m_TcpFd, hdr, 4, MSG_WAITALL);
	if (n == 0)
	{
		std::cerr << "SvxReflector: TCP connection closed by server" << std::endl;
		TcpDisconnect();
		return false;
	}
	if (n != 4)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return false;
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
	m_ClientId = UnpackUint32(payload, pos);
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

	// Send initial UDP heartbeat immediately so server learns our UDP address
	{
		m_UdpSeq = 0;
		CBuffer hb;
		BuildUdpHeader(hb, SVX_UDP_MSG_HEARTBEAT);
		Send(hb, m_ServerIp);
		std::cout << "SvxReflector: initial UDP heartbeat sent, client_id=" << m_ClientId << std::endl;
	}
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

	// Parse BlockProtocols (comma-separated, e.g. "MMDVMClient,USRP")
	if (g_Configure.Contains(g_Keys.svx.blockprotocols))
	{
		const std::map<std::string, EProtocol> protoMap = {
			{"MMDVMClient", EProtocol::mmdvmclient}, {"DExtra", EProtocol::dextra},
			{"DPlus", EProtocol::dplus}, {"DCS", EProtocol::dcs},
			{"DMRPlus", EProtocol::dmrplus}, {"DMRMMDVM", EProtocol::dmrmmdvm},
			{"YSF", EProtocol::ysf}, {"M17", EProtocol::m17},
			{"NXDN", EProtocol::nxdn}, {"P25", EProtocol::p25},
			{"USRP", EProtocol::usrp}, {"URF", EProtocol::urf},
			{"XLXPeer", EProtocol::xlxpeer}, {"G3", EProtocol::g3},
		};
		std::istringstream ss(g_Configure.GetString(g_Keys.svx.blockprotocols));
		std::string token;
		while (std::getline(ss, token, ','))
		{
			token.erase(0, token.find_first_not_of(" \t"));
			token.erase(token.find_last_not_of(" \t") + 1);
			auto it = protoMap.find(token);
			if (it != protoMap.end())
			{
				m_BlockedSources.insert(it->second);
				std::cout << "SvxReflector: blocking protocol " << token << std::endl;
			}
			else if (!token.empty())
				std::cerr << "SvxReflector: unknown protocol in BlockProtocols: " << token << std::endl;
		}
	}

	// Parse gain settings (dB, range -24 to +24, default 0)
	if (g_Configure.Contains(g_Keys.svx.rxgain))
	{
		int db = std::stoi(g_Configure.GetString(g_Keys.svx.rxgain));
		if (db < -40) db = -40; else if (db > 40) db = 40;
		m_RxGainNum = (int32_t)roundf(256.0f * powf(10.0f, (float)db / 20.0f));
		std::cout << "SvxReflector: RxGain = " << db << " dB" << std::endl;
	}
	if (g_Configure.Contains(g_Keys.svx.txgain))
	{
		int db = std::stoi(g_Configure.GetString(g_Keys.svx.txgain));
		if (db < -40) db = -40; else if (db > 40) db = 40;
		m_TxGainNum = (int32_t)roundf(256.0f * powf(10.0f, (float)db / 20.0f));
		std::cout << "SvxReflector: TxGain = " << db << " dB" << std::endl;
	}

	LoadTGMap();

	// OPUS encoder at 16kHz (SvxReflector expects 16kHz OPUS)
	// OPUS decoder at 8kHz (libopus resamples 16kHz->8kHz internally, clean anti-aliased)
	int err;
	m_OpusEncoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_AUDIO, &err);
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
	// Handle admin-requested reconnect
	if (m_ReconnectRequested.exchange(false))
	{
		std::cout << "SvxReflector: reconnect requested" << std::endl;
		TcpDisconnect();
		m_ReconnectTimer.start();
	}

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
			if (m_State == EState::connected)
			{
				// Drain all pending UDP audio packets (non-blocking)
				CBuffer buffer;
				CIp ip;
				if (!Receive4(buffer, ip, 20))
				{
					// No UDP data within 20ms — skip to TCP/timers
				}
				else
				{
					do {
						m_UdpLastReceiveTimer.start();
						if (buffer.size() >= 2)
						{
							uint16_t type = ((uint16_t)buffer.data()[0] << 8) | buffer.data()[1];
							if (type == SVX_UDP_MSG_AUDIO)
								OnUdpAudio(buffer);
							else if (type == SVX_UDP_MSG_FLUSH_SAMPLES)
								OnUdpFlush();
							else if (type == SVX_UDP_MSG_HEARTBEAT)
							{} // heartbeat OK
							else if (type == SVX_UDP_MSG_ALL_FLUSHED)
							{} // acknowledge flush
						}
					} while (Receive4(buffer, ip, 0));
				}
			}

			// Receive TCP frames (non-blocking via poll)
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

				// Send UDP heartbeat
				if (m_UdpHeartbeatTimer.time() >= SVX_UDP_KEEPALIVE_PERIOD)
				{
					CBuffer hb;
					BuildUdpHeader(hb, SVX_UDP_MSG_HEARTBEAT);
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

				// Process pending dynamic TG select/deselect commands
				{
					std::lock_guard<std::mutex> lock(m_PendingMutex);
					for (uint32_t tg : m_PendingSelectTG)
					{
						std::vector<uint8_t> sel;
						PackUint16(sel, SVX_TCP_MSG_SELECT_TG);
						PackUint32(sel, tg);
						TcpSendFrame(sel.data(), (uint32_t)sel.size());
						std::cout << "SvxReflector: dynamically selected TG" << tg << std::endl;
					}
					m_PendingSelectTG.clear();
					for (uint32_t tg : m_PendingDeselectTG)
					{
						(void)tg;  // logged for context
						std::cout << "SvxReflector: deselected TG" << tg << std::endl;
					}
					m_PendingDeselectTG.clear();
				}

				// Purge expired dynamic TGs
				bool allTGsGone = false;
				{
					std::lock_guard<std::mutex> lock(m_TGMutex);
					auto now = std::chrono::steady_clock::now();
					for (auto it = m_DynTGs.begin(); it != m_DynTGs.end(); )
					{
						if (now >= it->second.expires)
						{
							uint32_t tg = it->first;
							auto tgIt = m_TGToModule.find(tg);
							if (tgIt != m_TGToModule.end())
							{
								char mod = tgIt->second;
								// Only clear primary mapping if this TG was the primary
								auto modIt = m_ModuleToTG.find(mod);
								if (modIt != m_ModuleToTG.end() && modIt->second == tg)
									m_ModuleToTG.erase(modIt);
								m_TGToModule.erase(tgIt);
								std::cout << "SvxReflector: dynamic TG" << tg << " expired (Module " << mod << ")" << std::endl;

								// Remove orphaned SVX client for this module if no TGs remain
								bool hasTGsLeft = false;
								for (const auto &t : m_TGToModule)
									if (t.second == mod) { hasTGsLeft = true; break; }
								if (!hasTGsLeft)
								{
									CClients *clients = g_Reflector.GetClients();
									for (auto cit = clients->begin(); cit != clients->end(); ++cit)
									{
										if ((*cit)->GetProtocol() == EProtocol::svxreflector && (*cit)->GetReflectorModule() == mod)
										{
											clients->RemoveClient(*cit);
											break;
										}
									}
									g_Reflector.ReleaseClients();
								}
							}
							it = m_DynTGs.erase(it);
						}
						else
							++it;
					}
					allTGsGone = m_TGToModule.empty();
				}

				// If all TGs gone, send SELECT_TG(0) to unsubscribe from server
				if (allTGsGone)
				{
					std::vector<uint8_t> sel;
					PackUint16(sel, SVX_TCP_MSG_SELECT_TG);
					PackUint32(sel, 0);
					TcpSendFrame(sel.data(), (uint32_t)sel.size());
					std::cout << "SvxReflector: all TGs expired, sent SELECT_TG(0)" << std::endl;
				}

				// Handle end of streaming timeout
				CheckStreamsTimeout();

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

void CSvxReflectorProtocol::CloseInStream(void)
{
	if (m_InStream.open)
	{
		auto it = m_Streams.find(m_InStream.streamId);
		if (it != m_Streams.end() && it->second)
		{
			// Flush OPUS decoder to reset state (like svxlink's flushEncodedSamples)
			if (m_OpusDecoder)
			{
				int16_t dummy[160];
				[[maybe_unused]] int ret = opus_decode(m_OpusDecoder, NULL, 0, dummy, 160, 0);
			}
			// Close with silence — lastPcm often contains squelch tail / noise
			// that causes D-Star artifacts (crackle, tones) when transcoded to AMBE
			int16_t silence[160] = {};
			auto lastFrame = std::unique_ptr<CDvFramePacket>(
				new CDvFramePacket(silence, m_InStream.streamId, true, ECodecType::svx));
			lastFrame->SetPacketModule(m_InStream.module);
			it->second->Push(std::move(lastFrame));
			g_Reflector.CloseStream(it->second);
			m_Streams.erase(it);
		}

		m_InStream.open = false;
		m_InStream.streamId = 0;
	}
	m_InStream.tg = 0;
	m_InStream.module = ' ';
	m_InStream.talkerCallsign.clear();
}

void CSvxReflectorProtocol::OnTalkerStart(const std::vector<uint8_t> &payload)
{
	// payload: type(2) + tg(4) + callsign(string)
	if (payload.size() < 6) return;
	size_t pos = 2;
	uint32_t tg = UnpackUint32(payload, pos);
	char module = TGToModule(tg);
	if (module == ' ')
		return;

	// Refresh dynamic TG TTL on activity
	{
		std::lock_guard<std::mutex> lock(m_TGMutex);
		auto dynIt = m_DynTGs.find(tg);
		if (dynIt != m_DynTGs.end())
		{
			auto newExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(900);
			if (newExpiry > dynIt->second.expires)
				dynIt->second.expires = newExpiry;
		}
	}

	// Extract amateur radio callsign from SvxLink callsign (e.g. "DL4JC-APP" -> "DL4JC")
	std::string talkerCs;
	if (pos < payload.size())
	{
		std::string raw = UnpackString(payload, pos);
		talkerCs = ExtractCallsign(raw);
	}

	m_InStream.tg = tg;
	m_InStream.talkerCallsign = talkerCs;

	if (m_InStream.open && m_InStream.module == module)
	{
		// Reuse existing open stream on same module
		// TalkerStart/Stop are informational — the UDP audio stream is continuous
		// Closing here would cut off audio that's still in-flight via UDP
	}
	else
	{
		// New module or no open stream - will open on first audio frame
		if (m_InStream.open)
			CloseInStream();
		m_InStream.module = module;
		m_InStream.open = false;
		static uint8_t counter = 0;
		m_InStream.streamId = 0x5C00 | (counter++ & 0xFF);
	}

	std::cout << "SvxReflector: talker start on TG" << tg << " -> Module " << module << " by " << talkerCs << std::endl;
}

void CSvxReflectorProtocol::OnTalkerStop(const std::vector<uint8_t> &payload)
{
	// TalkerStop is informational only — the UDP audio stream continues.
	// The stream is closed by OnUdpFlush when audio actually stops.
	uint32_t tg = 0;
	if (payload.size() >= 6)
	{
		size_t pos = 2;
		tg = UnpackUint32(payload, pos);
	}
	std::cout << "SvxReflector: talker stop on TG" << tg << " (stream stays open)" << std::endl;
}

void CSvxReflectorProtocol::OnUdpAudio(const CBuffer &buffer)
{
	// Ignore audio if no active talker
	if (m_InStream.module == ' ' || m_InStream.tg == 0)
		return;

	// V2 UDP audio: type(2) + client_id(2) + seq(2) + opus_len(2) + opus_data
	if (buffer.size() < 9)
		return;

	uint16_t opusLen = ((uint16_t)buffer.data()[6] << 8) | buffer.data()[7];
	if ((size_t)(8 + opusLen) > buffer.size())
	{
		std::cerr << "SvxReflector: audio packet too short: opusLen=" << opusLen << " bufSize=" << buffer.size() << std::endl;
		return;
	}

	// Decode OPUS to 8kHz PCM (libopus resamples from 16kHz internally)
	int16_t pcm[160];
	int samples = opus_decode(m_OpusDecoder, buffer.data() + 8, opusLen, pcm, 160, 0);
	if (samples <= 0)
	{
		std::cerr << "SvxReflector: opus_decode failed: " << opus_strerror(samples) << std::endl;
		return;
	}
	// Zero-pad if fewer than 160 samples
	for (int i = samples; i < 160; i++)
		pcm[i] = 0;

	// Apply RX gain
	if (m_RxGainNum != 256)
	{
		for (int i = 0; i < 160; i++)
			pcm[i] = (int16_t)((pcm[i] * m_RxGainNum) >> 8);
	}

	// On first audio frame, open a stream with a header packet
	if (!m_InStream.open)
	{
		// Use talker callsign if available, otherwise reflector callsign
		std::string userCs = m_InStream.talkerCallsign.empty() ? m_Callsign : m_InStream.talkerCallsign;
		CCallsign my;
		my.SetCallsign(userCs, true); // true = lookup DMR ID
		if (my.GetDmrid() == 0)
			std::cout << "SvxReflector: no DMR ID found for " << userCs << " - DMR output depends on MMDVMClient FallbackDmrId" << std::endl;
		CCallsign rpt1(g_Reflector.GetCallsign());
		rpt1.SetCSModule(m_InStream.module);
		CCallsign rpt2 = m_ReflectorCallsign;
		rpt2.SetCSModule(m_InStream.module);
		auto header = std::unique_ptr<CDvHeaderPacket>(
			new CDvHeaderPacket(my, CCallsign("CQCQCQ"), rpt1, rpt2, m_InStream.streamId, true));
		OnDvHeaderPacketIn(header, m_ServerIp);
		m_InStream.open = true;
		std::cout << "SvxReflector: stream opened for " << userCs << " (DMR ID " << my.GetDmrid() << ") on module " << m_InStream.module << std::endl;
	}

	// Cache last PCM for clean last-frame on close
	memcpy(m_InStream.lastPcm, pcm, sizeof(m_InStream.lastPcm));

	// Create frame and push directly to stream
	auto frame = std::unique_ptr<CDvFramePacket>(
		new CDvFramePacket(pcm, m_InStream.streamId, false, ECodecType::svx));
	frame->SetPacketModule(m_InStream.module);

	// Look up stream by ID only (skip IP check)
	auto it = m_Streams.find(m_InStream.streamId);
	if (it != m_Streams.end() && it->second)
	{
		it->second->Push(std::move(frame));
	}
}

void CSvxReflectorProtocol::OnUdpFlush(void)
{
	CloseInStream();
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

	// Find or create client — use configured node callsign
	CClients *clients = g_Reflector.GetClients();
	char module = Header->GetRpt2Module();
	CCallsign cs;
	cs.SetCallsign(ExtractCallsign(m_Callsign), false); // "DL4JC-HS" -> "DL4JC"
	cs.SetCSModule(module);
	std::shared_ptr<CClient> client = clients->FindClient(cs, module, Ip, EProtocol::svxreflector);
	if (client == nullptr)
	{
		clients->AddClient(std::make_shared<CSvxReflectorClient>(cs, Ip, module));
		client = clients->FindClient(cs, module, Ip, EProtocol::svxreflector);
	}

	if (client)
	{
		// Force clear master status from previous stream
		if (client->IsAMaster())
		{
			client->NotAMaster();
		}
		client->Alive();
		client->SetReflectorModule(module);
		if ((stream = g_Reflector.OpenStream(Header, client)) != nullptr)
		{
			m_Streams[stream->GetStreamId()] = stream;
		}
		else
		{
			std::cerr << "SvxReflector: OpenStream failed for module " << module << std::endl;
		}
	}
	else
	{
		std::cerr << "SvxReflector: client not found/created for module " << module << std::endl;
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
			// Check if this stream originated from SVX — don't echo back
			{
				auto stream = g_Reflector.GetStream(module);
				if (stream)
				{
					auto owner = stream->GetOwnerClient();
					if (owner && owner->GetProtocol() == EProtocol::svxreflector)
					{
						m_OutStreamTG.erase(module);
						continue;
					}
				}
			}

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
				BuildUdpHeader(flushBuf, SVX_UDP_MSG_FLUSH_SAMPLES);
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
					if (m_TxGainNum != 256)
					{
						int16_t gained[160];
						for (int i = 0; i < 160; i++)
							gained[i] = (int16_t)((pcm[i] * m_TxGainNum) >> 8);
						EncodeAndSendAudio(gained, tg);
					}
					else
					{
						EncodeAndSendAudio(pcm, tg);
					}
				}
			}
		}
	}
}

void CSvxReflectorProtocol::BuildUdpHeader(CBuffer &buf, uint16_t type)
{
	// V2 UDP header: type(2) + client_id(2) + seq(2)
	uint16_t cid = (uint16_t)(m_ClientId & 0xFFFF);
	uint8_t hdr[6];
	hdr[0] = (type >> 8) & 0xFF;
	hdr[1] = type & 0xFF;
	hdr[2] = (cid >> 8) & 0xFF;
	hdr[3] = cid & 0xFF;
	hdr[4] = (m_UdpSeq >> 8) & 0xFF;
	hdr[5] = m_UdpSeq & 0xFF;
	m_UdpSeq++;
	buf.Set(hdr, 6);
}

void CSvxReflectorProtocol::EncodeAndSendAudio(const int16_t *pcm, uint32_t tg)
{
	// OPUS encode at 16kHz (upsample 8kHz PCM -> 16kHz for SvxReflector)
	int16_t pcm16k[320];
	for (int i = 0; i < 160; i++)
	{
		pcm16k[i * 2] = pcm[i];
		pcm16k[i * 2 + 1] = (i < 159) ? (int16_t)(((int32_t)pcm[i] + pcm[i+1]) / 2) : pcm[i];
	}

	uint8_t opusBuf[512];
	int opusLen = opus_encode(m_OpusEncoder, pcm16k, 320, opusBuf, sizeof(opusBuf));
	if (opusLen <= 0)
	{
		std::cerr << "SvxReflector: opus_encode failed: " << opus_strerror(opusLen) << std::endl;
		return;
	}

	// V2 UDP audio: header(6) + opus_len(2) + opus_data
	CBuffer buf;
	BuildUdpHeader(buf, SVX_UDP_MSG_AUDIO);
	uint8_t lenHdr[2];
	lenHdr[0] = (opusLen >> 8) & 0xFF;
	lenHdr[1] = opusLen & 0xFF;
	buf.Append(lenHdr, 2);
	buf.Append(opusBuf, opusLen);

	Send(buf, m_ServerIp);
}

////////////////////////////////////////////////////////////////////////////////////////
// dynamic TG management (called from admin socket thread)

bool CSvxReflectorProtocol::AddDynamicTG(uint32_t tg, char module, int ttlSeconds)
{
	{
		std::lock_guard<std::mutex> lock(m_TGMutex);
		auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);

		// Check if TG already mapped
		auto tgIt = m_TGToModule.find(tg);
		if (tgIt != m_TGToModule.end())
		{
			// If static, reject
			if (m_DynTGs.find(tg) == m_DynTGs.end())
			{
				std::cerr << "SvxReflector: TG" << tg << " is statically mapped, cannot override" << std::endl;
				return false;
			}
			// Update existing dynamic TTL
			m_DynTGs[tg].expires = expiry;
			std::cout << "SvxReflector: refreshed dynamic TG" << tg << " TTL=" << ttlSeconds << "s" << std::endl;
			return true;
		}

		// Check if module has a primary TG
		auto modIt = m_ModuleToTG.find(module);
		if (modIt != m_ModuleToTG.end())
		{
			// Module has primary — add as secondary (inbound only)
			m_TGToModule[tg] = module;
			m_DynTGs[tg] = { expiry };
			std::cout << "SvxReflector: added secondary TG" << tg << " -> Module " << module
			          << " TTL=" << ttlSeconds << "s (inbound only)" << std::endl;
		}
		else
		{
			// Module is free — add as primary
			m_TGToModule[tg] = module;
			m_ModuleToTG[module] = tg;
			m_DynTGs[tg] = { expiry };
			std::cout << "SvxReflector: added dynamic TG" << tg << " -> Module " << module
			          << " TTL=" << ttlSeconds << "s (primary)" << std::endl;
		}
	}

	// Queue SELECT_TG for the Task thread to send
	{
		std::lock_guard<std::mutex> lock(m_PendingMutex);
		m_PendingSelectTG.push_back(tg);
	}

	return true;
}

bool CSvxReflectorProtocol::RemoveDynamicTG(uint32_t tg)
{
	{
		std::lock_guard<std::mutex> lock(m_TGMutex);

		auto dynIt = m_DynTGs.find(tg);
		if (dynIt == m_DynTGs.end())
		{
			std::cerr << "SvxReflector: TG" << tg << " is not a dynamic entry" << std::endl;
			return false;
		}

		char removedMod = ' ';
		auto tgIt = m_TGToModule.find(tg);
		if (tgIt != m_TGToModule.end())
		{
			removedMod = tgIt->second;
			auto modIt = m_ModuleToTG.find(removedMod);
			if (modIt != m_ModuleToTG.end() && modIt->second == tg)
				m_ModuleToTG.erase(modIt);
			m_TGToModule.erase(tgIt);
			std::cout << "SvxReflector: removed dynamic TG" << tg << " from Module " << removedMod << std::endl;
		}
		m_DynTGs.erase(dynIt);

		// Remove orphaned SVX client if no TGs remain for this module
		if (removedMod != ' ')
		{
			bool hasTGsLeft = false;
			for (const auto &t : m_TGToModule)
				if (t.second == removedMod) { hasTGsLeft = true; break; }
			if (!hasTGsLeft)
			{
				CClients *clients = g_Reflector.GetClients();
				for (auto cit = clients->begin(); cit != clients->end(); ++cit)
				{
					if ((*cit)->GetProtocol() == EProtocol::svxreflector && (*cit)->GetReflectorModule() == removedMod)
					{
						clients->RemoveClient(*cit);
						break;
					}
				}
				g_Reflector.ReleaseClients();
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(m_PendingMutex);
		m_PendingDeselectTG.push_back(tg);
	}

	return true;
}

std::vector<CTGModuleMap::STGInfo> CSvxReflectorProtocol::GetTGMappings(void) const
{
	std::lock_guard<std::mutex> lock(m_TGMutex);
	std::vector<CTGModuleMap::STGInfo> result;
	auto now = std::chrono::steady_clock::now();

	for (const auto &pair : m_TGToModule)
	{
		CTGModuleMap::STGInfo info;
		info.tg = pair.first;
		info.module = pair.second;
		info.timeslot = 0;

		auto dynIt = m_DynTGs.find(pair.first);
		if (dynIt != m_DynTGs.end())
		{
			info.is_static = false;
			info.remainingSeconds = (int)std::chrono::duration_cast<std::chrono::seconds>(dynIt->second.expires - now).count();
		}
		else
		{
			info.is_static = true;
			info.remainingSeconds = -1;
		}

		// Determine if primary: check if m_ModuleToTG[module] == this TG
		auto modIt = m_ModuleToTG.find(pair.second);
		info.is_primary = (modIt != m_ModuleToTG.end() && modIt->second == pair.first);

		result.push_back(info);
	}

	return result;
}
