#pragma once

// MMDVMClientProtocol -- Connects to a DMR master via the MMDVM protocol
// URFD acts as an MMDVM repeater client to an MMDVM master server.
//
// Copyright (C) 2024-2026
// Licensed under the GNU General Public License v3 or later

#include "Defines.h"
#include "Timer.h"
#include "Protocol.h"
#include "DVHeaderPacket.h"
#include "DVFramePacket.h"
#include "SHA256.h"
#include "TGModuleMap.h"
#include "BPTC19696.h"

// MMDVM protocol constants
#define MMDVMCLI_RETRY_PERIOD       10  // seconds between retransmissions
#define MMDVMCLI_TIMEOUT_PERIOD     60  // seconds before connection is considered dead
#define MMDVMCLI_PING_PERIOD        10  // seconds between keepalive pings

// MMDVM data packet size
#define MMDVMCLI_DATA_PACKET_LENGTH 55

// MMDVM byte 15 flags (NOT the same as DMR_DT_* values from Protocol.h)
// These are the flag bits in the MMDVM wire format
#define MMDVMCLI_FLAG_SLOT1         0x00
#define MMDVMCLI_FLAG_SLOT2         0x80
#define MMDVMCLI_FLAG_DATA_SYNC     0x20
#define MMDVMCLI_FLAG_VOICE_SYNC    0x10
// Colour code for this repeater
#define MMDVMCLI_COLOUR_CODE        1

// Stream cache for outbound triplet buffering (3 AMBE frames per DMRD packet)
struct SMMDVMClientStreamCache
{
	CDvHeaderPacket header;
	CDvFramePacket  frames[3];
	uint8_t         frameCount;
	uint32_t        srcId;
	uint8_t         seqNo;       // voice superframe counter (0-5 cycle)
	uint8_t         pktSeqNo;    // global DMRD packet sequence counter
	uint32_t        streamId;
};

class CMMDVMClientProtocol : public CProtocol
{
public:
	// initialization
	bool Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6);

	// task
	void Task(void);

protected:
	// MMDVM state machine
	enum class EHBState
	{
		DISCONNECTED,
		WAITING_LOGIN,
		WAITING_AUTH,
		WAITING_CONFIG,
		WAITING_OPTIONS,
		RUNNING
	};

	// State machine
	void HandleStateMachine(void);
	void HandleIncoming(const CBuffer &Buffer, const CIp &Ip);
	void HandleQueue(void);
	void HandleKeepalives(void) {}  // keepalives handled by state machine

	// Auth packet helpers
	void SendLogin(void);
	void SendAuth(void);
	void SendConfig(void);
	void SendOptions(void);
	void SendPing(void);
	void SendClose(void);

	// DMRD incoming: Master -> Reflector
	void OnDMRDPacketIn(const CBuffer &Buffer);
	void OnDMRDVoiceHeaderIn(const CBuffer &Buffer, uint32_t srcId, uint32_t dstId, uint32_t streamId);
	void OnDMRDVoiceFrameIn(const CBuffer &Buffer, uint32_t srcId, uint32_t dstId, uint32_t streamId);
	void OnDMRDTerminatorIn(const CBuffer &Buffer, uint32_t srcId, uint32_t dstId, uint32_t streamId);

	// DMRD outgoing: Reflector -> Master
	bool EncodeDMRDHeader(const CDvHeaderPacket &header, char module, CBuffer &buffer);
	bool EncodeDMRDVoiceFrame(const CDvFramePacket &frame0, const CDvFramePacket &frame1, const CDvFramePacket &frame2, uint8_t seqNo, uint32_t streamId, char module, CBuffer &buffer);
	bool EncodeDMRDTerminator(uint32_t streamId, char module, CBuffer &buffer);

	// DMR frame construction helpers (adapted from CDmrmmdvmProtocol)
	void AppendVoiceLCToBuffer(CBuffer *, uint32_t srcId, uint32_t dstId) const;
	void AppendTerminatorLCToBuffer(CBuffer *, uint32_t srcId, uint32_t dstId) const;
	void ReplaceEMBInBuffer(CBuffer *, uint8_t) const;
	void AppendDmrIdToBuffer(CBuffer *, uint32_t) const;
	void AppendDmrRptrIdToBuffer(CBuffer *, uint32_t) const;
	uint8_t SlotFlag(char module) const;

	// DMR ID / Callsign helpers
	uint32_t CallsignToDmrId(const CCallsign &cs) const;
	CCallsign DmrIdToCallsign(uint32_t id) const;

	// Stream helpers
	void OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &, const CIp &);

private:
	// Config
	uint32_t        m_FallbackDmrId;
	uint32_t        m_uiDmrId;
	uint8_t         m_uiId[4];         // DMR ID as big-endian 4 bytes
	std::string     m_Password;
	std::string     m_Callsign;
	CIp             m_MasterIp;
	uint16_t        m_MasterPort;

	// State machine
	EHBState        m_State;
	uint8_t         m_uiSalt[4];       // Auth salt from master

	// Timers
	CTimer          m_RetryTimer;
	CTimer          m_TimeoutTimer;
	CTimer          m_PingTimer;

	// TG mapping
	CTGModuleMap    m_TGMap;

	// Incoming stream tracking: master streamId -> URFD streamId
	std::unordered_map<uint32_t, uint16_t> m_IncomingStreams;

	// Outbound stream cache per module (triplet buffering)
	std::unordered_map<char, SMMDVMClientStreamCache> m_OutboundCache;

	// SHA256
	CSHA256         m_SHA256;
};
