#include "DPlusClientPeer.h"
#include "DPlusClientProtocol.h"

CDPlusClientPeer::CDPlusClientPeer() {}

CDPlusClientPeer::CDPlusClientPeer(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule) {}

CDPlusClientPeer::CDPlusClientPeer(const CDPlusClientPeer &client)
	: CClient(client) {}

bool CDPlusClientPeer::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < DPLUSCLI_KEEPALIVE_TIMEOUT);
}
