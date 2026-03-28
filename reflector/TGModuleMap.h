#pragma once

// TGModuleMap -- Maps DMR Talkgroups to URFD Modules and vice versa
// Part of the MMDVMClient protocol extension for urfd

#include <map>
#include <string>
#include <cstdint>

struct STGMapEntry
{
	char module;
	uint8_t timeslot;  // 1 or 2
};

class CTGModuleMap
{
public:
	CTGModuleMap();

	// Load TG=Module[,TS<1|2>] mappings from config
	bool LoadFromConfig(void);

	// Lookups
	char TGToModule(uint32_t tg) const;
	uint32_t ModuleToTG(char module) const;
	uint8_t TGToTimeslot(uint32_t tg) const;
	uint8_t ModuleToTimeslot(char module) const;

	// Check if a TG is configured
	bool IsTGMapped(uint32_t tg) const;

	// Generate BM options string for static TG subscription
	std::string GetOptionsString(void) const;

	// Iterators for access
	const std::map<uint32_t, STGMapEntry> &GetTGMap(void) const { return m_TGtoEntry; }

private:
	std::map<uint32_t, STGMapEntry> m_TGtoEntry;
	std::map<char, uint32_t> m_ModuleToTG;
};
