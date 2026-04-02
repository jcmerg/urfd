#pragma once

// BrandMeister REST API v2 client for static talkgroup management.
// Optional — only active when an API key is configured.

#include <string>
#include <vector>
#include <cstdint>

struct BMTalkgroup
{
	uint32_t talkgroup;
	uint8_t  slot;  // 0=simplex, 1=TS1, 2=TS2
};

class CBrandMeisterApi
{
public:
	void Configure(const std::string &apiKey, uint32_t dmrId);
	bool IsConfigured() const { return !m_ApiKey.empty() && m_DmrId != 0; }

	// Get all static TGs currently registered on this device
	bool GetStaticTGs(std::vector<BMTalkgroup> &tgs);

	// Add a static TG
	bool AddStaticTG(uint32_t tg, uint8_t slot);

	// Remove a static TG
	bool RemoveStaticTG(uint32_t tg, uint8_t slot);

	// Drop all dynamic TGs on a slot
	bool DropDynamicTGs(uint8_t slot);

private:
	// HTTP request with Bearer auth, returns HTTP status code (0 on error)
	long DoRequest(const std::string &method, const std::string &path,
	               const std::string &body, std::string &response);

	std::string m_ApiKey;
	uint32_t    m_DmrId = 0;
	static constexpr const char *BASE_URL = "https://api.brandmeister.network/v2";

	static size_t WriteCallback(void *buf, size_t size, size_t nmemb, void *userp);
};
