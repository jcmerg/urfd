// BMMmdvmClient -- Represents the BM master server as a virtual client
// Part of the BMMmdvm protocol extension for urfd

#include "BMMmdvmClient.h"

// keep alive timeout same as MMDVM
#define BMHB_KEEPALIVE_TIMEOUT 60

CBMMmdvmClient::CBMMmdvmClient()
{
}

CBMMmdvmClient::CBMMmdvmClient(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule)
{
}

CBMMmdvmClient::CBMMmdvmClient(const CBMMmdvmClient &client)
	: CClient(client)
{
}

bool CBMMmdvmClient::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < BMHB_KEEPALIVE_TIMEOUT);
}
