#pragma once

// YSFClientPeer -- Represents the remote YSF reflector as a virtual client

#include "Defines.h"
#include "Client.h"

class CYSFClientPeer : public CClient
{
public:
	CYSFClientPeer();
	CYSFClientPeer(const CCallsign &, const CIp &, char = ' ');
	CYSFClientPeer(const CYSFClientPeer &);

	virtual ~CYSFClientPeer() {};

	EProtocol GetProtocol(void) const           { return EProtocol::ysfclient; }
	const char *GetProtocolName(void) const     { return "YSFClient"; }
	bool IsNode(void) const                     { return true; }

	bool IsAlive(void) const;
	void WriteXml(std::ofstream &) {}
};
