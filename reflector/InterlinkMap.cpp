//
//  ccallsignlist.cpp
//  m17ref
//
//  Created by Jean-Luc Deltombe (LX3JL) on 30/12/2015.
//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.
//  Copyright © 2020,2022 Thomas A. Early, N7TAE
//
// ----------------------------------------------------------------------------
//    This file is part of m17ref.
//
//    m17ref is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    m17ref is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    with this software.  If not, see <http://www.gnu.org/licenses/>.
// ----------------------------------------------------------------------------

#include <fstream>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "Global.h"
#include "InterlinkMap.h"

CInterlinkMap::CInterlinkMap()
{
	m_Filename.clear();
	::memset(&m_LastModTime, 0, sizeof(time_t));
}

bool CInterlinkMap::LoadFromFile(const std::string &filename)
{
	bool ok = false;
	char line[256];

	// and load
	std::ifstream file(filename);
	if ( file.is_open() )
	{
		Lock();

		// empty list
		m_InterlinkMap.clear();
		// fill with file content
		while ( file.getline(line, sizeof(line)).good() )
		{
			char *token[5];
			// remove leading & trailing spaces
			token[0] = ToUpper(TrimWhiteSpaces(line));
			// crack it
			if ( (strlen(token[0]) > 0) && (token[0][0] != '#') )
			{
				const char *delim = " \t\r";
				// 1st token is callsign
				if ( (token[0] = strtok(token[0], delim)) != nullptr )
				{
					if (strcmp(token[0], g_Configure.GetString(g_Keys.names.callsign).c_str()))
					{
						// default port and protocol depend on callsign prefix
						int default_port;
						std::string default_proto;
						if (0 == memcmp(token[0], "URF", 3)) {
							default_port = 10017;
							default_proto = "URF";
						} else if (0 == memcmp(token[0], "DCS", 3)) {
							default_port = 30051;
							default_proto = "DCS";
						} else {
							default_port = 10002;
							default_proto = "XLX";
						}
						if (m_InterlinkMap.end() == m_InterlinkMap.find(token[0]))
						{
							// read remaining tokens
							// 1=IP 2=Modules 3=Port 4=Protocol (Port and Protocol optional)
							// OR... 1=Modules and the dht will be used
							for (int i=1; i<5; i++)
							{
								token[i] = strtok(nullptr, delim);
							}

							if (token[2])
							{
								int port = default_port;
								std::string proto = default_proto;
								if (token[3])
								{
									// token[3] could be a port number or a protocol name
									if (std::isdigit(token[3][0]))
									{
										port = std::atoi(token[3]);
										if (port < 1024 || port > 49000)
										{
											std::cout << token[0] << " Port " << port << " is out of range, resetting to " << default_port << std::endl;
											port = default_port;
										}
										// token[4] is protocol if present
										if (token[4])
											proto = token[4];
									}
									else
									{
										// token[3] is protocol (no port specified)
										proto = token[3];
									}
								}
								auto &item = m_InterlinkMap[token[0]] = CInterlinkMapItem(token[1], token[2], (uint16_t)port);
								item.SetProtocol(proto);
							}
#ifndef NO_DHT
							else if (token[1])
							{
								m_InterlinkMap[token[0]] = CInterlinkMapItem(token[1]);
							}
#endif
							else
							{
								std::cout << token[0] << " has insufficient parameters!" << std::endl;
							}
						}
						else
						{
							std::cerr << "Duplicate found: " << token[0] << " in " << filename << std::endl;
						}
					}
					else
					{
						std::cerr << "Self linking is not allowed! You cannot use " << token[0] << " in " << filename << std::endl;
					}
				}
			}
		}
		// close file
		file.close();

		// keep file path
		m_Filename.assign(filename);

		// update time
		GetLastModTime(&m_LastModTime);

		// and done
		Unlock();
		ok = true;
		std::cout << "Gatekeeper loaded " << m_InterlinkMap.size() << " lines from " << filename <<  std::endl;
	}
	else
	{
		std::cout << "Gatekeeper cannot find " << filename <<  std::endl;
	}

	return ok;
}

bool CInterlinkMap::ReloadFromFile(void)
{
	bool ok = false;

	if ( ! m_Filename.empty() )
	{
		ok = LoadFromFile(m_Filename);
	}
	return ok;
}

bool CInterlinkMap::NeedReload(void)
{
	bool needReload = false;

	time_t time;
	if ( GetLastModTime(&time) )
	{
		needReload = time != m_LastModTime;
	}
	return needReload;
}

bool CInterlinkMap::IsCallsignListed(const std::string &callsign, char module) const
{
	const auto item = m_InterlinkMap.find(callsign);
	if (m_InterlinkMap.cend() == item)
		return false;
	else
		return item->second.HasModuleListed(module);
}

bool CInterlinkMap::IsCallsignListed(const std::string &callsign, const CIp &ip, const char *modules) const
{
	const auto item = m_InterlinkMap.find(callsign);
	if (m_InterlinkMap.cend() != item)
	{
		if ( item->second.CheckListedModules(modules) )
		{
			if ( ip == item->second.GetIp() )
			{
				return true;
			}
		}
	}
	else
	{
		std::cout << "'" << callsign << "' not found in interlink map\n";
	}

	return false;
}

CInterlinkMapItem *CInterlinkMap::FindMapItem(const std::string &cs)
{
	auto it = m_InterlinkMap.find(cs);
	if (m_InterlinkMap.end() == it)
		return nullptr;
	return &it->second;
}

char *CInterlinkMap::TrimWhiteSpaces(char *str)
{
	char *end;

	// Trim leading space & tabs
	while((*str == ' ') || (*str == '\t')) str++;

	// All spaces?
	if(*str == 0)
		return str;

	// Trim trailing space, tab or lf
	end = str + ::strlen(str) - 1;
	while((end > str) && ((*end == ' ') || (*end == '\t') || (*end == '\r'))) end--;

	// Write new null terminator
	*(end+1) = 0;

	return str;
}

bool CInterlinkMap::GetLastModTime(time_t *time)
{
	bool ok = false;

	if ( ! m_Filename.empty() )
	{
		struct stat fileStat;
		if( ::stat(m_Filename.c_str(), &fileStat) != -1 )
		{
			*time = fileStat.st_mtime;
			ok = true;
		}
	}
	return ok;
}

char *CInterlinkMap::ToUpper(char *str)
{
	constexpr auto diff = 'a' - 'A';
	for (char *p=str; *p; p++)
	{
		if (*p >= 'a' && *p <= 'z')
			*p -= diff;
	}
	return str;
}

#ifndef NO_DHT
void CInterlinkMap::Update(const std::string &cs, const std::string &cmods, const std::string &ipv4, const std::string &ipv6, uint16_t port, const std::string &emods)
{
	auto it = m_InterlinkMap.find(cs);
	if (m_InterlinkMap.end() == it)
	{
		std::cerr << "Can't Update CInterlinkMap item '" << cs << "' because it doesn't exist!";
	}
	else
	{
		it->second.UpdateItem(cmods, ipv4, ipv6, port, emods);
	}
}
#endif
