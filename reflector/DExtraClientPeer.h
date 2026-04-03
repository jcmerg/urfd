#pragma once

#include "Defines.h"
#include "Client.h"

class CDExtraClientPeer : public CClient
{
public:
	CDExtraClientPeer();
	CDExtraClientPeer(const CCallsign &, const CIp &, char = ' ');
	CDExtraClientPeer(const CDExtraClientPeer &);
	virtual ~CDExtraClientPeer() {};

	EProtocol GetProtocol(void) const           { return EProtocol::dextraclient; }
	const char *GetProtocolName(void) const     { return "DExtraClient"; }
	bool IsNode(void) const                     { return true; }
	bool IsAlive(void) const;
	void WriteXml(std::ofstream &) {}
};
