#pragma once

// AdminSocket -- TCP-based JSON admin interface for runtime management
// Provides TG add/remove/list, gain control, stats, and status queries.
// Protected by password authentication with session tokens.

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <future>
#include <thread>
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

	// Block commands
	nlohmann::json CmdBlock(const nlohmann::json &cmd);
	nlohmann::json CmdUnblock(const nlohmann::json &cmd);
	nlohmann::json CmdBlockReset(void);

	// DCSClient mapping commands
	nlohmann::json CmdDcsMapAdd(const nlohmann::json &cmd);
	nlohmann::json CmdDcsMapRemove(const nlohmann::json &cmd);
	nlohmann::json CmdDcsMapList(void);

	// DExtraClient mapping commands
	nlohmann::json CmdDExtraMapAdd(const nlohmann::json &cmd);
	nlohmann::json CmdDExtraMapRemove(const nlohmann::json &cmd);
	nlohmann::json CmdDExtraMapList(void);

	// DPlusClient mapping commands
	nlohmann::json CmdDPlusMapAdd(const nlohmann::json &cmd);
	nlohmann::json CmdDPlusMapRemove(const nlohmann::json &cmd);
	nlohmann::json CmdDPlusMapList(void);

	// YSFClient mapping commands
	nlohmann::json CmdYsfMapAdd(const nlohmann::json &cmd);
	nlohmann::json CmdYsfMapRemove(const nlohmann::json &cmd);
	nlohmann::json CmdYsfMapList(void);

	// SVX Server user management
	nlohmann::json CmdSvxsUserAdd(const nlohmann::json &cmd);
	nlohmann::json CmdSvxsUserRemove(const nlohmann::json &cmd);
	nlohmann::json CmdSvxsUserList(void);
	nlohmann::json CmdSvxsPeerList(void);

	// MMDVM Server user management
	nlohmann::json CmdMmdvmUserAdd(const nlohmann::json &cmd);
	nlohmann::json CmdMmdvmUserRemove(const nlohmann::json &cmd);
	nlohmann::json CmdMmdvmUserList(void);

	// MMDVM Server TG management
	nlohmann::json CmdMmdvmTGAdd(const nlohmann::json &cmd);
	nlohmann::json CmdMmdvmTGRemove(const nlohmann::json &cmd);
	nlohmann::json CmdMmdvmTGList(void);

	// MMDVM Server peer list
	nlohmann::json CmdMmdvmPeerList(void);

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
