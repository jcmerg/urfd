#pragma once

// AdminSocket -- TCP-based JSON admin interface for runtime management
// Provides TG add/remove/list, gain control, stats, and status queries.
// Protected by password authentication with session tokens.

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <future>
#include <nlohmann/json.hpp>

class CAdminSocket
{
public:
	CAdminSocket();
	~CAdminSocket();

	bool Start(void);
	void Stop(void);

private:
	void ListenThread(void);
	void ClientThread(int clientFd, const std::string &clientAddr);

	// Command dispatch
	nlohmann::json HandleCommand(const nlohmann::json &cmd, const std::string &clientAddr);

	// Auth
	nlohmann::json CmdAuth(const nlohmann::json &cmd);
	bool ValidateToken(const std::string &token) const;
	std::string GenerateToken(void);

	// TG commands
	nlohmann::json CmdTGAdd(const nlohmann::json &cmd);
	nlohmann::json CmdTGRemove(const nlohmann::json &cmd);
	nlohmann::json CmdTGList(const nlohmann::json &cmd);

	// Status commands
	nlohmann::json CmdStatus(void);
	nlohmann::json CmdReconnect(const nlohmann::json &cmd);
	nlohmann::json CmdTCStats(void);
	nlohmann::json CmdLog(const nlohmann::json &cmd);
	nlohmann::json CmdKerchunk(const nlohmann::json &cmd);

	// Config
	int m_ListenFd;
	uint16_t m_Port;
	std::string m_Password;
	std::string m_BindAddress;

	// Auth tokens
	mutable std::mutex m_TokenMutex;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_Tokens;

	// Thread
	std::atomic<bool> m_Running;
	std::future<void> m_ListenFuture;
};
