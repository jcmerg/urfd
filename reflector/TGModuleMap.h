#pragma once

// TGModuleMap -- Maps DMR Talkgroups to URFD Modules and vice versa
// Supports both static (from config) and dynamic (runtime) TG mappings.
// Dynamic entries have a TTL and expire after inactivity.
// Multiple TGs can map to the same module (multi-TG):
//   - Primary TG: used for outbound traffic (must be static)
//   - Secondary TGs: inbound only, always dynamic with TTL

#include <map>
#include <set>
#include <string>
#include <cstdint>
#include <mutex>
#include <chrono>
#include <vector>

struct STGMapEntry
{
	char module;
	uint8_t timeslot;  // 1 or 2
	bool is_static;    // true = from config, false = dynamic (has TTL)
	bool is_primary;   // true = used for outbound on this module
	std::chrono::steady_clock::time_point expires;  // only used when !is_static
};

class CTGModuleMap
{
public:
	CTGModuleMap();

	// Load TG=Module[,TS<1|2>] mappings from config (static entries)
	bool LoadFromConfig(void);
	void ReloadStaticFromConfig(void);  // re-read static mappings, preserve dynamic

	// Lookups (thread-safe)
	char TGToModule(uint32_t tg) const;
	uint32_t ModuleToTG(char module) const;  // returns primary TG only
	uint8_t TGToTimeslot(uint32_t tg) const;
	uint8_t ModuleToTimeslot(char module) const;

	// Check if a TG is configured
	bool IsTGMapped(uint32_t tg) const;

	// Check if a module has a primary TG
	bool IsModuleInUse(char module) const;

	// Dynamic TG management (thread-safe)
	// AddDynamic: adds as primary if module is free, as secondary if module has a primary
	bool AddDynamic(uint32_t tg, char module, uint8_t timeslot, int ttlSeconds);
	bool RemoveDynamic(uint32_t tg);
	void RefreshActivity(uint32_t tg);  // reset TTL on traffic
	void RefreshActivityByModule(char module);  // reset TTL for all dynamic TGs on module

	// Expire old dynamic entries, returns list of expired TGs
	std::vector<uint32_t> PurgeExpired(void);

	// Generate BM options string (includes all TGs: primary + secondary)
	std::string GetOptionsString(void) const;

	// List all mappings (for admin API)
	struct STGInfo
	{
		uint32_t tg;
		char module;
		uint8_t timeslot;
		bool is_static;
		bool is_primary;
		int remainingSeconds;  // -1 for static
	};
	std::vector<STGInfo> GetAllMappings(void) const;

	// Iterators for access
	const std::map<uint32_t, STGMapEntry> &GetTGMap(void) const { return m_TGtoEntry; }

private:
	mutable std::mutex m_Mutex;
	std::map<uint32_t, STGMapEntry> m_TGtoEntry;       // TG -> entry (all TGs)
	std::map<char, uint32_t> m_ModuleToTG;              // module -> primary TG only
	std::map<char, std::set<uint32_t>> m_ModuleToAllTGs; // module -> all TGs (primary + secondary)
};
