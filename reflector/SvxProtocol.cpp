// SvxProtocol -- SvxReflector server accepting incoming SvxLink node connections
// Implements SvxReflector protocol V2 with HMAC-SHA1 auth and OPUS audio
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <random>
#include <regex>

#include <cmath>
#include <openssl/hmac.h>

#include "SvxProtocol.h"
#include "SvxClient.h"
#include "Global.h"

// Extract valid amateur radio callsign from SvxLink callsign
static std::string ExtractCallsign(const std::string &svxCallsign)
{
	static const std::regex csRegex("^([A-Z]{1,2}[0-9]+[A-Z]{1,4})");
	std::smatch match;
	if (std::regex_search(svxCallsign, match, csRegex))
		return match[1].str();
	auto dash = svxCallsign.find('-');
	if (dash != std::string::npos && dash >= 3)
		return svxCallsign.substr(0, dash);
	return svxCallsign;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

////////////////////////////////////////////////////////////////////////////////////////
// constructor / destructor

CSvxProtocol::CSvxProtocol()
	: m_TcpListenFd(-1)
	, m_NextClientId(1)
	, m_RxGainNum(256)   // 0 dB default
	, m_TxGainNum(256)   // 0 dB default
{
}

CSvxProtocol::~CSvxProtocol()
{
	Close();
}

////////////////////////////////////////////////////////////////////////////////////////
// serialization helpers (big-endian)

void CSvxProtocol::PackUint16(std::vector<uint8_t> &buf, uint16_t val)
{
	buf.push_back((val >> 8) & 0xFF);
	buf.push_back(val & 0xFF);
}

void CSvxProtocol::PackUint32(std::vector<uint8_t> &buf, uint32_t val)
{
	buf.push_back((val >> 24) & 0xFF);
	buf.push_back((val >> 16) & 0xFF);
	buf.push_back((val >> 8) & 0xFF);
	buf.push_back(val & 0xFF);
}

void CSvxProtocol::PackString(std::vector<uint8_t> &buf, const std::string &str)
{
	PackUint16(buf, (uint16_t)str.size());
	buf.insert(buf.end(), str.begin(), str.end());
}

uint16_t CSvxProtocol::UnpackUint16(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint16_t val = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
	pos += 2;
	return val;
}

uint32_t CSvxProtocol::UnpackUint32(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint32_t val = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16)
	             | ((uint32_t)buf[pos+2] << 8) | buf[pos+3];
	pos += 4;
	return val;
}

std::string CSvxProtocol::UnpackString(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint16_t len = UnpackUint16(buf, pos);
	if (pos + len > buf.size()) { pos = buf.size(); return ""; }
	std::string str((const char *)&buf[pos], len);
	pos += len;
	return str;
}

////////////////////////////////////////////////////////////////////////////////////////
// TG mapping

char CSvxProtocol::TGToModule(uint32_t tg) const
{
	std::lock_guard<std::mutex> lock(m_TGMutex);
	auto it = m_TGToModule.find(tg);
	return (it != m_TGToModule.end()) ? it->second : ' ';
}

uint32_t CSvxProtocol::ModuleToTG(char module) const
{
	std::lock_guard<std::mutex> lock(m_TGMutex);
	auto it = m_ModuleToTG.find(module);
	return (it != m_ModuleToTG.end()) ? it->second : 0;
}

void CSvxProtocol::RefreshActivityByModule(char module)
{
	std::lock_guard<std::mutex> lock(m_TGMutex);
	auto modIt = m_ModuleToTG.find(module);
	if (modIt == m_ModuleToTG.end()) return;
	auto dynIt = m_DynTGs.find(modIt->second);
	if (dynIt != m_DynTGs.end())
	{
		auto newExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(900);
		if (newExpiry > dynIt->second.expires)
			dynIt->second.expires = newExpiry;
	}
}

void CSvxProtocol::LoadTGMap(void)
{
	m_TGToModule.clear();
	m_ModuleToTG.clear();
	const auto &jdata = g_Configure.GetData();
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, 6) == "svxsTG")
		{
			try {
				uint32_t tg = std::stoul(key.substr(6));
				char mod = it.value().get<std::string>()[0];
				m_TGToModule[tg] = mod;
				m_ModuleToTG[mod] = tg;
				std::cout << "SVXServer: TG mapping: TG" << tg << " <-> Module " << mod << std::endl;
			} catch (...) {}
		}
	}
}

void CSvxProtocol::ReloadStaticTGMap(void)
{
	std::lock_guard<std::mutex> lock(m_TGMutex);

	auto savedDynTGs = m_DynTGs;
	std::unordered_map<uint32_t, char> savedDynMappings;
	for (auto &[tg, dynInfo] : m_DynTGs)
	{
		auto it = m_TGToModule.find(tg);
		if (it != m_TGToModule.end())
			savedDynMappings[tg] = it->second;
	}

	m_TGToModule.clear();
	m_ModuleToTG.clear();
	const auto &jdata = g_Configure.GetData();
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, 6) != "svxsTG") continue;
		try {
			uint32_t tg = std::stoul(key.substr(6));
			char mod = it.value().get<std::string>()[0];
			m_TGToModule[tg] = mod;
			m_ModuleToTG[mod] = tg;
		} catch (...) {}
	}

	m_DynTGs.clear();
	for (auto &[tg, dynInfo] : savedDynTGs)
	{
		if (m_TGToModule.find(tg) != m_TGToModule.end()) continue;
		auto mapIt = savedDynMappings.find(tg);
		if (mapIt == savedDynMappings.end()) continue;
		char mod = mapIt->second;
		m_TGToModule[tg] = mod;
		if (m_ModuleToTG.find(mod) == m_ModuleToTG.end())
			m_ModuleToTG[mod] = tg;
		m_DynTGs[tg] = dynInfo;
	}

	std::cout << "SVXServer: TG mappings reloaded (" << m_TGToModule.size() << " total)" << std::endl;
}

std::vector<std::string> CSvxProtocol::GetConnectedNodeList(void) const
{
	std::vector<std::string> nodes;
	for (const auto &[id, peer] : m_Peers)
	{
		if (peer.authState == SSvxPeer::EAuthState::connected)
			nodes.push_back(peer.callsign);
	}
	return nodes;
}

////////////////////////////////////////////////////////////////////////////////////////
// TCP server

