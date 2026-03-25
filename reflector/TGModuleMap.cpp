// TGModuleMap -- Maps DMR Talkgroups to URFD Modules and vice versa
// Part of the BMHomebrew protocol extension for urfd

#include <iostream>
#include <sstream>
#include "Global.h"
#include "TGModuleMap.h"

CTGModuleMap::CTGModuleMap()
{
}

bool CTGModuleMap::LoadFromConfig(void)
{
	m_TGtoEntry.clear();
	m_ModuleToTG.clear();

	// Read TG mappings from the JSON config
	// Format: "bmhbTG<number>" = "<module>[,TS<1|2>]"
	// e.g. "bmhbTG26363" = "F" or "bmhbTG26363" = "F,TS2"
	const auto &jdata = g_Configure.GetData();
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, 6) == "bmhbTG")
		{
			try
			{
				uint32_t tg = std::stoul(key.substr(6));
				std::string val = it.value().get<std::string>();

				// Parse "F" or "F,TS2"
				char mod = ' ';
				uint8_t ts = 2;  // default TS2

				if (val.size() >= 1 && val[0] >= 'A' && val[0] <= 'Z')
				{
					mod = val[0];
				}

				// Check for ,TS1 or ,TS2
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
					m_TGtoEntry[tg] = { mod, ts };
					m_ModuleToTG[mod] = tg;
					std::cout << "BMHomebrew TG mapping: TG" << tg << " <-> Module " << mod << " on TS" << (int)ts << std::endl;
				}
				else
				{
					std::cerr << "BMHomebrew: invalid module '" << val << "' for TG" << tg << std::endl;
				}
			}
			catch (const std::exception &e)
			{
				std::cerr << "BMHomebrew: failed to parse TG mapping key '" << key << "': " << e.what() << std::endl;
			}
		}
	}

	if (m_TGtoEntry.empty())
	{
		std::cerr << "BMHomebrew: no TG mappings configured!" << std::endl;
		return false;
	}

	return true;
}

char CTGModuleMap::TGToModule(uint32_t tg) const
{
	auto it = m_TGtoEntry.find(tg);
	if (it != m_TGtoEntry.end())
		return it->second.module;
	return ' ';
}

uint32_t CTGModuleMap::ModuleToTG(char module) const
{
	auto it = m_ModuleToTG.find(module);
	if (it != m_ModuleToTG.end())
		return it->second;
	return 0;
}

uint8_t CTGModuleMap::TGToTimeslot(uint32_t tg) const
{
	auto it = m_TGtoEntry.find(tg);
	if (it != m_TGtoEntry.end())
		return it->second.timeslot;
	return 2;
}

uint8_t CTGModuleMap::ModuleToTimeslot(char module) const
{
	uint32_t tg = ModuleToTG(module);
	if (tg != 0)
		return TGToTimeslot(tg);
	return 2;
}

bool CTGModuleMap::IsTGMapped(uint32_t tg) const
{
	return m_TGtoEntry.find(tg) != m_TGtoEntry.end();
}

std::string CTGModuleMap::GetOptionsString(void) const
{
	// Generate BM-compatible static TG subscription options
	// Format: "TS<slot>_<idx>=<tg>;..."
	std::ostringstream oss;
	int idx = 1;
	for (const auto &pair : m_TGtoEntry)
	{
		if (idx > 1)
			oss << ";";
		oss << "TS" << (int)pair.second.timeslot << "_" << idx << "=" << pair.first;
		idx++;
	}
	return oss.str();
}
