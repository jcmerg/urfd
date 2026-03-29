#pragma once

// SvxReflectorProtocol -- Connects to an SvxLink SvxReflector via TCP/UDP
// Implements SvxReflector protocol V2 with HMAC-SHA1 auth and OPUS audio
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include "Defines.h"
#include "Timer.h"
#include "Protocol.h"
#include "DVHeaderPacket.h"
#include "DVFramePacket.h"

#include <opus/opus.h>
#include <vector>
#include <unordered_map>
#include <string>

// TCP message types
#define SVX_TCP_MSG_HEARTBEAT            1
#define SVX_TCP_MSG_PROTO_VER            5
#define SVX_TCP_MSG_AUTH_CHALLENGE        10
#define SVX_TCP_MSG_AUTH_RESPONSE         11
#define SVX_TCP_MSG_AUTH_OK              12
#define SVX_TCP_MSG_ERROR                13
#define SVX_TCP_MSG_SERVER_INFO          100
#define SVX_TCP_MSG_NODE_LIST            101
#define SVX_TCP_MSG_NODE_JOINED          102
#define SVX_TCP_MSG_NODE_LEFT            103
#define SVX_TCP_MSG_TALKER_START         104
#define SVX_TCP_MSG_TALKER_STOP          105
#define SVX_TCP_MSG_SELECT_TG            106
#define SVX_TCP_MSG_NODE_INFO            111

// UDP message types
#define SVX_UDP_MSG_HEARTBEAT            1
#define SVX_UDP_MSG_AUDIO                101
#define SVX_UDP_MSG_FLUSH_SAMPLES        102
#define SVX_UDP_MSG_ALL_FLUSHED          103

class CSvxReflectorProtocol : public CProtocol
{
public:
	CSvxReflectorProtocol();
	virtual ~CSvxReflectorProtocol();

	// initialization
	bool Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6);

	// task
	void Task(void);

protected:
	// queue helper
	void HandleQueue(void);
	void HandleKeepalives(void);

	// TCP connection
	bool TcpConnect(void);
	void TcpDisconnect(void);
	bool TcpSendFrame(const uint8_t *data, uint32_t len);
	bool TcpReceiveFrame(std::vector<uint8_t> &frame);

	// TCP message handlers
	void OnTcpMessage(uint16_t type, const std::vector<uint8_t> &payload);
	void OnAuthChallenge(const std::vector<uint8_t> &payload);
	void OnAuthOk(void);
	void OnServerInfo(const std::vector<uint8_t> &payload);
	void OnTalkerStart(const std::vector<uint8_t> &payload);
	void OnTalkerStop(const std::vector<uint8_t> &payload);

	// UDP audio
	void OnUdpAudio(const CBuffer &buffer);
	void OnUdpFlush(void);
	void CloseInStream(void);
	void EncodeAndSendAudio(const int16_t *pcm, uint32_t tg);
	void BuildUdpHeader(CBuffer &buf, uint16_t type);

	// serialization helpers (big-endian)
	void PackUint16(std::vector<uint8_t> &buf, uint16_t val);
	void PackUint32(std::vector<uint8_t> &buf, uint32_t val);
	void PackString(std::vector<uint8_t> &buf, const std::string &str);
	uint16_t UnpackUint16(const std::vector<uint8_t> &buf, size_t &pos);
	uint32_t UnpackUint32(const std::vector<uint8_t> &buf, size_t &pos);
	std::string UnpackString(const std::vector<uint8_t> &buf, size_t &pos);

	// TG mapping
	char TGToModule(uint32_t tg) const;
	uint32_t ModuleToTG(char module) const;
	void LoadTGMap(void);

	// stream helpers
	void OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &, const CIp &);

	// state
	enum class EState { disconnected, connecting, authenticating, connected };
	EState m_State;

	// TCP
	int m_TcpFd;
	CIp m_ServerIp;

	// timers
	CTimer m_TcpHeartbeatTimer;
	CTimer m_TcpLastReceiveTimer;
	CTimer m_UdpHeartbeatTimer;
	CTimer m_UdpLastReceiveTimer;
	CTimer m_ReconnectTimer;
	int m_ReconnectBackoff;

	// auth
	uint32_t m_ClientId;
	uint16_t m_UdpSeq;
	std::string m_Password;
	std::string m_Callsign;
	std::string m_Host;

	// OPUS
	OpusEncoder *m_OpusEncoder;
	OpusDecoder *m_OpusDecoder;

	// TG mapping
	std::unordered_map<uint32_t, char> m_TGToModule;
	std::unordered_map<char, uint32_t> m_ModuleToTG;

	// incoming stream state
	struct SIncomingStream {
		uint32_t tg;
		char module;
		uint16_t streamId;
		bool open;
		std::string talkerCallsign;
		int16_t lastPcm[160] = {};
	};
	SIncomingStream m_InStream;

	// outgoing stream state per module
	std::unordered_map<char, uint32_t> m_OutStreamTG;
};