bool CSvxProtocol::TcpListen(void)
{
	m_TcpListenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (m_TcpListenFd < 0)
	{
		std::cerr << "SVXServer: socket() failed: " << strerror(errno) << std::endl;
		return false;
	}

	int optval = 1;
	setsockopt(m_TcpListenFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(m_Port);

	if (bind(m_TcpListenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		std::cerr << "SVXServer: bind() port " << m_Port << " failed: " << strerror(errno) << std::endl;
		close(m_TcpListenFd);
		m_TcpListenFd = -1;
		return false;
	}

	if (listen(m_TcpListenFd, 5) < 0)
	{
		std::cerr << "SVXServer: listen() failed: " << strerror(errno) << std::endl;
		close(m_TcpListenFd);
		m_TcpListenFd = -1;
		return false;
	}

	// non-blocking for accept in Task() loop
	fcntl(m_TcpListenFd, F_SETFL, O_NONBLOCK);

	std::cout << "SVXServer: TCP listening on port " << m_Port << std::endl;
	return true;
}

void CSvxProtocol::TcpAccept(void)
{
	struct pollfd pfd = { m_TcpListenFd, POLLIN, 0 };
	if (poll(&pfd, 1, 0) <= 0) return;

	struct sockaddr_in clientAddr{};
	socklen_t addrLen = sizeof(clientAddr);
	int fd = accept(m_TcpListenFd, (struct sockaddr *)&clientAddr, &addrLen);
	if (fd < 0) return;

	// Set non-blocking
	fcntl(fd, F_SETFL, O_NONBLOCK);

	// Assign client ID
	uint32_t clientId = m_NextClientId++;

	SSvxPeer peer;
	peer.tcpFd = fd;
	peer.clientId = clientId;
	peer.authState = SSvxPeer::EAuthState::expect_proto_ver;
	peer.tcpLastRecvTimer.start();
	peer.tcpHeartbeatTimer.start();

	m_Peers[clientId] = std::move(peer);
	m_FdToClientId[fd] = clientId;

	char ipstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
	std::cout << "SVXServer: new TCP connection from " << ipstr << " (clientId=" << clientId << ")" << std::endl;
}

bool CSvxProtocol::TcpSendFrame(int fd, const uint8_t *data, uint32_t len)
{
	if (fd < 0) return false;

	uint8_t hdr[4];
	hdr[0] = (len >> 24) & 0xFF;
	hdr[1] = (len >> 16) & 0xFF;
	hdr[2] = (len >> 8) & 0xFF;
	hdr[3] = len & 0xFF;

	if (send(fd, hdr, 4, MSG_NOSIGNAL) != 4)
		return false;
	if (send(fd, data, len, MSG_NOSIGNAL) != (ssize_t)len)
		return false;

	return true;
}

void CSvxProtocol::TcpBroadcast(const uint8_t *data, uint32_t len, uint32_t exceptClientId)
{
	for (auto &[id, peer] : m_Peers)
	{
		if (id == exceptClientId) continue;
		if (peer.authState != SSvxPeer::EAuthState::connected) continue;
		TcpSendFrame(peer.tcpFd, data, len);
	}
}

void CSvxProtocol::TcpReceiveFromPeer(uint32_t clientId)
{
	auto it = m_Peers.find(clientId);
	if (it == m_Peers.end()) return;
	SSvxPeer &peer = it->second;

	// Read available data into buffer
	uint8_t tmp[4096];
	ssize_t n = recv(peer.tcpFd, tmp, sizeof(tmp), 0);
	if (n == 0)
	{
		// Graceful disconnect
		std::cout << "SVXServer: peer " << peer.callsign << " (clientId=" << clientId << ") disconnected" << std::endl;
		DisconnectPeer(clientId);
		return;
	}
	if (n < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		std::cerr << "SVXServer: recv error from clientId=" << clientId << ": " << strerror(errno) << std::endl;
		DisconnectPeer(clientId);
		return;
	}

	peer.tcpRecvBuf.insert(peer.tcpRecvBuf.end(), tmp, tmp + n);
	peer.tcpLastRecvTimer.start();

	// Process complete frames
	while (peer.tcpRecvBuf.size() >= 4)
	{
		uint32_t frameLen = ((uint32_t)peer.tcpRecvBuf[0] << 24) | ((uint32_t)peer.tcpRecvBuf[1] << 16)
		                  | ((uint32_t)peer.tcpRecvBuf[2] << 8) | peer.tcpRecvBuf[3];

		if (frameLen > 32768)
		{
			std::cerr << "SVXServer: oversized frame from clientId=" << clientId << std::endl;
			DisconnectPeer(clientId);
			return;
		}

		if (peer.tcpRecvBuf.size() < 4 + frameLen)
			break; // incomplete frame

		std::vector<uint8_t> payload(peer.tcpRecvBuf.begin() + 4, peer.tcpRecvBuf.begin() + 4 + frameLen);
		peer.tcpRecvBuf.erase(peer.tcpRecvBuf.begin(), peer.tcpRecvBuf.begin() + 4 + frameLen);

		if (payload.size() < 2) continue;
		uint16_t type = ((uint16_t)payload[0] << 8) | payload[1];

		switch (peer.authState)
		{
		case SSvxPeer::EAuthState::expect_proto_ver:
			if (type == SVX_TCP_MSG_PROTO_VER)
				OnPeerProtoVer(clientId, payload);
			break;
		case SSvxPeer::EAuthState::expect_auth_response:
			if (type == SVX_TCP_MSG_AUTH_RESPONSE)
				OnPeerAuthResponse(clientId, payload);
			break;
		case SSvxPeer::EAuthState::connected:
			switch (type)
			{
			case SVX_TCP_MSG_HEARTBEAT:
				break; // timer already reset
			case SVX_TCP_MSG_NODE_INFO:
				OnPeerNodeInfo(clientId, payload);
				break;
			case SVX_TCP_MSG_SELECT_TG:
				OnPeerSelectTG(clientId, payload);
				break;
			default:
				break;
			}
			break;
		}

		// Re-check if peer still exists (may have been disconnected in handler)
		if (m_Peers.find(clientId) == m_Peers.end())
			return;
	}
}

void CSvxProtocol::DisconnectPeer(uint32_t clientId)
{
	auto it = m_Peers.find(clientId);
	if (it == m_Peers.end()) return;
	SSvxPeer &peer = it->second;

	// Close any open incoming stream
	ClosePeerInStream(clientId);

	// Broadcast NODE_LEFT if was connected
	if (peer.authState == SSvxPeer::EAuthState::connected && !peer.callsign.empty())
	{
		std::vector<uint8_t> msg;
		PackUint16(msg, SVX_TCP_MSG_NODE_LEFT);
		PackString(msg, peer.callsign);
		TcpBroadcast(msg.data(), (uint32_t)msg.size(), clientId);

		// Remove CClient entries for this peer's subscribed modules
		CClients *clients = g_Reflector.GetClients();
		for (uint32_t tg : peer.subscribedTGs)
		{
			char mod = TGToModule(tg);
			if (mod == ' ') continue;
			CCallsign cs;
			cs.SetCallsign(ExtractCallsign(peer.callsign), false);
			cs.SetCSModule(mod);
			auto client = clients->FindClient(cs, mod, peer.udpDiscovered ? peer.udpEndpoint : CIp(), EProtocol::svx);
			if (client)
				clients->RemoveClient(client);
		}
		g_Reflector.ReleaseClients();
	}

	// Cleanup OPUS
	if (peer.opusEncoder) opus_encoder_destroy(peer.opusEncoder);
	if (peer.opusDecoder) opus_decoder_destroy(peer.opusDecoder);

	// Cleanup TCP
	if (peer.tcpFd >= 0)
	{
		m_FdToClientId.erase(peer.tcpFd);
		close(peer.tcpFd);
	}

	std::cout << "SVXServer: peer " << peer.callsign << " (clientId=" << clientId << ") removed" << std::endl;
	m_Peers.erase(it);
}

////////////////////////////////////////////////////////////////////////////////////////
// TCP message handlers

void CSvxProtocol::OnPeerProtoVer(uint32_t clientId, const std::vector<uint8_t> &payload)
{
	auto it = m_Peers.find(clientId);
	if (it == m_Peers.end()) return;
	SSvxPeer &peer = it->second;

	// payload: type(2) + major(2) + minor(2)
	if (payload.size() < 6) { DisconnectPeer(clientId); return; }
	size_t pos = 2;
	uint16_t major = UnpackUint16(payload, pos);
	uint16_t minor = UnpackUint16(payload, pos);

	if (major < 2)
	{
		std::cerr << "SVXServer: client proto version " << major << "." << minor << " too old, need >= 2.0" << std::endl;
		std::vector<uint8_t> err;
		PackUint16(err, SVX_TCP_MSG_ERROR);
		PackString(err, "unsupported protocol version");
		TcpSendFrame(peer.tcpFd, err.data(), (uint32_t)err.size());
		DisconnectPeer(clientId);
		return;
	}

	std::cout << "SVXServer: clientId=" << clientId << " proto version " << major << "." << minor << std::endl;

	// Generate random 20-byte challenge
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint8_t> dist(0, 255);
	for (int i = 0; i < 20; i++)
		peer.challenge[i] = dist(gen);

	// Send MsgAuthChallenge: type(2) + vector_length(2) + nonce(20)
	std::vector<uint8_t> msg;
	PackUint16(msg, SVX_TCP_MSG_AUTH_CHALLENGE);
	PackUint16(msg, 20); // vector<uint8_t> length prefix
	msg.insert(msg.end(), peer.challenge, peer.challenge + 20);
	TcpSendFrame(peer.tcpFd, msg.data(), (uint32_t)msg.size());

	peer.authState = SSvxPeer::EAuthState::expect_auth_response;
}

void CSvxProtocol::OnPeerAuthResponse(uint32_t clientId, const std::vector<uint8_t> &payload)
{
	auto it = m_Peers.find(clientId);
	if (it == m_Peers.end()) return;
	SSvxPeer &peer = it->second;

	// payload: type(2) + callsign(string) + digest_vector_len(2) + digest(20)
	if (payload.size() < 6) { DisconnectPeer(clientId); return; }

	size_t pos = 2;
	std::string callsign = UnpackString(payload, pos);
	if (pos + 22 > payload.size()) { DisconnectPeer(clientId); return; }
	uint16_t digestLen = UnpackUint16(payload, pos);
	if (digestLen != 20 || pos + 20 > payload.size()) { DisconnectPeer(clientId); return; }
	const uint8_t *clientDigest = &payload[pos];

	// Look up password for this callsign (thread-safe)
	// Try exact match first, then extract base callsign
	std::string password;
	{
		std::lock_guard<std::mutex> lock(m_PasswordMutex);
		auto pwIt = m_Passwords.find(callsign);
		if (pwIt == m_Passwords.end())
		{
			std::string baseCs = ExtractCallsign(callsign);
			pwIt = m_Passwords.find(baseCs);
		}
		if (pwIt != m_Passwords.end())
			password = pwIt->second;
	}
	if (password.empty())
	{
		std::cerr << "SVXServer: unknown callsign '" << callsign << "'" << std::endl;
		std::vector<uint8_t> err;
		PackUint16(err, SVX_TCP_MSG_ERROR);
		PackString(err, "unknown callsign");
		TcpSendFrame(peer.tcpFd, err.data(), (uint32_t)err.size());
		DisconnectPeer(clientId);
		return;
	}

	// Verify HMAC-SHA1(password, challenge)
	unsigned int hmacLen = 20;
	uint8_t expectedDigest[20];
	HMAC(EVP_sha1(),
	     password.c_str(), (int)password.size(),
	     peer.challenge, 20,
	     expectedDigest, &hmacLen);

	if (memcmp(clientDigest, expectedDigest, 20) != 0)
	{
		std::cerr << "SVXServer: auth failed for '" << callsign << "'" << std::endl;
		std::vector<uint8_t> err;
		PackUint16(err, SVX_TCP_MSG_ERROR);
		PackString(err, "authentication failed");
		TcpSendFrame(peer.tcpFd, err.data(), (uint32_t)err.size());
		DisconnectPeer(clientId);
		return;
	}

	// Check GateKeeper
	CCallsign cs;
	cs.SetCallsign(ExtractCallsign(callsign), false);
	// Get peer IP from socket
	struct sockaddr_in sa{};
	socklen_t saLen = sizeof(sa);
	getpeername(peer.tcpFd, (struct sockaddr *)&sa, &saLen);
	char ipstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &sa.sin_addr, ipstr, sizeof(ipstr));
	CIp peerIp(AF_INET, 0, ipstr);

	if (!g_GateKeeper.MayLink(cs, peerIp, EProtocol::svx))
	{
		std::cerr << "SVXServer: access denied for '" << callsign << "' by GateKeeper" << std::endl;
		std::vector<uint8_t> err;
		PackUint16(err, SVX_TCP_MSG_ERROR);
		PackString(err, "access denied");
		TcpSendFrame(peer.tcpFd, err.data(), (uint32_t)err.size());
		DisconnectPeer(clientId);
		return;
	}

	// Auth successful
	peer.callsign = callsign;
	peer.authState = SSvxPeer::EAuthState::connected;

	// Create OPUS encoder/decoder for this peer
	int err;
	peer.opusEncoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_AUDIO, &err);
	peer.opusDecoder = opus_decoder_create(8000, 1, &err);

	// Send MsgAuthOk
	{
		std::vector<uint8_t> msg;
		PackUint16(msg, SVX_TCP_MSG_AUTH_OK);
		TcpSendFrame(peer.tcpFd, msg.data(), (uint32_t)msg.size());
	}

	// Send MsgServerInfo: type(2) + client_id(4) + codecs(vector<string>) + nodes(vector<string>) + reserved(2)
	{
		std::vector<uint8_t> msg;
		PackUint16(msg, SVX_TCP_MSG_SERVER_INFO);
		PackUint32(msg, clientId);

		// nodes: vector<string> (must come BEFORE codecs — SvxLink field order)
		auto nodes = GetConnectedNodeList();
		PackUint16(msg, (uint16_t)nodes.size());
		for (const auto &n : nodes) PackString(msg, n);

		// codecs: vector<string>
		std::vector<std::string> codecs = {"OPUS"};
		PackUint16(msg, (uint16_t)codecs.size());
		for (const auto &c : codecs) PackString(msg, c);

		// reserved
		PackUint16(msg, 0);

		TcpSendFrame(peer.tcpFd, msg.data(), (uint32_t)msg.size());
	}

	// Broadcast NODE_JOINED to all other peers
	{
		std::vector<uint8_t> msg;
		PackUint16(msg, SVX_TCP_MSG_NODE_JOINED);
		PackString(msg, callsign);
		TcpBroadcast(msg.data(), (uint32_t)msg.size(), clientId);
	}

	std::cout << "SVXServer: '" << callsign << "' authenticated (clientId=" << clientId << ")" << std::endl;
}

