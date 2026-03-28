// MMDVMClientPeer -- Represents the MMDVM master server as a virtual client
// Part of the MMDVMClient protocol extension for urfd

#include "MMDVMClientPeer.h"

// keep alive timeout same as MMDVM
#define MMDVMCLI_KEEPALIVE_TIMEOUT 60

CMMDVMClientPeer::CMMDVMClientPeer()
{
}

CMMDVMClientPeer::CMMDVMClientPeer(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule)
{
}

CMMDVMClientPeer::CMMDVMClientPeer(const CMMDVMClientPeer &client)
	: CClient(client)
{
}

bool CMMDVMClientPeer::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < MMDVMCLI_KEEPALIVE_TIMEOUT);
}
