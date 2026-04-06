// TGModuleMap -- Maps DMR Talkgroups to URFD Modules and vice versa
// Part of the MMDVMClient protocol extension for urfd

#include <iostream>
#include <sstream>
#include "Global.h"
#include "TGModuleMap.h"

CTGModuleMap::CTGModuleMap()
{
}

bool CTGModuleMap::LoadFromConfig(const std::string &prefix)
{
	const std::string keyPrefix = prefix + "TG";
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_TGtoEntry.clear();
	m_ModuleToTG.clear();
	m_ModuleToAllTGs.clear();

	const auto &jdata = g_Configure.GetData();
	int matchCount = 0;
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, keyPrefix.size()) == keyPrefix)
		{
			matchCount++;
		}
	}
	std::cout << prefix << " TG: scanning " << jdata.size() << " config keys for prefix '" << keyPrefix << "', found " << matchCount << " match(es)" << std::endl;
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, keyPrefix.size()) == keyPrefix)
		{
			try
			{
				uint32_t tg = std::stoul(key.substr(keyPrefix.size()));
				std::string val = it.value().get<std::string>();

				char mod = ' ';
				uint8_t ts = 2;

				if (val.size() >= 1 && val[0] >= 'A' && val[0] <= 'Z')
					mod = val[0];

				auto comma = val.find(',');
				if (comma != std::string::npos)
				{
					std::string tsStr = val.substr(comma + 1);
					if (tsStr == "TS1" || tsStr == "ts1")
						ts = 1;
					else if (tsStr == "TS2" || tsStr == "ts2")
						ts = 2;
				}

				if (mod >= 'A' && mod <= 'Z')
				{
					// First TG for a module becomes primary, additional ones become secondary
					bool isPrimary = (m_ModuleToTG.find(mod) == m_ModuleToTG.end());

					m_TGtoEntry[tg] = { mod, ts, true, isPrimary, {} };
					m_ModuleToAllTGs[mod].insert(tg);

					if (isPrimary)
					{
						m_ModuleToTG[mod] = tg;
						std::cout << prefix << " TG mapping: TG" << tg << " <-> Module " << mod << " on TS" << (int)ts << " (primary)" << std::endl;
					}
					else
					{
						std::cout << prefix << " TG mapping: TG" << tg << " -> Module " << mod << " on TS" << (int)ts << " (secondary, inbound only)" << std::endl;
					}
				}
				else
				{
					std::cerr << "MMDVMClient: invalid module '" << val << "' for TG" << tg << std::endl;
				}
			}
			catch (const std::exception &e)
			{
				std::cerr << "MMDVMClient: failed to parse TG mapping key '" << key << "': " << e.what() << std::endl;
			}
		}
	}

	if (m_TGtoEntry.empty())
		std::cout << "MMDVMClient: no static TG mappings configured (dynamic mappings can be added via admin API)" << std::endl;

	return true;
}

void CTGModuleMap::ReloadStaticFromConfig(const std::string &prefix)
{
	const std::string keyPrefix = prefix + "TG";
	std::lock_guard<std::mutex> lock(m_Mutex);

	// Collect dynamic entries to preserve
	std::vector<std::tuple<uint32_t, STGMapEntry>> dynEntries;
	for (auto &pair : m_TGtoEntry)
	{
		if (!pair.second.is_static)
			dynEntries.emplace_back(pair.first, pair.second);
	}

	// Clear and reload static from config
	m_TGtoEntry.clear();
	m_ModuleToTG.clear();
	m_ModuleToAllTGs.clear();

	const auto &jdata = g_Configure.GetData();
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, keyPrefix.size()) != keyPrefix) continue;
		try
		{
			uint32_t tg = std::stoul(key.substr(keyPrefix.size()));
			std::string val = it.value().get<std::string>();
			char mod = (val.size() >= 1 && val[0] >= 'A' && val[0] <= 'Z') ? val[0] : ' ';
			uint8_t ts = 2;
			auto comma = val.find(',');
			if (comma != std::string::npos)
			{
				std::string tsStr = val.substr(comma + 1);
				if (tsStr == "TS1" || tsStr == "ts1") ts = 1;
			}
			if (mod >= 'A' && mod <= 'Z')
			{
				bool isPrimary = (m_ModuleToTG.find(mod) == m_ModuleToTG.end());
				m_TGtoEntry[tg] = { mod, ts, true, isPrimary, {} };
				m_ModuleToAllTGs[mod].insert(tg);
				if (isPrimary)
					m_ModuleToTG[mod] = tg;
			}
		}
		catch (...) {}
	}

	// Re-add dynamic entries (skip if TG now exists as static)
	for (auto &[tg, entry] : dynEntries)
	{
		if (m_TGtoEntry.find(tg) != m_TGtoEntry.end())
			continue;  // now static, skip
		bool isPrimary = (m_ModuleToTG.find(entry.module) == m_ModuleToTG.end());
		entry.is_primary = isPrimary;
		m_TGtoEntry[tg] = entry;
		m_ModuleToAllTGs[entry.module].insert(tg);
		if (isPrimary)
			m_ModuleToTG[entry.module] = tg;
	}

	std::cout << "MMDVMClient: TG mappings reloaded (" << m_TGtoEntry.size() << " total)" << std::endl;
}

