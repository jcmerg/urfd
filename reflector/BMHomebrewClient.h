#pragma once

// BMHomebrewClient -- Represents the BM master server as a virtual client
// Part of the BMHomebrew protocol extension for urfd

#include "Defines.h"
#include "Client.h"

class CBMHomebrewClient : public CClient
{
public:
	CBMHomebrewClient();
	CBMHomebrewClient(const CCallsign &, const CIp &, char = ' ');
	CBMHomebrewClient(const CBMHomebrewClient &);

	virtual ~CBMHomebrewClient() {};

	// identity
	EProtocol GetProtocol(void) const           { return EProtocol::bmhomebrew; }
	const char *GetProtocolName(void) const     { return "BMHomebrew"; }
	bool IsNode(void) const                     { return true; }

	// status
	bool IsAlive(void) const;

	// reporting
	void WriteXml(std::ofstream &) {}
};
