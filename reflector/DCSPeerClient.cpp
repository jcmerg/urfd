#include "DCSPeerClient.h"

CDcsPeerClient::CDcsPeerClient() {}

CDcsPeerClient::CDcsPeerClient(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule) {}

CDcsPeerClient::CDcsPeerClient(const CDcsPeerClient &client)
	: CClient(client) {}

bool CDcsPeerClient::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < DCS_KEEPALIVE_TIMEOUT);
}