void CSvxProtocol::OnPeerNodeInfo(uint32_t clientId, const std::vector<uint8_t> &payload)
{
	auto it = m_Peers.find(clientId);
	if (it == m_Peers.end()) return;

	// payload: type(2) + json(string)
	if (payload.size() < 4) return;
	size_t pos = 2;
	std::string json = UnpackString(payload, pos);
	std::cout << "SVXServer: node info from " << it->second.callsign << ": " << json << std::endl;
}

void CSvxProtocol::OnPeerSelectTG(uint32_t clientId, const std::vector<uint8_t> &payload)
{
	auto it = m_Peers.find(clientId);
	if (it == m_Peers.end()) return;
	SSvxPeer &peer = it->second;

	// payload: type(2) + tg(4)
	if (payload.size() < 6) return;
	size_t pos = 2;
	uint32_t tg = UnpackUint32(payload, pos);

	if (tg == 0)
	{
		// Unsubscribe all TGs
		CClients *clients = g_Reflector.GetClients();
		for (uint32_t oldTg : peer.subscribedTGs)
		{
			char mod = TGToModule(oldTg);
			if (mod == ' ') continue;
			CCallsign cs;
			cs.SetCallsign(ExtractCallsign(peer.callsign), false);
			cs.SetCSModule(mod);
			auto client = clients->FindClient(cs, mod, peer.udpDiscovered ? peer.udpEndpoint : CIp(), EProtocol::svx);
			if (client)
				clients->RemoveClient(client);
		}
		g_Reflector.ReleaseClients();
		peer.subscribedTGs.clear();
		std::cout << "SVXServer: " << peer.callsign << " unsubscribed all TGs" << std::endl;
		return;
	}

	char module = TGToModule(tg);
	if (module == ' ')
	{
		std::cout << "SVXServer: " << peer.callsign << " requested unknown TG" << tg << std::endl;
		return;
	}

	peer.subscribedTGs.insert(tg);

	// Create/update CClient for this module
	CClients *clients = g_Reflector.GetClients();
	CCallsign cs;
	cs.SetCallsign(ExtractCallsign(peer.callsign), false);
	cs.SetCSModule(module);
	CIp ip = peer.udpDiscovered ? peer.udpEndpoint : CIp();
	auto client = clients->FindClient(cs, module, ip, EProtocol::svx);
	if (!client)
	{
		clients->AddClient(std::make_shared<CSvxClient>(cs, ip, module));
		std::cout << "SVXServer: created client for " << peer.callsign << " on module " << module << std::endl;
	}
	g_Reflector.ReleaseClients();

	std::cout << "SVXServer: " << peer.callsign << " subscribed TG" << tg << " -> Module " << module << std::endl;
}

