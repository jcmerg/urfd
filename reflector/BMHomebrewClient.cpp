// BMHomebrewClient -- Represents the BM master server as a virtual client
// Part of the BMHomebrew protocol extension for urfd

#include "BMHomebrewClient.h"

// keep alive timeout same as MMDVM
#define BMHB_KEEPALIVE_TIMEOUT 60

CBMHomebrewClient::CBMHomebrewClient()
{
}

CBMHomebrewClient::CBMHomebrewClient(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule)
{
}

CBMHomebrewClient::CBMHomebrewClient(const CBMHomebrewClient &client)
	: CClient(client)
{
}

bool CBMHomebrewClient::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < BMHB_KEEPALIVE_TIMEOUT);
}
