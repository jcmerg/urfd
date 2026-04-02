#include <iostream>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "BrandMeisterApi.h"

void CBrandMeisterApi::Configure(const std::string &apiKey, uint32_t dmrId)
{
	m_ApiKey = apiKey;
	m_DmrId = dmrId;
	if (IsConfigured())
		std::cout << "BrandMeister API: configured for DMR ID " << m_DmrId << std::endl;
}

size_t CBrandMeisterApi::WriteCallback(void *buf, size_t size, size_t nmemb, void *userp)
{
	auto *s = static_cast<std::string *>(userp);
	s->append(static_cast<char *>(buf), size * nmemb);
	return size * nmemb;
}

long CBrandMeisterApi::DoRequest(const std::string &method, const std::string &path,
                                  const std::string &body, std::string &response)
{
	CURL *curl = curl_easy_init();
	if (!curl) return 0;

	std::string url = std::string(BASE_URL) + path;
	std::string authHeader = "Authorization: Bearer " + m_ApiKey;

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, authHeader.c_str());
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	if (method == "POST")
	{
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
	}
	else if (method == "DELETE")
	{
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	}
	// else GET (default)

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	if (res == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	else
		std::cerr << "BrandMeister API: curl error: " << curl_easy_strerror(res) << " for " << method << " " << url << std::endl;

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return httpCode;
}

bool CBrandMeisterApi::GetStaticTGs(std::vector<BMTalkgroup> &tgs)
{
	if (!IsConfigured()) return false;

	std::string response;
	std::string path = "/device/" + std::to_string(m_DmrId) + "/talkgroup";
	long code = DoRequest("GET", path, "", response);

	if (code != 200)
	{
		std::cerr << "BrandMeister API: GET talkgroups failed (HTTP " << code << ")" << std::endl;
		return false;
	}

	try
	{
		auto arr = nlohmann::json::parse(response);
		for (const auto &item : arr)
		{
			BMTalkgroup tg;
			// BM API returns talkgroup and slot as strings
			if (item["talkgroup"].is_string())
				tg.talkgroup = std::stoul(item["talkgroup"].get<std::string>());
			else
				tg.talkgroup = item.value("talkgroup", 0U);
			if (item["slot"].is_string())
				tg.slot = (uint8_t)std::stoul(item["slot"].get<std::string>());
			else
				tg.slot = item.value("slot", (uint8_t)0);
			if (tg.talkgroup > 0)
				tgs.push_back(tg);
		}
	}
	catch (const std::exception &e)
	{
		std::cerr << "BrandMeister API: JSON parse error: " << e.what() << std::endl;
		return false;
	}

	return true;
}

bool CBrandMeisterApi::AddStaticTG(uint32_t tg, uint8_t slot)
{
	if (!IsConfigured()) return false;

	nlohmann::json body;
	body["slot"] = slot;
	body["group"] = tg;

	std::string response;
	std::string path = "/device/" + std::to_string(m_DmrId) + "/talkgroup";
	long code = DoRequest("POST", path, body.dump(), response);

	if (code >= 200 && code < 300)
	{
		std::cout << "BrandMeister API: added static TG" << tg << " slot " << (int)slot << std::endl;
		return true;
	}

	std::cerr << "BrandMeister API: add TG" << tg << " failed (HTTP " << code << "): " << response << std::endl;
	return false;
}

bool CBrandMeisterApi::RemoveStaticTG(uint32_t tg, uint8_t slot)
{
	if (!IsConfigured()) return false;

	std::string response;
	std::string path = "/device/" + std::to_string(m_DmrId) + "/talkgroup/" + std::to_string(slot) + "/" + std::to_string(tg);
	long code = DoRequest("DELETE", path, "", response);

	if (code >= 200 && code < 300)
	{
		std::cout << "BrandMeister API: removed static TG" << tg << " slot " << (int)slot << std::endl;
		return true;
	}

	std::cerr << "BrandMeister API: remove TG" << tg << " failed (HTTP " << code << "): " << response << std::endl;
	return false;
}

bool CBrandMeisterApi::DropDynamicTGs(uint8_t slot)
{
	if (!IsConfigured()) return false;

	std::string response;
	std::string path = "/device/" + std::to_string(m_DmrId) + "/action/dropDynamicGroups/" + std::to_string(slot);
	long code = DoRequest("GET", path, "", response);

	if (code >= 200 && code < 300)
	{
		std::cout << "BrandMeister API: dropped dynamic TGs on slot " << (int)slot << std::endl;
		return true;
	}

	std::cerr << "BrandMeister API: drop dynamic TGs failed (HTTP " << code << ")" << std::endl;
	return false;
}
