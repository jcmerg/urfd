#pragma once

// YSFClientProtocol -- Connects to a remote YSF reflector as a client
// URFD acts as a YSF hotspot/node connecting to an external YSF reflector.
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include "Defines.h"
#include "Timer.h"
#include "Protocol.h"
#include "DVHeaderPacket.h"
#include "DVFramePacket.h"
#include "YSFDefines.h"
#include "YSFFich.h"
#include "YSFPayload.h"
#include "YSFUtils.h"

// YSF client protocol constants (aligned with g4klx YSFGateway)
#define YSFCLI_POLL_PERIOD          5   // seconds between polls
#define YSFCLI_KEEPALIVE_TIMEOUT    60  // seconds before connection is considered dead
#define YSFCLI_RECONNECT_PERIOD     5   // seconds between reconnection attempts
#define YSFCLI_INITIAL_POLL_COUNT   3   // number of polls sent on initial connect

// A single YSF client mapping: remote YSF reflector <-> local reflector module
struct SYsfClientMapping
{
	std::string host;
	uint16_t    port;
	CIp         ip;
	char        localModule;    // module on our reflector
	uint8_t     dgid;           // DG-ID to set on outbound packets (0 = disabled)
	bool        connected;
	CTimer      pollTimer;
	CTimer      timeoutTimer;

	SYsfClientMapping() : port(42000), localModule('A'), dgid(0), connected(false) {}
};

// Stream cache for outbound YSF packets (5-frame grouping)
struct SYsfClientStreamCache
{
	CDvHeaderPacket header;
	CDvFramePacket  frames[5];
	uint8_t         dgid;       // DG-ID for outbound FICH
};

class CYsfClientProtocol : public CProtocol
{
public:
	// initialization
	bool Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6);

	// task
	void Task(void);

	// admin API
	void RequestReconnect(void) { m_ReconnectRequested = true; }
	bool AddMapping(const std::string &host, uint16_t port, char localMod, uint8_t dgid = 0);
	bool RemoveMapping(char localMod);
	std::vector<SYsfClientMapping> GetMappings(void) const;

protected:
	// connection management
	void HandleConnections(void);
	void SendPoll(SYsfClientMapping &mapping);
	void SendDisconnect(SYsfClientMapping &mapping);

	// incoming packet handling
	void HandleIncoming(const CBuffer &Buffer, const CIp &Ip);
	void OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip);

	// outbound queue
	void HandleQueue(void);
	void HandleKeepalives(void) {}

	// packet decoding (YSF wire format)
	bool IsValidPollPacket(const CBuffer &, CCallsign *);
	bool IsValidDvPacket(const CBuffer &, CYSFFICH *);
	bool IsValidDvHeaderPacket(const CIp &, const CYSFFICH &, const CBuffer &, std::unique_ptr<CDvHeaderPacket> &, std::array<std::unique_ptr<CDvFramePacket>, 5> &);
	bool IsValidDvFramePacket(const CIp &, const CYSFFICH &, const CBuffer &, std::unique_ptr<CDvHeaderPacket> &, std::array<std::unique_ptr<CDvFramePacket>, 5> &);
	bool IsValidDvLastFramePacket(const CIp &, const CYSFFICH &, const CBuffer &, std::unique_ptr<CDvFramePacket> &, std::unique_ptr<CDvFramePacket> &);

	// packet encoding (reuse YSF server encode methods)
	void EncodePollPacket(CBuffer *);
	void EncodeDisconnectPacket(CBuffer *);
	bool EncodeYSFHeaderPacket(const CDvHeaderPacket &, uint8_t dgid, CBuffer *) const;
	bool EncodeYSFPacket(const CDvHeaderPacket &, const CDvFramePacket *, uint8_t dgid, CBuffer *) const;
	bool EncodeLastYSFPacket(const CDvHeaderPacket &, uint8_t dgid, CBuffer *) const;

	// stream ID helper
	uint32_t IpToStreamId(const CIp &) const;

	// mapping helpers
	SYsfClientMapping *FindMappingByLocal(char localMod);
	SYsfClientMapping *FindMappingByIp(const CIp &ip);

private:
	// config
	CCallsign   m_ClientCallsign;
	char        m_szCallsign[YSF_CALLSIGN_LENGTH + 1];

	// module mappings
	mutable std::mutex m_MappingMutex;
	std::vector<SYsfClientMapping> m_Mappings;

	// outbound stream cache per local module
	std::unordered_map<char, SYsfClientStreamCache> m_StreamsCache;

	// reconnect flag
	std::atomic<bool> m_ReconnectRequested{false};
};
