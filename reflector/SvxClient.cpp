#include "SvxClient.h"

CSvxClient::CSvxClient()
{
}

CSvxClient::CSvxClient(const CCallsign &cs, const CIp &ip, char mod)
	: CClient(cs, ip, mod)
{
}

CSvxClient::CSvxClient(const CSvxClient &rhs)
	: CClient(rhs)
{
}

bool CSvxClient::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < SVXS_TCP_KEEPALIVE_TIMEOUT);
}
