//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.

// urfd -- The universal reflector
// Copyright © 2023 Thomas A. Early N7TAE
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

#include <iostream>
#include <sstream>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#include "Global.h"

void CLookupNxdn::ClearContents()
{
	m_CallsignMap.clear();
	m_NxdnidMap.clear();
	m_NameMap.clear();
}

void CLookupNxdn::LoadParameters()
{
	m_Type = g_Configure.GetRefreshType(g_Keys.nxdniddb.mode);
	m_Refresh = g_Configure.GetUnsigned(g_Keys.nxdniddb.refreshmin);
	m_Path.assign(g_Configure.GetString(g_Keys.nxdniddb.filepath));
	m_Url.assign(g_Configure.GetString(g_Keys.nxdniddb.url));
}

const UCallsign *CLookupNxdn::FindCallsign(uint16_t nxdnid) const
{
	auto found = m_CallsignMap.find(nxdnid);
	if ( found != m_CallsignMap.end() )
	{
		return &(found->second);
	}
	return nullptr;
}

uint16_t CLookupNxdn::FindNXDNid(const UCallsign &ucs) const
{
	auto found = m_NxdnidMap.find(ucs);
	if ( found != m_NxdnidMap.end() )
	{
		return found->second;
	}
	return 0;
}

std::string CLookupNxdn::FindName(uint16_t id) const
{
	auto found = m_NameMap.find(id);
	if (found != m_NameMap.end())
		return found->second;
	return {};
}

void CLookupNxdn::UpdateContent(std::stringstream &ss, Eaction action)
{
	std::string line;
	while (std::getline(ss, line))
	{
		bool failed = true;
		auto l = atol(line.c_str()); // no throw guarantee
		if (0 < l && l < 0x10000)
		{
			auto id = uint32_t(l);
			const auto fields = LookupSplitFields(line, LookupDelimiter(line));
			if (fields.size() >= 2)
			{
				const auto &cs_str = fields[1];
				CCallsign cs;
				cs.SetCallsign(cs_str, false);
				if (cs.IsValid())
				{
					failed = false;
					if (Eaction::normal == action)
					{
						auto key = cs.GetKey();
						m_NxdnidMap[key] = id;
						m_CallsignMap[id] = key;
						if (fields.size() >= 3 && !fields[2].empty())
							m_NameMap[id] = fields[2];
					}
					else if (Eaction::parse == action)
					{
						std::cout << id << ',' << cs_str << ",\n";
					}
				}
			}
		}
		if (Eaction::error_only == action && failed)
		{
			std::cout << line << '\n';
		}
	}
	if (Eaction::normal == action)
		{ std::ostringstream s; s << "NXDN Id database size: " << m_NxdnidMap.size(); std::cout << s.str() << std::endl; }
}