////////////////////////////////////////////////////////////////////////////////////////
// UDP handling

void CSvxProtocol::OnUdpPacket(const CBuffer &buffer, const CIp &ip)
{
	if (buffer.size() < 6) return;

	uint16_t type = ((uint16_t)buffer.data()[0] << 8) | buffer.data()[1];
	uint16_t peerClientId = ((uint16_t)buffer.data()[2] << 8) | buffer.data()[3];

	// Find peer by clientId
	auto it = m_Peers.find((uint32_t)peerClientId);
	if (it == m_Peers.end()) return;
	SSvxPeer &peer = it->second;

	// Only accept UDP from authenticated peers
	if (peer.authState != SSvxPeer::EAuthState::connected) return;

	// UDP endpoint discovery
	if (!peer.udpDiscovered)
	{
		peer.udpEndpoint = ip;
		peer.udpDiscovered = true;
		std::cout << "SVXServer: discovered UDP endpoint for " << peer.callsign << std::endl;

		// Update CClient IP for already-subscribed modules
		CClients *clients = g_Reflector.GetClients();
		for (uint32_t tg : peer.subscribedTGs)
		{
			char mod = TGToModule(tg);
			if (mod == ' ') continue;
			CCallsign cs;
			cs.SetCallsign(ExtractCallsign(peer.callsign), false);
			cs.SetCSModule(mod);
			// Remove old (no-IP) client and re-add with correct IP
			auto client = clients->FindClient(cs, mod, CIp(), EProtocol::svx);
			if (client)
				clients->RemoveClient(client);
			clients->AddClient(std::make_shared<CSvxClient>(cs, ip, mod));
		}
		g_Reflector.ReleaseClients();
	}

	peer.udpLastRecvTimer.start();

	switch (type)
	{
	case SVX_UDP_MSG_HEARTBEAT:
		// Send heartbeat reply with client's own ID so it recognizes the packet
		{
			CBuffer hb;
			BuildUdpHeader(hb, SVX_UDP_MSG_HEARTBEAT, (uint16_t)peer.clientId, peer.udpSeqSend);
			Send(hb, peer.udpEndpoint);
		}
		break;
	case SVX_UDP_MSG_AUDIO:
		OnPeerUdpAudio(peer.clientId, buffer);
		break;
	case SVX_UDP_MSG_FLUSH_SAMPLES:
		OnPeerUdpFlush(peer.clientId);
		break;
	case SVX_UDP_MSG_ALL_FLUSHED:
		break;
	default:
		break;
	}
}

void CSvxProtocol::BuildUdpHeader(CBuffer &buf, uint16_t type, uint16_t clientId, uint16_t &seq)
{
	uint8_t hdr[6];
	hdr[0] = (type >> 8) & 0xFF;
	hdr[1] = type & 0xFF;
	hdr[2] = (clientId >> 8) & 0xFF;
	hdr[3] = clientId & 0xFF;
	hdr[4] = (seq >> 8) & 0xFF;
	hdr[5] = seq & 0xFF;
	seq++;
	buf.Set(hdr, 6);
}

