#include "DExtraClientPeer.h"
#include "DExtraClientProtocol.h"

CDExtraClientPeer::CDExtraClientPeer() {}

CDExtraClientPeer::CDExtraClientPeer(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule) {}

CDExtraClientPeer::CDExtraClientPeer(const CDExtraClientPeer &client)
	: CClient(client) {}

bool CDExtraClientPeer::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < DEXTRACLI_KEEPALIVE_TIMEOUT);
}
