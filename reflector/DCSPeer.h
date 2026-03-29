#pragma once

#include "Peer.h"
#include "DCSPeerClient.h"

class CDcsPeer : public CPeer
{
public:
	CDcsPeer();
	CDcsPeer(const CCallsign &, const CIp &, const char *, const CVersion &);
	CDcsPeer(const CDcsPeer &) = delete;

	bool IsAlive(void) const;

	EProtocol GetProtocol(void) const           { return EProtocol::dcs; }
	const char *GetProtocolName(void) const     { return "DCS"; }
};
