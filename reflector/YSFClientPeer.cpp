// YSFClientPeer -- Represents the remote YSF reflector as a virtual client

#include "YSFClientPeer.h"
#include "YSFClientProtocol.h"

CYSFClientPeer::CYSFClientPeer()
{
}

CYSFClientPeer::CYSFClientPeer(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule)
{
}

CYSFClientPeer::CYSFClientPeer(const CYSFClientPeer &client)
	: CClient(client)
{
}

bool CYSFClientPeer::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < YSFCLI_KEEPALIVE_TIMEOUT);
}