void CSvxProtocol::OnPeerUdpAudio(uint32_t clientId, const CBuffer &buffer)
{
	auto it = m_Peers.find(clientId);
	if (it == m_Peers.end()) return;
	SSvxPeer &peer = it->second;

	// Find which TG this peer is talking on (use first subscribed TG with a module)
	uint32_t tg = 0;
	char module = ' ';
	for (uint32_t subscribedTg : peer.subscribedTGs)
	{
		char mod = TGToModule(subscribedTg);
		if (mod != ' ')
		{
			tg = subscribedTg;
			module = mod;
			break;
		}
	}
	if (module == ' ') return;

	// V2 UDP audio: type(2) + client_id(2) + seq(2) + opus_len(2) + opus_data
	if (buffer.size() < 9) return;
	uint16_t opusLen = ((uint16_t)buffer.data()[6] << 8) | buffer.data()[7];
	if ((size_t)(8 + opusLen) > buffer.size()) return;

	if (!peer.opusDecoder) return;

	// Decode OPUS to 8kHz PCM
	int16_t pcm[160];
	int samples = opus_decode(peer.opusDecoder, buffer.data() + 8, opusLen, pcm, 160, 0);
	if (samples <= 0) return;
	for (int i = samples; i < 160; i++) pcm[i] = 0;

	// Apply RX gain
	if (m_RxGainNum != 256)
	{
		for (int i = 0; i < 160; i++)
			pcm[i] = (int16_t)((pcm[i] * m_RxGainNum) >> 8);
	}

	// Refresh dynamic TG TTL
	RefreshActivityByModule(module);

	// Open stream on first audio frame
	if (!peer.inStream.open || peer.inStream.module != module)
	{
		if (peer.inStream.open)
			ClosePeerInStream(clientId);

		static uint8_t counter = 0;
		peer.inStream.streamId = 0x5D00 | (counter++ & 0xFF);
		peer.inStream.tg = tg;
		peer.inStream.module = module;

		std::string userCs = ExtractCallsign(peer.callsign);
		CCallsign my;
		my.SetCallsign(userCs, true);
		CCallsign rpt1(g_Reflector.GetCallsign());
		rpt1.SetCSModule(module);
		CCallsign rpt2 = m_ReflectorCallsign;
		rpt2.SetCSModule(module);
		auto header = std::unique_ptr<CDvHeaderPacket>(
			new CDvHeaderPacket(my, CCallsign("CQCQCQ"), rpt1, rpt2, peer.inStream.streamId, true));

		CIp headerIp = peer.udpDiscovered ? peer.udpEndpoint : CIp();
		OnDvHeaderPacketIn(header, headerIp);
		peer.inStream.open = true;

		auto stream = g_Reflector.GetStream(module);
		if (stream) stream->SetSourceTG(tg);

		// Notify subscribed peers about talker start
		std::vector<uint8_t> msg;
		PackUint16(msg, SVX_TCP_MSG_TALKER_START);
		PackUint32(msg, tg);
		PackString(msg, peer.callsign);
		TcpBroadcast(msg.data(), (uint32_t)msg.size(), clientId);

		std::cout << "SVXServer: stream opened for " << peer.callsign << " on TG" << tg << " Module " << module << std::endl;
	}

	memcpy(peer.inStream.lastPcm, pcm, sizeof(peer.inStream.lastPcm));

	auto frame = std::unique_ptr<CDvFramePacket>(
		new CDvFramePacket(pcm, peer.inStream.streamId, false, ECodecType::svx));
	frame->SetPacketModule(module);

	auto sit = m_Streams.find(peer.inStream.streamId);
	if (sit != m_Streams.end() && sit->second)
		sit->second->Push(std::move(frame));
}

void CSvxProtocol::OnPeerUdpFlush(uint32_t clientId)
{
	ClosePeerInStream(clientId);

	// Send ALL_FLUSHED back to the peer
	auto it = m_Peers.find(clientId);
	if (it != m_Peers.end() && it->second.udpDiscovered)
	{
		SSvxPeer &peer = it->second;
		CBuffer buf;
		BuildUdpHeader(buf, SVX_UDP_MSG_ALL_FLUSHED, (uint16_t)peer.clientId, peer.udpSeqSend);
		Send(buf, peer.udpEndpoint);
	}
}

void CSvxProtocol::ClosePeerInStream(uint32_t clientId)
{
	auto it = m_Peers.find(clientId);
	if (it == m_Peers.end()) return;
	SSvxPeer &peer = it->second;

	if (!peer.inStream.open) return;

	auto sit = m_Streams.find(peer.inStream.streamId);
	if (sit != m_Streams.end() && sit->second)
	{
		// Flush OPUS decoder
		if (peer.opusDecoder)
		{
			int16_t dummy[160];
			[[maybe_unused]] int ret = opus_decode(peer.opusDecoder, NULL, 0, dummy, 160, 0);
		}

		// Close with silence
		int16_t silence[160] = {};
		auto lastFrame = std::unique_ptr<CDvFramePacket>(
			new CDvFramePacket(silence, peer.inStream.streamId, true, ECodecType::svx));
		lastFrame->SetPacketModule(peer.inStream.module);
		sit->second->Push(std::move(lastFrame));
		g_Reflector.CloseStream(sit->second);
		m_Streams.erase(sit);
	}

	// Notify peers about talker stop
	std::vector<uint8_t> msg;
	PackUint16(msg, SVX_TCP_MSG_TALKER_STOP);
	PackUint32(msg, peer.inStream.tg);
	PackString(msg, peer.callsign);
	TcpBroadcast(msg.data(), (uint32_t)msg.size(), clientId);

	peer.inStream.open = false;
	peer.inStream.streamId = 0;
	peer.inStream.tg = 0;
	peer.inStream.module = ' ';
}

////////////////////////////////////////////////////////////////////////////////////////
// stream helpers

void CSvxProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
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
	CCallsign cs;
	cs.SetCallsign(ExtractCallsign(my.GetCS()), false);
	cs.SetCSModule(module);
	std::shared_ptr<CClient> client = clients->FindClient(cs, module, Ip, EProtocol::svx);
	if (!client)
	{
		clients->AddClient(std::make_shared<CSvxClient>(cs, Ip, module));
		client = clients->FindClient(cs, module, Ip, EProtocol::svx);
	}

	if (client)
	{
		if (client->IsAMaster())
			client->NotAMaster();
		client->Alive();
		client->SetReflectorModule(module);
		if ((stream = g_Reflector.OpenStream(Header, client)) != nullptr)
			m_Streams[stream->GetStreamId()] = stream;
	}

	g_Reflector.ReleaseClients();
	g_Reflector.GetUsers()->Hearing(my, rpt1, rpt2, "SVX");
	g_Reflector.ReleaseUsers();
}

////////////////////////////////////////////////////////////////////////////////////////
// outgoing audio path: urfd streams -> SVX peers