char CTGModuleMap::TGToModule(uint32_t tg) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	auto it = m_TGtoEntry.find(tg);
	if (it != m_TGtoEntry.end())
		return it->second.module;
	return ' ';
}

uint32_t CTGModuleMap::ModuleToTG(char module) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	auto it = m_ModuleToTG.find(module);
	if (it != m_ModuleToTG.end())
		return it->second;
	return 0;
}

uint8_t CTGModuleMap::TGToTimeslot(uint32_t tg) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	auto it = m_TGtoEntry.find(tg);
	if (it != m_TGtoEntry.end())
		return it->second.timeslot;
	return 2;
}

uint8_t CTGModuleMap::ModuleToTimeslot(char module) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	uint32_t tg = 0;
	auto it = m_ModuleToTG.find(module);
	if (it != m_ModuleToTG.end())
		tg = it->second;
	if (tg != 0)
	{
		auto it2 = m_TGtoEntry.find(tg);
		if (it2 != m_TGtoEntry.end())
			return it2->second.timeslot;
	}
	return 2;
}

bool CTGModuleMap::IsTGMapped(uint32_t tg) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_TGtoEntry.find(tg) != m_TGtoEntry.end();
}

bool CTGModuleMap::IsModuleInUse(char module) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_ModuleToTG.find(module) != m_ModuleToTG.end();
}

bool CTGModuleMap::AddDynamic(uint32_t tg, char module, uint8_t timeslot, int ttlSeconds)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);

	// Check if TG is already mapped
	auto tgIt = m_TGtoEntry.find(tg);
	if (tgIt != m_TGtoEntry.end())
	{
		if (tgIt->second.is_static)
		{
			std::cerr << "Admin: TG" << tg << " is statically mapped, cannot override" << std::endl;
			return false;
		}
		// Already dynamic — update TTL
		tgIt->second.expires = expiry;
		std::cout << "Admin: refreshed dynamic TG" << tg << " TTL=" << ttlSeconds << "s" << std::endl;
		return true;
	}

	// Check if module has a primary TG
	auto modIt = m_ModuleToTG.find(module);
	if (modIt != m_ModuleToTG.end())
	{
		// Module has a primary — add as secondary (inbound only)
		m_TGtoEntry[tg] = { module, timeslot, false, false, expiry };
		m_ModuleToAllTGs[module].insert(tg);
		std::cout << "Admin: added secondary TG" << tg << " -> Module " << module
		          << " TS" << (int)timeslot << " TTL=" << ttlSeconds << "s (inbound only)" << std::endl;
		return true;
	}

	// Module is free — add as primary
	m_TGtoEntry[tg] = { module, timeslot, false, true, expiry };
	m_ModuleToTG[module] = tg;
	m_ModuleToAllTGs[module].insert(tg);
	std::cout << "Admin: added dynamic TG" << tg << " -> Module " << module
	          << " TS" << (int)timeslot << " TTL=" << ttlSeconds << "s (primary)" << std::endl;
	return true;
}

