#pragma once

// SvxProtocol -- SvxReflector server accepting incoming SvxLink node connections
// Implements SvxReflector protocol V2 with HMAC-SHA1 auth and OPUS audio
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include "Defines.h"
#include "Timer.h"
#include "Protocol.h"
#include "DVHeaderPacket.h"
#include "DVFramePacket.h"
#include "TGModuleMap.h"

#include <opus/opus.h>
#include <vector>
#include <unordered_map>
#include <set>
#include <string>
#include <atomic>
#include <mutex>

// Reuse message type defines from SvxReflectorProtocol.h
#include "SvxReflectorProtocol.h"

// Node info reported by SvxLink nodes
struct SSvxNodeInfo {
	std::string software;
	std::string location;
	std::string qth;
	std::string rxSiteName;
	std::string txSiteName;
	std::vector<std::string> logics;
	bool populated = false;
};

class CSvxProtocol : public CProtocol
{
public:
	CSvxProtocol();
	virtual ~CSvxProtocol();

	// initialization
	bool Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6);
	void Close(void);

	// task
	void Task(void);

	// dynamic TG management (called from admin socket thread)
	bool AddDynamicTG(uint32_t tg, char module, int ttlSeconds);
	bool RemoveDynamicTG(uint32_t tg);
	std::vector<CTGModuleMap::STGInfo> GetTGMappings(void) const;
	uint32_t ModuleToTG(char module) const;
	void RefreshActivityByModule(char module);
	void ReloadStaticTGMap(void);

	// user management (called from admin socket thread)
	bool AddUser(const std::string &callsign, const std::string &password);
	bool RemoveUser(const std::string &callsign);
	std::vector<std::string> GetUsers(void) const;

	// connected peer info (called from admin socket thread)
	struct SConnectedPeer {
		std::string callsign;
		std::string ip;
		std::set<uint32_t> subscribedTGs;
		bool udpDiscovered;
		SSvxNodeInfo nodeInfo;
	};
	std::vector<SConnectedPeer> GetConnectedPeers() const;

protected:
	// queue helper
	void HandleQueue(void);
	void HandleKeepalives(void);

	// per-peer state
	struct SSvxPeer
	{
		enum class EAuthState { expect_proto_ver, expect_auth_response, connected };

		int tcpFd = -1;
		uint32_t clientId = 0;
		std::string callsign;
		EAuthState authState = EAuthState::expect_proto_ver;
		uint8_t challenge[20] = {};

		// UDP
		CIp udpEndpoint;
		bool udpDiscovered = false;
		uint16_t udpSeqSend = 0;
		uint16_t udpSeqRecv = 0;

		// TG subscriptions
		std::set<uint32_t> subscribedTGs;

		// OPUS codec (per-peer, stateful)
		OpusEncoder *opusEncoder = nullptr;
		OpusDecoder *opusDecoder = nullptr;

		// keepalive timers
		CTimer tcpLastRecvTimer;
		CTimer tcpHeartbeatTimer;
		CTimer udpLastRecvTimer;

		// TCP receive buffer (partial frame accumulation)
		std::vector<uint8_t> tcpRecvBuf;

		// node info from SvxLink JSON
		SSvxNodeInfo nodeInfo;

		// incoming stream state (audio from this peer)
		struct SInStream {
			uint32_t tg = 0;
			char module = ' ';
			uint16_t streamId = 0;
			bool open = false;
			int16_t lastPcm[160] = {};
		} inStream;
	};

	// TCP server
	bool TcpListen(void);
	void TcpAccept(void);
	bool TcpSendFrame(int fd, const uint8_t *data, uint32_t len);
	void TcpReceiveFromPeer(uint32_t clientId);
	void TcpBroadcast(const uint8_t *data, uint32_t len, uint32_t exceptClientId = 0);
	void DisconnectPeer(uint32_t clientId);

	// TCP message handlers
	void OnPeerProtoVer(uint32_t clientId, const std::vector<uint8_t> &payload);
	void OnPeerAuthResponse(uint32_t clientId, const std::vector<uint8_t> &payload);
	void OnPeerNodeInfo(uint32_t clientId, const std::vector<uint8_t> &payload);
	void OnPeerSelectTG(uint32_t clientId, const std::vector<uint8_t> &payload);

	// UDP handling
	void OnUdpPacket(const CBuffer &buffer, const CIp &ip);
	void OnPeerUdpAudio(uint32_t clientId, const CBuffer &buffer);
	void OnPeerUdpFlush(uint32_t clientId);
	void ClosePeerInStream(uint32_t clientId);

	// outbound audio to peers
	void SendAudioToPeer(SSvxPeer &peer, const int16_t *pcm, uint32_t tg);
	void SendFlushToPeer(SSvxPeer &peer);
	void BuildUdpHeader(CBuffer &buf, uint16_t type, uint16_t clientId, uint16_t &seq);

	// stream helpers
	void OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &, const CIp &);

	// serialization helpers (big-endian)
	void PackUint16(std::vector<uint8_t> &buf, uint16_t val);
	void PackUint32(std::vector<uint8_t> &buf, uint32_t val);
	void PackString(std::vector<uint8_t> &buf, const std::string &str);
	uint16_t UnpackUint16(const std::vector<uint8_t> &buf, size_t &pos);
	uint32_t UnpackUint32(const std::vector<uint8_t> &buf, size_t &pos);
	std::string UnpackString(const std::vector<uint8_t> &buf, size_t &pos);

	// TG mapping
	char TGToModule(uint32_t tg) const;
	void LoadTGMap(void);

	// helper to get list of connected node callsigns
	std::vector<std::string> GetConnectedNodeList(void) const;

	// INI persistence for user add/remove
	bool IniAddUser(const std::string &callsign, const std::string &password);
	bool IniRemoveUser(const std::string &callsign);

	// TCP listen socket
	int m_TcpListenFd;

	// peers indexed by clientId
	std::unordered_map<uint32_t, SSvxPeer> m_Peers;
	std::unordered_map<int, uint32_t> m_FdToClientId;  // tcpFd -> clientId
	std::atomic<uint32_t> m_NextClientId;

	// per-user passwords (callsign -> password), protected by m_PasswordMutex
	mutable std::mutex m_PasswordMutex;
	std::unordered_map<std::string, std::string> m_Passwords;

	// gain (linear numerator, 256 = 0dB)
	int32_t m_RxGainNum;
	int32_t m_TxGainNum;

	// TG mapping (static + dynamic)
	mutable std::mutex m_TGMutex;
	std::unordered_map<uint32_t, char> m_TGToModule;
	std::unordered_map<char, uint32_t> m_ModuleToTG;

	// Dynamic TG tracking
	struct SDynTG {
		std::chrono::steady_clock::time_point expires;
	};
	std::unordered_map<uint32_t, SDynTG> m_DynTGs;

	// outgoing stream state per module
	std::unordered_map<char, uint32_t> m_OutStreamTG;
};
