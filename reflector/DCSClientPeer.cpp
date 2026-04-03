// DCSClientPeer -- Represents the remote DCS reflector as a virtual client
// Part of the DCSClient protocol extension for urfd

#include "DCSClientPeer.h"

#define DCSCLI_PEER_KEEPALIVE_TIMEOUT 60

CDCSClientPeer::CDCSClientPeer()
{
}

CDCSClientPeer::CDCSClientPeer(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule)
{
}

CDCSClientPeer::CDCSClientPeer(const CDCSClientPeer &client)
	: CClient(client)
{
}

bool CDCSClientPeer::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < DCSCLI_PEER_KEEPALIVE_TIMEOUT);
}
