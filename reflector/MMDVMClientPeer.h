#pragma once

// MMDVMClientPeer -- Represents the MMDVM master server as a virtual client
// Part of the MMDVMClient protocol extension for urfd

#include "Defines.h"
#include "Client.h"

class CMMDVMClientPeer : public CClient
{
public:
	CMMDVMClientPeer();
	CMMDVMClientPeer(const CCallsign &, const CIp &, char = ' ');
	CMMDVMClientPeer(const CMMDVMClientPeer &);

	virtual ~CMMDVMClientPeer() {};

	// identity
	EProtocol GetProtocol(void) const           { return EProtocol::mmdvmclient; }
	const char *GetProtocolName(void) const     { return "MMDVMClient"; }
	bool IsNode(void) const                     { return true; }

	// status
	bool IsAlive(void) const;

	// reporting
	void WriteXml(std::ofstream &) {}
};
