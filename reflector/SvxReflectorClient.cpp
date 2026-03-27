#include "SvxReflectorClient.h"

CSvxReflectorClient::CSvxReflectorClient()
{
}

CSvxReflectorClient::CSvxReflectorClient(const CCallsign &cs, const CIp &ip, char mod)
	: CClient(cs, ip, mod)
{
}

CSvxReflectorClient::CSvxReflectorClient(const CSvxReflectorClient &rhs)
	: CClient(rhs)
{
}

bool CSvxReflectorClient::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < SVX_TCP_KEEPALIVE_TIMEOUT);
}