bool CTGModuleMap::RemoveDynamic(uint32_t tg)
{
	std::lock_guard<std::mutex> lock(m_Mutex);

	auto it = m_TGtoEntry.find(tg);
	if (it == m_TGtoEntry.end())
		return false;
	if (it->second.is_static)
	{
		std::cerr << "Admin: cannot remove static TG" << tg << std::endl;
		return false;
	}

	char mod = it->second.module;
	bool wasPrimary = it->second.is_primary;

	// Remove from maps
	m_TGtoEntry.erase(it);
	m_ModuleToAllTGs[mod].erase(tg);
	if (m_ModuleToAllTGs[mod].empty())
		m_ModuleToAllTGs.erase(mod);

	if (wasPrimary)
	{
		m_ModuleToTG.erase(mod);
		// Don't promote secondaries — they are inbound-only and have no outbound TG
		std::cout << "Admin: removed primary dynamic TG" << tg << " from Module " << mod << std::endl;
	}
	else
	{
		std::cout << "Admin: removed secondary dynamic TG" << tg << " from Module " << mod << std::endl;
	}
	return true;
}

void CTGModuleMap::RefreshActivity(uint32_t tg)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	auto it = m_TGtoEntry.find(tg);
	if (it != m_TGtoEntry.end() && !it->second.is_static)
	{
		auto newExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(900);
		if (newExpiry > it->second.expires)
			it->second.expires = newExpiry;
	}
}

void CTGModuleMap::RefreshActivityByModule(char module)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	auto modIt = m_ModuleToAllTGs.find(module);
	if (modIt == m_ModuleToAllTGs.end())
		return;
	auto newExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(900);
	for (uint32_t tg : modIt->second)
	{
		auto it = m_TGtoEntry.find(tg);
		if (it != m_TGtoEntry.end() && !it->second.is_static && newExpiry > it->second.expires)
			it->second.expires = newExpiry;
	}
}

std::vector<uint32_t> CTGModuleMap::PurgeExpired(void)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	std::vector<uint32_t> expired;
	auto now = std::chrono::steady_clock::now();

	for (auto it = m_TGtoEntry.begin(); it != m_TGtoEntry.end(); )
	{
		if (!it->second.is_static && now >= it->second.expires)
		{
			uint32_t tg = it->first;
			char mod = it->second.module;
			bool wasPrimary = it->second.is_primary;

			m_ModuleToAllTGs[mod].erase(tg);
			if (m_ModuleToAllTGs[mod].empty())
				m_ModuleToAllTGs.erase(mod);

			if (wasPrimary)
				m_ModuleToTG.erase(mod);

			it = m_TGtoEntry.erase(it);
			expired.push_back(tg);

			std::cout << "Admin: dynamic TG" << tg << " expired ("
			          << (wasPrimary ? "primary" : "secondary")
			          << " on Module " << mod << ")" << std::endl;
		}
		else
		{
			++it;
		}
	}
	return expired;
}

std::string CTGModuleMap::GetOptionsString(void) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	// Includes ALL TGs (primary + secondary) for BM subscription
	std::ostringstream oss;
	int idxTS1 = 0, idxTS2 = 0;
	bool first = true;
	for (const auto &pair : m_TGtoEntry)
	{
		if (!first)
			oss << ";";
		first = false;
		int idx = (pair.second.timeslot == 1) ? ++idxTS1 : ++idxTS2;
		oss << "TS" << (int)pair.second.timeslot << "_" << idx << "=" << pair.first;
	}
	return oss.str();
}

std::vector<CTGModuleMap::STGInfo> CTGModuleMap::GetAllMappings(void) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	std::vector<STGInfo> result;
	auto now = std::chrono::steady_clock::now();

	for (const auto &pair : m_TGtoEntry)
	{
		int remaining = -1;
		if (!pair.second.is_static)
			remaining = (int)std::chrono::duration_cast<std::chrono::seconds>(pair.second.expires - now).count();

		result.push_back({
			pair.first,
			pair.second.module,
			pair.second.timeslot,
			pair.second.is_static,
			pair.second.is_primary,
			remaining
		});
	}
	return result;
}