void CSvxProtocol::HandleQueue(void)
{
	while (!m_Queue.IsEmpty())
	{
		auto packet = m_Queue.Pop();
		const char module = packet->GetPacketModule();

		if (packet->IsDvHeader())
		{
			// Don't echo back to SVX peers
			{
				auto stream = g_Reflector.GetStream(module);
				if (stream)
				{
					auto owner = stream->GetOwnerClient();
					if (owner && owner->GetProtocol() == EProtocol::svx)
					{
						m_OutStreamTG.erase(module);
						continue;
					}
				}
			}

			uint32_t tg = ModuleToTG(module);
			if (tg != 0)
			{
				m_OutStreamTG[module] = tg;

				// Get talker callsign from stream (base callsign without module)
				std::string talkerCs;
				auto stream = g_Reflector.GetStream(module);
				if (stream)
				{
					auto owner = stream->GetOwnerClient();
					if (owner)
						talkerCs = owner->GetCallsign().GetBase();
				}

				// Send TALKER_START to subscribed peers
				std::vector<uint8_t> msg;
				PackUint16(msg, SVX_TCP_MSG_TALKER_START);
				PackUint32(msg, tg);
				PackString(msg, talkerCs);

				for (auto &[id, peer] : m_Peers)
				{
					if (peer.authState != SSvxPeer::EAuthState::connected) continue;
					if (peer.subscribedTGs.count(tg) == 0) continue;
					TcpSendFrame(peer.tcpFd, msg.data(), (uint32_t)msg.size());
				}
			}
		}
		else if (packet->IsDvFrame())
		{
			auto tgIt = m_OutStreamTG.find(module);
			if (tgIt == m_OutStreamTG.end()) continue;
			uint32_t tg = tgIt->second;

			const CDvFramePacket &frame = (const CDvFramePacket &)*packet;

			if (packet->IsLastPacket())
			{
				// Send flush + TALKER_STOP to subscribed peers
				for (auto &[id, peer] : m_Peers)
				{
					if (peer.authState != SSvxPeer::EAuthState::connected) continue;
					if (peer.subscribedTGs.count(tg) == 0) continue;
					if (!peer.udpDiscovered) continue;
					SendFlushToPeer(peer);
				}

				// Get talker callsign for stop notification
				std::string stopCs;
				{
					auto stream2 = g_Reflector.GetStream(module);
					if (stream2)
					{
						auto owner2 = stream2->GetOwnerClient();
						if (owner2)
							stopCs = owner2->GetCallsign().GetBase();
					}
				}
				std::vector<uint8_t> msg;
				PackUint16(msg, SVX_TCP_MSG_TALKER_STOP);
				PackUint32(msg, tg);
				PackString(msg, stopCs);
				for (auto &[id, peer] : m_Peers)
				{
					if (peer.authState != SSvxPeer::EAuthState::connected) continue;
					if (peer.subscribedTGs.count(tg) == 0) continue;
					TcpSendFrame(peer.tcpFd, msg.data(), (uint32_t)msg.size());
				}

				m_OutStreamTG.erase(tgIt);
			}
			else
			{
				const int16_t *pcm = (const int16_t *)frame.GetCodecData(ECodecType::usrp);
				if (!pcm) continue;

				int16_t gained[160];
				const int16_t *sendPcm = pcm;
				if (m_TxGainNum != 256)
				{
					for (int i = 0; i < 160; i++)
						gained[i] = (int16_t)((pcm[i] * m_TxGainNum) >> 8);
					sendPcm = gained;
				}

				for (auto &[id, peer] : m_Peers)
				{
					if (peer.authState != SSvxPeer::EAuthState::connected) continue;
					if (peer.subscribedTGs.count(tg) == 0) continue;
					if (!peer.udpDiscovered) continue;
					SendAudioToPeer(peer, sendPcm, tg);
				}
			}
		}
	}
}

void CSvxProtocol::SendAudioToPeer(SSvxPeer &peer, const int16_t *pcm, uint32_t tg)
{
	if (!peer.opusEncoder) return;

	// Upsample 8kHz -> 16kHz
	int16_t pcm16k[320];
	for (int i = 0; i < 160; i++)
	{
		pcm16k[i * 2] = pcm[i];
		pcm16k[i * 2 + 1] = (i < 159) ? (int16_t)(((int32_t)pcm[i] + pcm[i+1]) / 2) : pcm[i];
	}

	uint8_t opusBuf[512];
	int opusLen = opus_encode(peer.opusEncoder, pcm16k, 320, opusBuf, sizeof(opusBuf));
	if (opusLen <= 0) return;

	CBuffer buf;
	// Send with the peer's client_id so the client recognizes the packet
	BuildUdpHeader(buf, SVX_UDP_MSG_AUDIO, (uint16_t)peer.clientId, peer.udpSeqSend);
	uint8_t lenHdr[2];
	lenHdr[0] = (opusLen >> 8) & 0xFF;
	lenHdr[1] = opusLen & 0xFF;
	buf.Append(lenHdr, 2);
	buf.Append(opusBuf, opusLen);

	Send(buf, peer.udpEndpoint);
}

