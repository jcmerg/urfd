#pragma once

#include "Defines.h"
#include "Client.h"

class CDPlusClientPeer : public CClient
{
public:
	CDPlusClientPeer();
	CDPlusClientPeer(const CCallsign &, const CIp &, char = ' ');
	CDPlusClientPeer(const CDPlusClientPeer &);
	virtual ~CDPlusClientPeer() {};

	EProtocol GetProtocol(void) const           { return EProtocol::dplusclient; }
	const char *GetProtocolName(void) const     { return "DPlusClient"; }
	bool IsNode(void) const                     { return true; }
	bool IsAlive(void) const;
	void WriteXml(std::ofstream &) {}
};
