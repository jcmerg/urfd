#include <string.h>
#include "Reflector.h"
#include "DCSPeer.h"

CDcsPeer::CDcsPeer() {}

CDcsPeer::CDcsPeer(const CCallsign &callsign, const CIp &ip, const char *modules, const CVersion &version)
	: CPeer(callsign, ip, modules, version)
{
	std::cout << "Adding DCS peer " << callsign << std::endl;

	for ( unsigned i = 0; i < ::strlen(modules); i++ )
	{
		m_Clients.push_back(std::make_shared<CDcsPeerClient>(callsign, ip, modules[i]));
	}
}

bool CDcsPeer::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < DCS_KEEPALIVE_TIMEOUT);
}
