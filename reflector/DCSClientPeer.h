#pragma once

// DCSClientPeer -- Represents the remote DCS reflector as a virtual client
// Part of the DCSClient protocol extension for urfd

#include "Defines.h"
#include "Client.h"

class CDCSClientPeer : public CClient
{
public:
	CDCSClientPeer();
	CDCSClientPeer(const CCallsign &, const CIp &, char = ' ');
	CDCSClientPeer(const CDCSClientPeer &);

	virtual ~CDCSClientPeer() {};

	// identity
	EProtocol GetProtocol(void) const           { return EProtocol::dcsclient; }
	const char *GetProtocolName(void) const     { return "DCSClient"; }
	bool IsNode(void) const                     { return true; }

	// status
	bool IsAlive(void) const;

	// reporting
	void WriteXml(std::ofstream &) {}
};