void CSvxProtocol::SendFlushToPeer(SSvxPeer &peer)
{
	CBuffer buf;
	BuildUdpHeader(buf, SVX_UDP_MSG_FLUSH_SAMPLES, (uint16_t)peer.clientId, peer.udpSeqSend);
	Send(buf, peer.udpEndpoint);
}

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CSvxProtocol::Initialize(const char *type, const EProtocol ptype,
	const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	// Load per-user passwords from svxsUser_<CALLSIGN> keys
	{
		const auto &jdata = g_Configure.GetData();
		for (auto it = jdata.begin(); it != jdata.end(); ++it)
		{
			const std::string &key = it.key();
			if (key.substr(0, 9) == "svxsUser_")
			{
				std::string callsign = key.substr(9);
				m_Passwords[callsign] = it.value().get<std::string>();
				std::cout << "SVXServer: password configured for " << callsign << std::endl;
			}
		}
	}
	if (m_Passwords.empty())
	{
		std::cerr << "SVXServer: no users configured in [SVXServer] section" << std::endl;
		return false;
	}

	// Parse BlockProtocols
	if (g_Configure.Contains(g_Keys.svxs.blockprotocols))
	{
		const std::map<std::string, EProtocol> protoMap = {
			{"MMDVMClient", EProtocol::mmdvmclient}, {"SvxReflector", EProtocol::svxreflector},
			{"DExtra", EProtocol::dextra}, {"DPlus", EProtocol::dplus},
			{"DCS", EProtocol::dcs}, {"DMRPlus", EProtocol::dmrplus},
			{"MMDVM", EProtocol::dmrmmdvm}, {"YSF", EProtocol::ysf},
			{"M17", EProtocol::m17}, {"NXDN", EProtocol::nxdn},
			{"P25", EProtocol::p25}, {"USRP", EProtocol::usrp},
			{"URF", EProtocol::urf}, {"XLXPeer", EProtocol::xlxpeer}, {"G3", EProtocol::g3},
		};
		std::istringstream ss(g_Configure.GetString(g_Keys.svxs.blockprotocols));
		std::string token;
		while (std::getline(ss, token, ','))
		{
			token.erase(0, token.find_first_not_of(" \t"));
			token.erase(token.find_last_not_of(" \t") + 1);
			auto it = protoMap.find(token);
			if (it != protoMap.end())
			{
				m_BlockedSources.insert(it->second);
				std::cout << "SVXServer: blocking protocol " << token << std::endl;
			}
		}
	}
	SaveBlockDefaults();

	// Parse gain settings
	if (g_Configure.Contains(g_Keys.svxs.rxgain))
	{
		int db = std::stoi(g_Configure.GetString(g_Keys.svxs.rxgain));
		if (db < -40) db = -40; else if (db > 40) db = 40;
		m_RxGainNum = (int32_t)roundf(256.0f * powf(10.0f, (float)db / 20.0f));
		std::cout << "SVXServer: RxGain = " << db << " dB" << std::endl;
	}
	if (g_Configure.Contains(g_Keys.svxs.txgain))
	{
		int db = std::stoi(g_Configure.GetString(g_Keys.svxs.txgain));
		if (db < -40) db = -40; else if (db > 40) db = 40;
		m_TxGainNum = (int32_t)roundf(256.0f * powf(10.0f, (float)db / 20.0f));
		std::cout << "SVXServer: TxGain = " << db << " dB" << std::endl;
	}

	LoadTGMap();

	// Initialize base protocol (opens UDP socket on port)
	if (!CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	// Open TCP listen socket on same port
	if (!TcpListen())
		return false;

	std::cout << "SVXServer: initialized on port " << m_Port << " with " << m_Passwords.size() << " user(s)" << std::endl;
	return true;
}

void CSvxProtocol::Close(void)
{
	// Disconnect all peers
	std::vector<uint32_t> peerIds;
	for (const auto &[id, peer] : m_Peers)
		peerIds.push_back(id);
	for (uint32_t id : peerIds)
		DisconnectPeer(id);

	if (m_TcpListenFd >= 0)
	{
		close(m_TcpListenFd);
		m_TcpListenFd = -1;
	}

	CProtocol::Close();
}

////////////////////////////////////////////////////////////////////////////////////////
// Task loop

void CSvxProtocol::Task(void)
{
	// 1. Accept new TCP connections
	TcpAccept();

	// 2. Receive TCP data from all peers
	{
		std::vector<struct pollfd> pfds;
		std::vector<uint32_t> clientIds;
		for (const auto &[id, peer] : m_Peers)
		{
			if (peer.tcpFd >= 0)
			{
				pfds.push_back({peer.tcpFd, POLLIN, 0});
				clientIds.push_back(id);
			}
		}
		if (!pfds.empty())
		{
			int ret = poll(pfds.data(), pfds.size(), 0);
			if (ret > 0)
			{
				for (size_t i = 0; i < pfds.size(); i++)
				{
					if (pfds[i].revents & (POLLIN | POLLERR | POLLHUP))
						TcpReceiveFromPeer(clientIds[i]);
				}
			}
		}
	}

	// 3. Receive UDP packets
	{
		CBuffer buffer;
		CIp ip;
		if (Receive4(buffer, ip, 20))
			OnUdpPacket(buffer, ip);
	}

	// 4. Keepalives: send TCP heartbeats, check timeouts
	{
		std::vector<uint32_t> deadPeers;
		for (auto &[id, peer] : m_Peers)
		{
			if (peer.authState == SSvxPeer::EAuthState::connected)
			{
				// TCP heartbeat send
				if (peer.tcpHeartbeatTimer.time() >= SVXS_TCP_KEEPALIVE_PERIOD)
				{
					std::vector<uint8_t> hb;
					PackUint16(hb, SVX_TCP_MSG_HEARTBEAT);
					TcpSendFrame(peer.tcpFd, hb.data(), (uint32_t)hb.size());
					peer.tcpHeartbeatTimer.start();
				}

				// TCP timeout
				if (peer.tcpLastRecvTimer.time() >= SVXS_TCP_KEEPALIVE_TIMEOUT)
				{
					std::cerr << "SVXServer: TCP timeout for " << peer.callsign << std::endl;
					deadPeers.push_back(id);
					continue;
				}

				// UDP timeout (only if endpoint was discovered)
				if (peer.udpDiscovered && peer.udpLastRecvTimer.time() >= SVXS_UDP_KEEPALIVE_TIMEOUT)
				{
					std::cerr << "SVXServer: UDP timeout for " << peer.callsign << std::endl;
					deadPeers.push_back(id);
					continue;
				}
			}
			else
			{
				// Auth timeout — 30 seconds to complete handshake
				if (peer.tcpLastRecvTimer.time() >= 30)
				{
					std::cerr << "SVXServer: auth timeout for clientId=" << id << std::endl;
					deadPeers.push_back(id);
				}
			}
		}
		for (uint32_t id : deadPeers)
			DisconnectPeer(id);
	}

	// 5. Handle outbound audio queue
	HandleQueue();

	// 6. Check stream timeouts
	CheckStreamsTimeout();

	// 7. Expire dynamic TGs
	{
		auto now = std::chrono::steady_clock::now();
		std::lock_guard<std::mutex> lock(m_TGMutex);
		for (auto dynIt = m_DynTGs.begin(); dynIt != m_DynTGs.end(); )
		{
			if (now >= dynIt->second.expires)
			{
				uint32_t tg = dynIt->first;
				char mod = ' ';
				auto tgIt = m_TGToModule.find(tg);
				if (tgIt != m_TGToModule.end())
				{
					mod = tgIt->second;
					auto modIt = m_ModuleToTG.find(mod);
					if (modIt != m_ModuleToTG.end() && modIt->second == tg)
						m_ModuleToTG.erase(modIt);
					m_TGToModule.erase(tgIt);
				}
				std::cout << "SVXServer: dynamic TG" << tg << " expired (Module " << mod << ")" << std::endl;
				dynIt = m_DynTGs.erase(dynIt);
			}
			else
			{
				++dynIt;
			}
		}
	}
}

void CSvxProtocol::HandleKeepalives(void)
{
	// handled in Task() directly
}

////////////////////////////////////////////////////////////////////////////////////////
// dynamic TG management (called from admin socket thread)

bool CSvxProtocol::AddDynamicTG(uint32_t tg, char module, int ttlSeconds)
{
	std::lock_guard<std::mutex> lock(m_TGMutex);
	auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);

	auto tgIt = m_TGToModule.find(tg);
	if (tgIt != m_TGToModule.end())
	{
		if (m_DynTGs.find(tg) == m_DynTGs.end())
		{
			std::cerr << "SVXServer: TG" << tg << " is statically mapped, cannot override" << std::endl;
			return false;
		}
		m_DynTGs[tg].expires = expiry;
		return true;
	}

	auto modIt = m_ModuleToTG.find(module);
	if (modIt != m_ModuleToTG.end())
	{
		// Module has primary — add as secondary (inbound only)
		m_TGToModule[tg] = module;
		m_DynTGs[tg] = { expiry };
		std::cout << "SVXServer: added secondary TG" << tg << " -> Module " << module << std::endl;
	}
	else
	{
		// Module is free — add as primary
		m_TGToModule[tg] = module;
		m_ModuleToTG[module] = tg;
		m_DynTGs[tg] = { expiry };
		std::cout << "SVXServer: added dynamic TG" << tg << " -> Module " << module << std::endl;
	}

	return true;
}

