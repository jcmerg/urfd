#pragma once

// DExtraClientProtocol -- Connects to a remote DExtra reflector as a client
//
// Copyright (C) 2026
// Licensed under the GNU General Public License v3 or later

#include "Defines.h"
#include "Timer.h"
#include "Protocol.h"
#include "DVHeaderPacket.h"
#include "DVFramePacket.h"

// DExtra client constants (aligned with ircDDBGateway)
#define DEXTRACLI_KEEPALIVE_PERIOD    3   // seconds
#define DEXTRACLI_KEEPALIVE_TIMEOUT   30  // seconds
#define DEXTRACLI_RECONNECT_PERIOD    5   // seconds

// A single DExtra module mapping
struct SDExtraClientMapping
{
	std::string host;
	uint16_t    port;
	CIp         ip;
	char        remoteModule;
	char        localModule;
	bool        connected;
	CTimer      keepaliveTimer;
	CTimer      timeoutTimer;

	SDExtraClientMapping() : port(30001), remoteModule('A'), localModule('A'), connected(false) {}
};

class CDExtraClientProtocol : public CProtocol
{
public:
	bool Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6);
	void Task(void);

	// admin API
	void RequestReconnect(void) { m_ReconnectRequested = true; }
	bool AddMapping(const std::string &host, uint16_t port, char remoteMod, char localMod);
	bool RemoveMapping(char localMod);
	std::vector<SDExtraClientMapping> GetMappings(void) const;

protected:
	void HandleConnections(void);
	void SendConnect(SDExtraClientMapping &mapping);
	void SendDisconnect(SDExtraClientMapping &mapping);
	void SendKeepalive(SDExtraClientMapping &mapping);

	void HandleIncoming(const CBuffer &Buffer, const CIp &Ip);
	void OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip);

	void HandleQueue(void);
	void HandleKeepalives(void) {}

	// packet decoding
	bool IsValidConnectAckPacket(const CBuffer &, CCallsign *, char *);
	bool IsValidConnectNackPacket(const CBuffer &, CCallsign *);
	bool IsValidKeepAlivePacket(const CBuffer &, CCallsign *);
	bool IsValidDvHeaderPacket(const CBuffer &, std::unique_ptr<CDvHeaderPacket> &);
	bool IsValidDvFramePacket(const CBuffer &, std::unique_ptr<CDvFramePacket> &);

	// packet encoding
	void EncodeConnectPacket(CBuffer *, char localModule, char remoteModule);
	void EncodeDisconnectPacket(CBuffer *, char localModule);
	void EncodeKeepAlivePacket(CBuffer *);
	bool EncodeDvHeaderPacket(const CDvHeaderPacket &, CBuffer &) const;
	bool EncodeDvFramePacket(const CDvFramePacket &, CBuffer &) const;

	SDExtraClientMapping *FindMappingByLocal(char localMod);
	SDExtraClientMapping *FindMappingByIp(const CIp &ip);

private:
	CCallsign   m_ClientCallsign;

	mutable std::mutex m_MappingMutex;
	std::vector<SDExtraClientMapping> m_Mappings;

	std::atomic<bool> m_ReconnectRequested{false};
};
