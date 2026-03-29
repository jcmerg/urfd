#pragma once

#include "Defines.h"
#include "Client.h"

class CDcsPeerClient : public CClient
{
public:
	CDcsPeerClient();
	CDcsPeerClient(const CCallsign &, const CIp &, char = ' ');
	CDcsPeerClient(const CDcsPeerClient &);
	virtual ~CDcsPeerClient() {};

	EProtocol GetProtocol(void) const           { return EProtocol::dcs; }
	const char *GetProtocolName(void) const     { return "DCS"; }
	bool IsNode(void) const                     { return true; }
	bool IsPeer(void) const                     { return true; }

	bool IsAlive(void) const;
	void WriteXml(std::ofstream &) {}
};