bool CSvxProtocol::RemoveDynamicTG(uint32_t tg)
{
	std::lock_guard<std::mutex> lock(m_TGMutex);

	auto dynIt = m_DynTGs.find(tg);
	if (dynIt == m_DynTGs.end())
		return false;

	char removedMod = ' ';
	auto tgIt = m_TGToModule.find(tg);
	if (tgIt != m_TGToModule.end())
	{
		removedMod = tgIt->second;
		auto modIt = m_ModuleToTG.find(removedMod);
		if (modIt != m_ModuleToTG.end() && modIt->second == tg)
			m_ModuleToTG.erase(modIt);
		m_TGToModule.erase(tgIt);
	}
	m_DynTGs.erase(dynIt);

	// Remove orphaned SVX clients if no TGs remain for this module
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
				if ((*cit)->GetProtocol() == EProtocol::svx && (*cit)->GetReflectorModule() == removedMod)
				{
					clients->RemoveClient(*cit);
					break;
				}
			}
			g_Reflector.ReleaseClients();
		}
	}

	return true;
}

std::vector<CTGModuleMap::STGInfo> CSvxProtocol::GetTGMappings(void) const
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

		auto modIt = m_ModuleToTG.find(pair.second);
		info.is_primary = (modIt != m_ModuleToTG.end() && modIt->second == pair.first);

		result.push_back(info);
	}

	return result;
}

////////////////////////////////////////////////////////////////////////////////////////
// user management (called from admin socket thread)

bool CSvxProtocol::AddUser(const std::string &callsign, const std::string &password)
{
	{
		std::lock_guard<std::mutex> lock(m_PasswordMutex);
		m_Passwords[callsign] = password;
	}
	IniAddUser(callsign, password);
	std::cout << "SVXServer: user '" << callsign << "' added" << std::endl;
	return true;
}

bool CSvxProtocol::RemoveUser(const std::string &callsign)
{
	{
		std::lock_guard<std::mutex> lock(m_PasswordMutex);
		auto it = m_Passwords.find(callsign);
		if (it == m_Passwords.end())
			return false;
		m_Passwords.erase(it);
	}
	IniRemoveUser(callsign);
	std::cout << "SVXServer: user '" << callsign << "' removed" << std::endl;
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// INI persistence

bool CSvxProtocol::IniAddUser(const std::string &callsign, const std::string &password)
{
	const std::string &path = g_Reflector.GetConfigPath();
	if (path.empty()) return false;

	// Read INI file
	std::ifstream in(path);
	if (!in.is_open()) return false;
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(in, line))
		lines.push_back(line);
	in.close();

	// Find [SVXServer] section and insert/update user line
	bool inSection = false;
	int insertPos = -1;
	for (size_t i = 0; i < lines.size(); i++)
	{
		std::string trimmed = lines[i];
		// trim leading whitespace
		size_t start = trimmed.find_first_not_of(" \t");
		if (start != std::string::npos) trimmed = trimmed.substr(start);

		if (!trimmed.empty() && trimmed[0] == '[')
		{
			if (inSection) { insertPos = (int)i; break; } // next section
			if (trimmed.find("[SVXServer]") == 0) inSection = true;
			continue;
		}
		if (!inSection) continue;

		// Check if this callsign already exists (key = value)
		auto eq = trimmed.find('=');
		if (eq != std::string::npos)
		{
			std::string key = trimmed.substr(0, eq);
			// trim trailing whitespace from key
			size_t end = key.find_last_not_of(" \t");
			if (end != std::string::npos) key = key.substr(0, end + 1);
			if (key == callsign)
			{
				// Update existing line
				lines[i] = callsign + " = " + password;
				// Write back
				std::ofstream out(path);
				if (!out.is_open()) return false;
				for (const auto &l : lines) out << l << "\n";
				return true;
			}
		}
		if (eq != std::string::npos)
			insertPos = (int)i + 1; // track last key=value line in section
	}

	// Not found — insert new line after last key=value in section
	if (insertPos < 0) insertPos = (int)lines.size();
	lines.insert(lines.begin() + insertPos, callsign + " = " + password);

	std::ofstream out(path);
	if (!out.is_open()) return false;
	for (const auto &l : lines) out << l << "\n";
	return true;
}

bool CSvxProtocol::IniRemoveUser(const std::string &callsign)
{
	const std::string &path = g_Reflector.GetConfigPath();
	if (path.empty()) return false;

	std::ifstream in(path);
	if (!in.is_open()) return false;
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(in, line))
		lines.push_back(line);
	in.close();

	bool inSection = false;
	bool found = false;
	for (auto it = lines.begin(); it != lines.end(); )
	{
		std::string trimmed = *it;
		size_t start = trimmed.find_first_not_of(" \t");
		if (start != std::string::npos) trimmed = trimmed.substr(start);

		if (!trimmed.empty() && trimmed[0] == '[')
		{
			if (inSection) break; // left section
			if (trimmed.find("[SVXServer]") == 0) inSection = true;
			++it;
			continue;
		}
		if (inSection && !trimmed.empty() && trimmed[0] != '#')
		{
			auto eq = trimmed.find('=');
			if (eq != std::string::npos)
			{
				std::string key = trimmed.substr(0, eq);
				size_t end = key.find_last_not_of(" \t");
				if (end != std::string::npos) key = key.substr(0, end + 1);
				if (key == callsign)
				{
					it = lines.erase(it);
					found = true;
					continue;
				}
			}
		}
		++it;
	}

	if (!found) return false;

	std::ofstream out(path);
	if (!out.is_open()) return false;
	for (const auto &l : lines) out << l << "\n";
	return true;
}

std::vector<std::string> CSvxProtocol::GetUsers(void) const
{
	std::lock_guard<std::mutex> lock(m_PasswordMutex);
	std::vector<std::string> users;
	for (const auto &[cs, pw] : m_Passwords)
		users.push_back(cs);
	return users;
}
