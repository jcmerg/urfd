#pragma once

#include "Client.h"

class CSvxClient : public CClient
{
public:
	CSvxClient();
	CSvxClient(const CCallsign &, const CIp &, char = ' ');
	CSvxClient(const CSvxClient &);
	virtual ~CSvxClient() {};

	// identity
	EProtocol GetProtocol(void) const           { return EProtocol::svx; }
	const char *GetProtocolName(void) const     { return "SVX"; }
	bool IsNode(void) const                     { return true; }

	// status
	bool IsAlive(void) const;
};
