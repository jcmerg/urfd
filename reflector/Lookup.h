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

#pragma once

#include <atomic>
#include <future>
#include <iostream>
#include <string>
#include <vector>
#include "Callsign.h"
#include "Configure.h"

// Databases are published with different field separators depending on the source
// (xlxapi uses ';', radioid.net ',', Pi-Star a tab). Field order is the same, so
// detecting the separator per line is enough to consume any of them.
char LookupDelimiter(const std::string &line);
std::vector<std::string> LookupSplitFields(const std::string &line, char delim);

enum class Eaction { normal, parse, error_only };
enum class Esource { http, file };

////////////////////////////////////////////////////////////////////////////////////////

class CLookup
{
public:
	// constructor
	CLookup() : keep_running(true), m_LastLoadTime(0) {}

	void LookupInit();
	void LookupClose();

	// locks
	void Lock(void)   { m_Mutex.lock();   }
	void Unlock(void) { m_Mutex.unlock(); }
	bool Utility(Eaction action, Esource source);

protected:
	std::time_t GetLastModTime();
	virtual void LoadParameters() = 0;
	virtual void ClearContents()  = 0;
	void Thread();

	// refresh
	bool LoadContentHttp(std::stringstream &ss);
	bool LoadContentFile(std::stringstream &ss);
	bool LoadContentFile(const std::string &path, std::stringstream &ss);
	// The download is mirrored to m_CachePath so a database outage does not leave
	// the map empty. m_Path is not usable for this: in ERefreshType::both it holds
	// the operator's own additional entries and must never be overwritten.
	bool SaveHttpCache(const std::stringstream &ss);
	std::vector<std::string> SplitUrls() const;
	virtual void UpdateContent(std::stringstream &ss, Eaction action) = 0;

	std::mutex        m_Mutex;
	ERefreshType      m_Type;
	unsigned          m_Refresh;
	std::string       m_Path, m_Url, m_CachePath;
	std::time_t       m_LastLoadTime;
	bool              m_ContentLoaded = false;

	std::atomic<bool> keep_running;
	std::future<void> m_Future;
};
