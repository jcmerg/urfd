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

#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <thread>
#include <sys/stat.h>
#include "CurlGet.h"
#include "Lookup.h"

char LookupDelimiter(const std::string &line)
{
	// Order matters: a semicolon separated record may legitimately contain a comma
	// inside a name field, so ';' has to win when both are present.
	for (const char c : { ';', '\t', ',' })
	{
		if (std::string::npos != line.find(c))
			return c;
	}
	return ';';
}

std::vector<std::string> LookupSplitFields(const std::string &line, char delim)
{
	std::vector<std::string> fields;
	std::string field;
	std::istringstream iss(line);
	while (std::getline(iss, field, delim))
	{
		// Strip whitespace and stray CR from CRLF sources.
		while (!field.empty() && (unsigned char)field.back() <= ' ')
			field.pop_back();
		while (!field.empty() && (unsigned char)field.front() <= ' ')
			field.erase(0, 1);
		fields.push_back(field);
	}
	return fields;
}

void CLookup::LookupClose()
{
	keep_running = false;
	if (m_Future.valid())
		m_Future.get();
}

std::time_t CLookup::GetLastModTime()
{
	struct stat fileStat;
	if(0 == stat(m_Path.c_str(), &fileStat))
	{
		return fileStat.st_mtime;
	}
	return 0;
}

void CLookup::LookupInit()
{
	LoadParameters();
	m_LastLoadTime = 0;
	m_ContentLoaded = false;
	m_CachePath = m_Path.empty() ? std::string() : m_Path + ".cache";

	m_Future = std::async(std::launch::async, &CLookup::Thread, this);
}

void CLookup::Thread()
{
	const unsigned long wait_cycles = (m_Refresh > 0) ? m_Refresh * 6u : 1u;
	unsigned long count = 0;
	while (keep_running)
	{
		std::stringstream ss;
		bool http_loaded = false;
		bool file_loaded = false;

		// load http section first, if configured and m_Refresh minutes have lapsed
		// on the first pass through this while loop (count == 0)
		if (ERefreshType::file != m_Type && 0ul == count % wait_cycles)
		{
			// if SIG_INT was received at this point in time,
			// in might take a bit more than 10 seconds to soft close
			http_loaded = LoadContentHttp(ss);
			if (http_loaded)
			{
				SaveHttpCache(ss);	// keep a copy for the next outage
				count++;	// only advance on success, so failed loads retry in 10s
			}
			else
			{
				count = 0;	// reset to keep retrying every 10s until first success
				// Fall back to the last good download, but only while we have no
				// content at all — once the map is populated, later failures just
				// leave it in place.
				if (! m_ContentLoaded)
					http_loaded = LoadContentFile(m_CachePath, ss);
			}
		}
		else
		{
			count++;
		}

		// load the file if http was loaded or if we haven't loaded since the last mod time
		if (ERefreshType::http != m_Type)
		{
			if (http_loaded || m_LastLoadTime < GetLastModTime())
			{
				file_loaded = LoadContentFile(ss);
				time(&m_LastLoadTime);
			}
		}

		// now update the map(s) if anything was loaded
		if (http_loaded || file_loaded)
		{
			Lock();
			// if m_Type == ERefreshType::both, and if something was deleted from the file,
			// it won't be purged from the map(s) until http is loaded
			// It would be a lot of work (iterating on an unordered_map) to do otherwise!
			if (http_loaded || ERefreshType::file == m_Type)
				ClearContents();
			UpdateContent(ss, Eaction::normal);
			Unlock();
			m_ContentLoaded = true;
		}

		// now wait for 10 seconds
		std::this_thread::sleep_for(std::chrono::seconds(10));
	}
}

// The configured URL may be a comma separated list. The first entry is the primary
// source; the rest are tried in order when it cannot be reached, so one directory
// going down no longer empties the database.
std::vector<std::string> CLookup::SplitUrls() const
{
	std::vector<std::string> urls;
	std::string item;
	std::istringstream iss(m_Url);
	while (std::getline(iss, item, ','))
	{
		item.erase(0, item.find_first_not_of(" \t"));
		const auto end = item.find_last_not_of(" \t\r");
		if (std::string::npos != end)
			item.erase(end + 1);
		if (! item.empty())
			urls.push_back(item);
	}
	return urls;
}

bool CLookup::LoadContentHttp(std::stringstream &ss)
{
	const auto urls = SplitUrls();
	CCurlGet get;

	for (std::size_t i = 0; i < urls.size(); i++)
	{
		if (CURLE_OK == get.GetURL(urls[i], ss))
		{
			if (i > 0)
				std::cout << "Loaded database from fallback source '" << urls[i] << "'" << std::endl;
			return true;
		}
		// A transfer that failed part way through leaves partial data behind.
		ss.str(std::string());
		ss.clear();
	}
	return false;
}

bool CLookup::LoadContentFile(std::stringstream &ss)
{
	return LoadContentFile(m_Path, ss);
}

bool CLookup::LoadContentFile(const std::string &path, std::stringstream &ss)
{
	if (path.empty())
		return false;

	bool rval = false;
    std::ifstream file(path);
    if ( file )
    {
        ss << file.rdbuf();
        ss.clear();  // clear failbit if file was empty (operator<< sets it when no chars extracted)
        file.close();
		rval = true;
    }
	return rval;
}

bool CLookup::SaveHttpCache(const std::stringstream &ss)
{
	if (m_CachePath.empty())
		return false;

	// Write via a temporary and rename, so a crash mid-write cannot leave a
	// truncated cache that would silently shrink the database on next start.
	const std::string tmp(m_CachePath + ".tmp");
	{
		std::ofstream file(tmp, std::ofstream::out | std::ofstream::trunc);
		if (! file)
		{
			std::cerr << "WARNING: could not write database cache '" << tmp << "'" << std::endl;
			return false;
		}
		file << ss.str();
		if (! file)
		{
			std::cerr << "WARNING: failed writing database cache '" << tmp << "'" << std::endl;
			return false;
		}
	}
	if (std::rename(tmp.c_str(), m_CachePath.c_str()))
	{
		std::cerr << "WARNING: could not replace database cache '" << m_CachePath << "'" << std::endl;
		std::remove(tmp.c_str());
		return false;
	}
	return true;
}

bool CLookup::Utility(Eaction action, Esource source)
{
	std::stringstream ss;
	LoadParameters();
	auto rval = (Esource::http == source) ? LoadContentHttp(ss) : LoadContentFile(ss);
	if (rval)
		UpdateContent(ss, action);
	return rval;
}
