#pragma once

#include "Client.h"

class CSvxReflectorClient : public CClient
{
public:
	CSvxReflectorClient();
	CSvxReflectorClient(const CCallsign &, const CIp &, char = ' ');
	CSvxReflectorClient(const CSvxReflectorClient &);
	virtual ~CSvxReflectorClient() {};

	// identity
	EProtocol GetProtocol(void) const           { return EProtocol::svxreflector; }
	const char *GetProtocolName(void) const     { return "SvxReflector"; }
	bool IsNode(void) const                     { return true; }

	// status
	bool IsAlive(void) const;
};
