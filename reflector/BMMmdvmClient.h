#pragma once

// BMMmdvmClient -- Represents the BM master server as a virtual client
// Part of the BMMmdvm protocol extension for urfd

#include "Defines.h"
#include "Client.h"

class CBMMmdvmClient : public CClient
{
public:
	CBMMmdvmClient();
	CBMMmdvmClient(const CCallsign &, const CIp &, char = ' ');
	CBMMmdvmClient(const CBMMmdvmClient &);

	virtual ~CBMMmdvmClient() {};

	// identity
	EProtocol GetProtocol(void) const           { return EProtocol::bmhomebrew; }
	const char *GetProtocolName(void) const     { return "BMMmdvm"; }
	bool IsNode(void) const                     { return true; }

	// status
	bool IsAlive(void) const;

	// reporting
	void WriteXml(std::ofstream &) {}
};
