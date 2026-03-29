//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.

// urfd -- The universal reflector
// Copyright © 2021 Thomas A. Early N7TAE
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include "DExtraProtocol.h"
#include "DPlusProtocol.h"
#include "DCSProtocol.h"
#include "URFProtocol.h"
#include "DMRPlusProtocol.h"
#include "DMRMMDVMProtocol.h"
#include "YSFProtocol.h"
#include "M17Protocol.h"
#include "XLXPeerProtocol.h"
#include "MMDVMClientProtocol.h"
#include "P25Protocol.h"
#include "NXDNProtocol.h"
#include "USRPProtocol.h"
#include "SvxReflectorProtocol.h"
#include "G3Protocol.h"
#include "Protocols.h"
#include "Global.h"

////////////////////////////////////////////////////////////////////////////////////////
// destructor

CProtocols::~CProtocols()
{
	Close();
}

////////////////////////////////////////////////////////////////////////////////////////
// initialization

// Helper: check if protocol is enabled (defaults to true if key not set)
#define IS_PROTO_ENABLED(key) (!g_Configure.Contains(key) || g_Configure.GetBoolean(key))

bool CProtocols::Init(void)
{
	m_Mutex.lock();
	{
		if (IS_PROTO_ENABLED(g_Keys.dextra.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CDextraProtocol>(new CDextraProtocol));
			if (! m_Protocols.back()->Initialize("XRF", EProtocol::dextra, uint16_t(g_Configure.GetUnsigned(g_Keys.dextra.port)), DSTAR_IPV4, DSTAR_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.dplus.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CDplusProtocol>(new CDplusProtocol));
			if (! m_Protocols.back()->Initialize("REF", EProtocol::dplus, uint16_t(g_Configure.GetUnsigned(g_Keys.dplus.port)), DSTAR_IPV4, DSTAR_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.dcs.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CDcsProtocol>(new CDcsProtocol));
			if (! m_Protocols.back()->Initialize("DCS", EProtocol::dcs, uint16_t(g_Configure.GetUnsigned(g_Keys.dcs.port)), DSTAR_IPV4, DSTAR_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.mmdvm.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CDmrmmdvmProtocol>(new CDmrmmdvmProtocol));
			if (! m_Protocols.back()->Initialize(nullptr, EProtocol::dmrmmdvm, uint16_t(g_Configure.GetUnsigned(g_Keys.mmdvm.port)), DMR_IPV4, DMR_IPV6))
				return false;
		}

		if (g_Configure.Contains(g_Keys.xlxpeer.enable) && g_Configure.GetBoolean(g_Keys.xlxpeer.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CXlxPeerProtocol>(new CXlxPeerProtocol));
			if (! m_Protocols.back()->Initialize("XLX", EProtocol::xlxpeer, uint16_t(g_Configure.GetUnsigned(g_Keys.xlxpeer.port)), DMR_IPV4, DMR_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.dmrplus.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CDmrplusProtocol>(new CDmrplusProtocol));
			if (! m_Protocols.back()->Initialize(nullptr, EProtocol::dmrplus, uint16_t(g_Configure.GetUnsigned(g_Keys.dmrplus.port)), DMR_IPV4, DMR_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.ysf.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CYsfProtocol>(new CYsfProtocol));
			if (! m_Protocols.back()->Initialize("YSF", EProtocol::ysf, uint16_t(g_Configure.GetUnsigned(g_Keys.ysf.port)), YSF_IPV4, YSF_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.m17.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CM17Protocol>(new CM17Protocol));
			if (! m_Protocols.back()->Initialize("URF", EProtocol::m17, uint16_t(g_Configure.GetUnsigned(g_Keys.m17.port)), M17_IPV4, M17_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.p25.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CP25Protocol>(new CP25Protocol));
			if (! m_Protocols.back()->Initialize("P25", EProtocol::p25, uint16_t(g_Configure.GetUnsigned(g_Keys.p25.port)), P25_IPV4, P25_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.nxdn.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CNXDNProtocol>(new CNXDNProtocol));
			if (! m_Protocols.back()->Initialize("NXDN", EProtocol::nxdn, uint16_t(g_Configure.GetUnsigned(g_Keys.nxdn.port)), NXDN_IPV4, NXDN_IPV6))
				return false;
		}

		if (g_Configure.Contains(g_Keys.usrp.enable) && g_Configure.GetBoolean(g_Keys.usrp.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CUSRPProtocol>(new CUSRPProtocol));
			if (! m_Protocols.back()->Initialize("USRP", EProtocol::usrp, uint16_t(g_Configure.GetUnsigned(g_Keys.usrp.rxport)), USRP_IPV4, USRP_IPV6))
				return false;
		}

		if (g_Configure.Contains(g_Keys.svx.enable) && g_Configure.GetBoolean(g_Keys.svx.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CSvxReflectorProtocol>(new CSvxReflectorProtocol));
			if (! m_Protocols.back()->Initialize(nullptr, EProtocol::svxreflector, uint16_t(0), SVX_IPV4, SVX_IPV6))
				return false;
		}

		if (IS_PROTO_ENABLED(g_Keys.urf.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CURFProtocol>(new CURFProtocol));
			if (! m_Protocols.back()->Initialize("URF", EProtocol::urf, uint16_t(g_Configure.GetUnsigned(g_Keys.urf.port)), URF_IPV4, URF_IPV6))
				return false;
		}

		if (g_Configure.Contains(g_Keys.g3.enable) && g_Configure.GetBoolean(g_Keys.g3.enable))
		{
			m_Protocols.emplace_back(std::unique_ptr<CG3Protocol>(new CG3Protocol));
			if (! m_Protocols.back()->Initialize("XLX", EProtocol::g3, G3_DV_PORT, DMR_IPV4, DMR_IPV6))
			return false;
		}

		if (g_Configure.Contains(g_Keys.mmdvmclient.enable) && g_Configure.GetBoolean(g_Keys.mmdvmclient.enable))
		{
			uint16_t localport = 0;
			if (g_Configure.Contains(g_Keys.mmdvmclient.localport))
				localport = uint16_t(g_Configure.GetUnsigned(g_Keys.mmdvmclient.localport));
			m_Protocols.emplace_back(std::unique_ptr<CMMDVMClientProtocol>(new CMMDVMClientProtocol));
			if (! m_Protocols.back()->Initialize(nullptr, EProtocol::mmdvmclient, localport, DMR_IPV4, false))
				return false;
		}

	}
	m_Mutex.unlock();

	// done
	return true;
}

void CProtocols::Close(void)
{
	m_Mutex.lock();
	m_Protocols.clear();
	m_Mutex.unlock();
}
