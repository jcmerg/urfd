// AdminSocket -- TCP-based JSON admin interface for runtime management

#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <random>
#include <iomanip>

#include "Global.h"
#include "AdminSocket.h"
#include "LogBuffer.h"
#include "MMDVMClientProtocol.h"
#include "SvxReflectorProtocol.h"
#include "DCSClientProtocol.h"
#include "DExtraClientProtocol.h"
#include "DPlusClientProtocol.h"
#include "YSFClientProtocol.h"

#define ADMIN_TOKEN_TTL_SECONDS   3600   // 1 hour session
#define ADMIN_MAX_MSG_SIZE        8192

static const std::map<std::string, EProtocol> s_NameToProto = {
	{"MMDVMClient", EProtocol::mmdvmclient}, {"SVX", EProtocol::svxreflector},
	{"DCSClient", EProtocol::dcsclient}, {"DExtraClient", EProtocol::dextraclient},
	{"DPlusClient", EProtocol::dplusclient}, {"YSFClient", EProtocol::ysfclient},
	{"DExtra", EProtocol::dextra}, {"DPlus", EProtocol::dplus},
	{"DCS", EProtocol::dcs}, {"DMRPlus", EProtocol::dmrplus},
	{"MMDVM", EProtocol::dmrmmdvm}, {"YSF", EProtocol::ysf},
	{"M17", EProtocol::m17}, {"NXDN", EProtocol::nxdn},
	{"P25", EProtocol::p25}, {"USRP", EProtocol::usrp},
	{"URF", EProtocol::urf}, {"XLXPeer", EProtocol::xlxpeer},
	{"G3", EProtocol::g3},
};

static const std::map<EProtocol, std::string> s_ProtoToName = {
	{EProtocol::mmdvmclient, "MMDVMClient"}, {EProtocol::svxreflector, "SVX"},
	{EProtocol::dcsclient, "DCSClient"}, {EProtocol::dextraclient, "DExtraClient"},
	{EProtocol::dplusclient, "DPlusClient"}, {EProtocol::ysfclient, "YSFClient"},
	{EProtocol::dextra, "DExtra"}, {EProtocol::dplus, "DPlus"},
	{EProtocol::dcs, "DCS"}, {EProtocol::dmrplus, "DMRPlus"},
	{EProtocol::dmrmmdvm, "MMDVM"}, {EProtocol::ysf, "YSF"},
	{EProtocol::m17, "M17"}, {EProtocol::nxdn, "NXDN"},
	{EProtocol::p25, "P25"}, {EProtocol::usrp, "USRP"},
	{EProtocol::urf, "URF"}, {EProtocol::xlxpeer, "XLXPeer"},
	{EProtocol::g3, "G3"},
};

CAdminSocket::CAdminSocket()
	: m_ListenFd(-1)
	, m_Port(10101)
	, m_Running(false)
{
}

CAdminSocket::~CAdminSocket()
{
	Stop();
}

bool CAdminSocket::Start(void)
{
	if (!g_Configure.Contains(g_Keys.admin.enable) || !g_Configure.GetBoolean(g_Keys.admin.enable))
	{
		std::cout << "Admin: disabled" << std::endl;
		return true;
	}

	m_Port = (uint16_t)g_Configure.GetUnsigned(g_Keys.admin.port);
	m_Password = g_Configure.GetString(g_Keys.admin.password);

	m_BindAddress = "127.0.0.1";
	if (g_Configure.Contains(g_Keys.admin.bindaddress))
		m_BindAddress = g_Configure.GetString(g_Keys.admin.bindaddress);

	// Create listening socket
	m_ListenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (m_ListenFd < 0)
	{
		std::cerr << "Admin: failed to create socket: " << strerror(errno) << std::endl;
		return false;
	}

	int opt = 1;
	setsockopt(m_ListenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(m_Port);
	inet_pton(AF_INET, m_BindAddress.c_str(), &addr.sin_addr);

	if (bind(m_ListenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		std::cerr << "Admin: bind failed on " << m_BindAddress << ":" << m_Port << ": " << strerror(errno) << std::endl;
		close(m_ListenFd);
		m_ListenFd = -1;
		return false;
	}

	if (listen(m_ListenFd, 4) < 0)
	{
		std::cerr << "Admin: listen failed: " << strerror(errno) << std::endl;
		close(m_ListenFd);
		m_ListenFd = -1;
		return false;
	}

	// Runs in its own thread — does not block reflector audio processing
	m_Running = true;
	m_ListenFuture = std::async(std::launch::async, &CAdminSocket::ListenThread, this);

	std::cout << "Admin: listening on " << m_BindAddress << ":" << m_Port << std::endl;
	return true;
}

void CAdminSocket::Stop(void)
{
	m_Running = false;
	if (m_ListenFd >= 0)
	{
		shutdown(m_ListenFd, SHUT_RDWR);
		close(m_ListenFd);
		m_ListenFd = -1;
	}
	if (m_ListenFuture.valid())
		m_ListenFuture.get();
}

void CAdminSocket::ListenThread(void)
{
	while (m_Running)
	{
		struct pollfd pfd{};
		pfd.fd = m_ListenFd;
		pfd.events = POLLIN;

		int ret = poll(&pfd, 1, 1000);
		if (ret <= 0)
			continue;

		struct sockaddr_in clientAddr{};
		socklen_t addrLen = sizeof(clientAddr);
		int clientFd = accept(m_ListenFd, (struct sockaddr *)&clientAddr, &addrLen);
		if (clientFd < 0)
			continue;

		char addrStr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
		std::string addr(addrStr);

		// Handle client in the listen thread — commands are fast, no blocking
		ClientThread(clientFd, addr);
	}
}

void CAdminSocket::ClientThread(int clientFd, const std::string &clientAddr)
{
	struct timeval tv;
	tv.tv_sec = 30;
	tv.tv_usec = 0;
	setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	while (m_Running)
	{
		char buf[ADMIN_MAX_MSG_SIZE];
		ssize_t n = recv(clientFd, buf, sizeof(buf) - 1, 0);
		if (n <= 0)
			break;
		buf[n] = '\0';

		std::istringstream iss(buf);
		std::string line;
		while (std::getline(iss, line))
		{
			if (line.empty())
				continue;

			try
			{
				auto cmd = nlohmann::json::parse(line);
				auto response = HandleCommand(cmd, clientAddr);
				std::string respStr = response.dump() + "\n";
				send(clientFd, respStr.c_str(), respStr.size(), MSG_NOSIGNAL);
			}
			catch (const nlohmann::json::exception &)
			{
				nlohmann::json err = {{"status", "error"}, {"message", "invalid JSON"}};
				std::string respStr = err.dump() + "\n";
				send(clientFd, respStr.c_str(), respStr.size(), MSG_NOSIGNAL);
			}
		}
	}

	close(clientFd);
}

nlohmann::json CAdminSocket::HandleCommand(const nlohmann::json &cmd, const std::string &clientAddr)
{
	if (!cmd.contains("cmd"))
		return {{"status", "error"}, {"message", "missing 'cmd' field"}};

	std::string command = cmd["cmd"];

	// Auth does not require token
	if (command == "auth")
		return CmdAuth(cmd);

	// All other commands require valid token
	if (!cmd.contains("token") || !ValidateToken(cmd["token"]))
		return {{"status", "error"}, {"message", "authentication required"}};

	if (command == "tg_add")
		return CmdTGAdd(cmd);
	else if (command == "tg_remove")
		return CmdTGRemove(cmd);
	else if (command == "tg_list")
		return CmdTGList(cmd);
	else if (command == "status")
		return CmdStatus();
	else if (command == "reconnect")
		return CmdReconnect(cmd);
	else if (command == "tc_stats")
		return CmdTCStats();
	else if (command == "log")
		return CmdLog(cmd);
	else if (command == "kerchunk")
		return CmdKerchunk(cmd);
	else if (command == "block")
		return CmdBlock(cmd);
	else if (command == "unblock")
		return CmdUnblock(cmd);
	else if (command == "block_reset")
		return CmdBlockReset();
	else if (command == "dcs_map_add")
		return CmdDcsMapAdd(cmd);
	else if (command == "dcs_map_remove")
		return CmdDcsMapRemove(cmd);
	else if (command == "dcs_map_list")
		return CmdDcsMapList();
	else if (command == "dextra_map_add")
		return CmdDExtraMapAdd(cmd);
	else if (command == "dextra_map_remove")
		return CmdDExtraMapRemove(cmd);
	else if (command == "dextra_map_list")
		return CmdDExtraMapList();
	else if (command == "dplus_map_add")
		return CmdDPlusMapAdd(cmd);
	else if (command == "dplus_map_remove")
		return CmdDPlusMapRemove(cmd);
	else if (command == "dplus_map_list")
		return CmdDPlusMapList();
	else if (command == "ysf_map_add")
		return CmdYsfMapAdd(cmd);
	else if (command == "ysf_map_remove")
		return CmdYsfMapRemove(cmd);
	else if (command == "ysf_map_list")
		return CmdYsfMapList();

	return {{"status", "error"}, {"message", "unknown command: " + command}};
}

////////////////////////////////////////////////////////////////////////////////////////
// Auth

nlohmann::json CAdminSocket::CmdAuth(const nlohmann::json &cmd)
{
	if (!cmd.contains("password"))
		return {{"status", "error"}, {"message", "missing password"}};

	std::string pw = cmd["password"];
	if (pw != m_Password)
	{
		std::cerr << "Admin: authentication failed" << std::endl;
		return {{"status", "error"}, {"message", "invalid password"}};
	}

	std::string token = GenerateToken();

	{
		std::lock_guard<std::mutex> lock(m_TokenMutex);
		m_Tokens[token] = std::chrono::steady_clock::now() + std::chrono::seconds(ADMIN_TOKEN_TTL_SECONDS);

		// Purge expired tokens
		auto now = std::chrono::steady_clock::now();
		for (auto it = m_Tokens.begin(); it != m_Tokens.end(); )
		{
			if (now >= it->second)
				it = m_Tokens.erase(it);
			else
				++it;
		}
	}

	return {{"status", "ok"}, {"token", token}};
}

bool CAdminSocket::ValidateToken(const std::string &token) const
{
	std::lock_guard<std::mutex> lock(m_TokenMutex);
	auto it = m_Tokens.find(token);
	if (it == m_Tokens.end())
		return false;
	return std::chrono::steady_clock::now() < it->second;
}

std::string CAdminSocket::GenerateToken(void)
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint64_t> dis;
	std::ostringstream oss;
	oss << std::hex << std::setfill('0')
	    << std::setw(16) << dis(gen)
	    << std::setw(16) << dis(gen);
	return oss.str();
}

////////////////////////////////////////////////////////////////////////////////////////
// TG commands

nlohmann::json CAdminSocket::CmdTGAdd(const nlohmann::json &cmd)
{
	if (!cmd.contains("protocol") || !cmd.contains("tg") || !cmd.contains("module"))
		return {{"status", "error"}, {"message", "missing required fields: protocol, tg, module"}};

	std::string protocol = cmd["protocol"];
	uint32_t tg = cmd["tg"];
	std::string modStr = cmd["module"];
	if (modStr.empty() || modStr[0] < 'A' || modStr[0] > 'Z')
		return {{"status", "error"}, {"message", "module must be A-Z"}};
	char module = modStr[0];

	uint8_t timeslot = 2;
	if (cmd.contains("ts"))
		timeslot = cmd["ts"];

	int ttl = 900;  // default 15 min
	if (cmd.contains("ttl"))
		ttl = cmd["ttl"];

	// Validate module exists in reflector
	if (!g_Reflector.IsValidModule(module))
		return {{"status", "error"}, {"message", std::string("module ") + module + " is not configured"}};

	if (protocol == "mmdvmclient")
	{
		auto &protocols = g_Reflector.GetProtocols();
		protocols.Lock();
		auto *proto = protocols.FindByType(EProtocol::mmdvmclient);
		if (!proto)
		{
			protocols.Unlock();
			return {{"status", "error"}, {"message", "MMDVMClient protocol not active"}};
		}
		auto *mmdvm = static_cast<CMMDVMClientProtocol *>(proto);
		bool ok = mmdvm->GetTGMap().AddDynamic(tg, module, timeslot, ttl);
		if (ok)
		{
			if (mmdvm->GetBmApi().IsConfigured())
			{
				// Register as static on BM via API (no kerchunk needed)
				mmdvm->GetBmApi().AddStaticTG(tg, timeslot);
			}
			else
			{
				mmdvm->RequestKerchunk(tg);  // kerchunk to activate on BM
			}
			mmdvm->RequestReconnect();   // reconnect to update options string
		}
		protocols.Unlock();

		if (!ok)
			return {{"status", "error"}, {"message", "failed to add TG (module in use or static conflict)"}};

		std::string method = mmdvm->GetBmApi().IsConfigured() ? "API" : "kerchunk";
		return {{"status", "ok"}, {"message", "MMDVM TG" + std::to_string(tg) + " -> Module " + module + " added via " + method}};
	}
	else if (protocol == "svx")
	{
		auto &protocols = g_Reflector.GetProtocols();
		protocols.Lock();
		auto *proto = protocols.FindByType(EProtocol::svxreflector);
		if (!proto)
		{
			protocols.Unlock();
			return {{"status", "error"}, {"message", "SvxReflector protocol not active"}};
		}
		auto *svx = static_cast<CSvxReflectorProtocol *>(proto);
		bool ok = svx->AddDynamicTG(tg, module, ttl);
		protocols.Unlock();

		if (!ok)
			return {{"status", "error"}, {"message", "failed to add TG (module in use or conflict)"}};

		return {{"status", "ok"}, {"message", "SVX TG" + std::to_string(tg) + " -> Module " + module + " added"}};
	}

	return {{"status", "error"}, {"message", "unknown protocol: " + protocol}};
}

nlohmann::json CAdminSocket::CmdTGRemove(const nlohmann::json &cmd)
{
	if (!cmd.contains("protocol") || !cmd.contains("tg"))
		return {{"status", "error"}, {"message", "missing required fields: protocol, tg"}};

	std::string protocol = cmd["protocol"];
	uint32_t tg = cmd["tg"];

	if (protocol == "mmdvmclient")
	{
		auto &protocols = g_Reflector.GetProtocols();
		protocols.Lock();
		auto *proto = protocols.FindByType(EProtocol::mmdvmclient);
		if (!proto)
		{
			protocols.Unlock();
			return {{"status", "error"}, {"message", "MMDVMClient protocol not active"}};
		}
		auto *mmdvm = static_cast<CMMDVMClientProtocol *>(proto);
		// Get timeslot before removing (needed for BM API)
		uint8_t ts = 2;
		auto &tgmap = mmdvm->GetTGMap().GetTGMap();
		auto tgit = tgmap.find(tg);
		if (tgit != tgmap.end())
			ts = tgit->second.timeslot;
		bool ok = mmdvm->GetTGMap().RemoveDynamic(tg);
		if (ok)
		{
			if (mmdvm->GetBmApi().IsConfigured())
				mmdvm->GetBmApi().RemoveStaticTG(tg, ts);
			mmdvm->RequestReconnect();
		}
		protocols.Unlock();

		if (!ok)
			return {{"status", "error"}, {"message", "failed to remove TG (not found or static)"}};

		return {{"status", "ok"}, {"message", "MMDVM TG" + std::to_string(tg) + " removed"}};
	}
	else if (protocol == "svx")
	{
		auto &protocols = g_Reflector.GetProtocols();
		protocols.Lock();
		auto *proto = protocols.FindByType(EProtocol::svxreflector);
		if (!proto)
		{
			protocols.Unlock();
			return {{"status", "error"}, {"message", "SvxReflector protocol not active"}};
		}
		auto *svx = static_cast<CSvxReflectorProtocol *>(proto);
		bool ok = svx->RemoveDynamicTG(tg);
		protocols.Unlock();

		if (!ok)
			return {{"status", "error"}, {"message", "failed to remove TG (not found or static)"}};

		return {{"status", "ok"}, {"message", "SVX TG" + std::to_string(tg) + " removed"}};
	}

	return {{"status", "error"}, {"message", "unknown protocol: " + protocol}};
}

nlohmann::json CAdminSocket::CmdTGList(const nlohmann::json &cmd)
{
	std::string protocol = "all";
	if (cmd.contains("protocol"))
		protocol = cmd["protocol"];

	nlohmann::json result;
	result["status"] = "ok";
	result["mappings"] = nlohmann::json::array();

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();

	if (protocol == "all" || protocol == "mmdvmclient")
	{
		auto *proto = protocols.FindByType(EProtocol::mmdvmclient);
		if (proto)
		{
			auto *mmdvm = static_cast<CMMDVMClientProtocol *>(proto);
			auto mappings = mmdvm->GetTGMap().GetAllMappings();
			for (const auto &m : mappings)
			{
				result["mappings"].push_back({
					{"protocol", "mmdvmclient"},
					{"tg", m.tg},
					{"module", std::string(1, m.module)},
					{"ts", m.timeslot},
					{"static", m.is_static},
					{"primary", m.is_primary},
					{"remaining", m.remainingSeconds}
				});
			}
		}
	}

	if (protocol == "all" || protocol == "svx")
	{
		auto *proto = protocols.FindByType(EProtocol::svxreflector);
		if (proto)
		{
			auto *svx = static_cast<CSvxReflectorProtocol *>(proto);
			auto mappings = svx->GetTGMappings();
			for (const auto &m : mappings)
			{
				result["mappings"].push_back({
					{"protocol", "svx"},
					{"tg", m.tg},
					{"module", std::string(1, m.module)},
					{"static", m.is_static},
					{"primary", m.is_primary},
					{"remaining", m.remainingSeconds}
				});
			}
		}
	}

	protocols.Unlock();
	return result;
}

////////////////////////////////////////////////////////////////////////////////////////
// Status

nlohmann::json CAdminSocket::CmdStatus(void)
{
	nlohmann::json status;
	status["status"] = "ok";
	status["reflector"] = g_Configure.GetString(g_Keys.names.callsign);
	status["version"] = g_Version.GetVersion();
	status["modules"] = g_Reflector.GetModules();
	status["tc_modules"] = g_Configure.GetString(g_Keys.tc.modules);

	// Report which protocols are active and their block lists
	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();

	status["mmdvm_active"] = (protocols.FindByType(EProtocol::mmdvmclient) != nullptr);
	status["svx_active"] = (protocols.FindByType(EProtocol::svxreflector) != nullptr);
	status["dcsclient_active"] = (protocols.FindByType(EProtocol::dcsclient) != nullptr);
	status["dextraclient_active"] = (protocols.FindByType(EProtocol::dextraclient) != nullptr);
	status["dplusclient_active"] = (protocols.FindByType(EProtocol::dplusclient) != nullptr);
	status["ysfclient_active"] = (protocols.FindByType(EProtocol::ysfclient) != nullptr);

	// List all active protocol names
	nlohmann::json activeProtos = nlohmann::json::array();
	for (auto it = protocols.begin(); it != protocols.end(); ++it)
	{
		auto nameIt = s_ProtoToName.find((*it)->GetProtocolType());
		if (nameIt != s_ProtoToName.end())
			activeProtos.push_back(nameIt->second);
	}
	status["active_protocols"] = activeProtos;

	// Collect block rules
	nlohmann::json blocks = nlohmann::json::array();

	for (auto it = protocols.begin(); it != protocols.end(); ++it)
	{
		auto dstType = (*it)->GetProtocolType();
		auto dstIt = s_ProtoToName.find(dstType);
		if (dstIt == s_ProtoToName.end()) continue;

		for (const auto &[proto, name] : s_ProtoToName)
		{
			if (proto == dstType) continue;  // skip self-blocks
			if ((*it)->IsSourceBlocked(proto))
			{
				blocks.push_back({{"from", name}, {"to", dstIt->second}});
			}
		}
	}
	status["blocks"] = blocks;

	protocols.Unlock();
	return status;
}

nlohmann::json CAdminSocket::CmdReconnect(const nlohmann::json &cmd)
{
	if (!cmd.contains("protocol"))
		return {{"status", "error"}, {"message", "missing protocol field"}};

	std::string protocol = cmd["protocol"];

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();

	if (protocol == "mmdvmclient")
	{
		auto *proto = protocols.FindByType(EProtocol::mmdvmclient);
		if (proto)
		{
			static_cast<CMMDVMClientProtocol *>(proto)->RequestReconnect();
			protocols.Unlock();
			return {{"status", "ok"}, {"message", "MMDVMClient reconnect requested"}};
		}
	}
	else if (protocol == "svx")
	{
		auto *proto = protocols.FindByType(EProtocol::svxreflector);
		if (proto)
		{
			static_cast<CSvxReflectorProtocol *>(proto)->RequestReconnect();
			protocols.Unlock();
			return {{"status", "ok"}, {"message", "SVX reconnect requested"}};
		}
	}
	else if (protocol == "dcsclient")
	{
		auto *proto = protocols.FindByType(EProtocol::dcsclient);
		if (proto)
		{
			static_cast<CDcsClientProtocol *>(proto)->RequestReconnect();
			protocols.Unlock();
			return {{"status", "ok"}, {"message", "DCSClient reconnect requested"}};
		}
	}
	else if (protocol == "dextraclient")
	{
		auto *proto = protocols.FindByType(EProtocol::dextraclient);
		if (proto)
		{
			static_cast<CDExtraClientProtocol *>(proto)->RequestReconnect();
			protocols.Unlock();
			return {{"status", "ok"}, {"message", "DExtraClient reconnect requested"}};
		}
	}
	else if (protocol == "dplusclient")
	{
		auto *proto = protocols.FindByType(EProtocol::dplusclient);
		if (proto)
		{
			static_cast<CDPlusClientProtocol *>(proto)->RequestReconnect();
			protocols.Unlock();
			return {{"status", "ok"}, {"message", "DPlusClient reconnect requested"}};
		}
	}
	else if (protocol == "ysfclient")
	{
		auto *proto = protocols.FindByType(EProtocol::ysfclient);
		if (proto)
		{
			static_cast<CYsfClientProtocol *>(proto)->RequestReconnect();
			protocols.Unlock();
			return {{"status", "ok"}, {"message", "YSFClient reconnect requested"}};
		}
	}

	protocols.Unlock();
	return {{"status", "error"}, {"message", "protocol not found: " + protocol}};
}

nlohmann::json CAdminSocket::CmdTCStats(void)
{
	nlohmann::json result;
	result["status"] = "ok";
	result["modules"] = nlohmann::json::array();

	const std::string tcModules = g_Configure.GetString(g_Keys.tc.modules);

	for (char mod : tcModules)
	{
		nlohmann::json modInfo;
		modInfo["module"] = std::string(1, mod);
		modInfo["connected"] = g_TCServer.IsModuleConnected(mod);

		auto stream = g_Reflector.GetStream(mod);
		if (stream)
		{
			modInfo["streaming"] = stream->IsOpen();
			if (stream->IsOpen())
			{
				modInfo["stream_id"] = stream->GetStreamId();
				modInfo["user"] = stream->GetUserCallsign().GetCS();
			}

			CCodecStream *cs = stream->GetCodecStream();
			if (cs)
			{
				modInfo["codec_in"] = (int)cs->GetCodecIn();
				modInfo["total_packets"] = cs->GetTotalPackets();
				modInfo["mismatch_count"] = cs->GetMismatchCount();
				modInfo["rt_count"] = cs->GetRTCount();
				if (cs->GetRTCount() > 0 && cs->GetRTMin() >= 0)
				{
					modInfo["rt_min_ms"] = (int)(cs->GetRTMin() * 1000.0);
					modInfo["rt_avg_ms"] = (int)(cs->GetRTAvg() * 1000.0);
					modInfo["rt_max_ms"] = (int)(cs->GetRTMax() * 1000.0);
				}
			}
		}
		else
		{
			modInfo["streaming"] = false;
		}

		result["modules"].push_back(modInfo);
	}

	return result;
}

nlohmann::json CAdminSocket::CmdLog(const nlohmann::json &cmd)
{
	int count = 50;
	if (cmd.contains("lines"))
		count = cmd["lines"];
	if (count < 1) count = 1;
	if (count > 200) count = 200;

	auto lines = g_LogBuffer.GetLines(count);

	nlohmann::json result;
	result["status"] = "ok";
	result["lines"] = nlohmann::json::array();
	for (const auto &line : lines)
		result["lines"].push_back(line);

	return result;
}

////////////////////////////////////////////////////////////////////////////////////////
// Kerchunk — send a brief DMR transmission to keep a TG alive on BrandMeister

nlohmann::json CAdminSocket::CmdKerchunk(const nlohmann::json &cmd)
{
	if (!cmd.contains("tg"))
		return {{"status", "error"}, {"message", "missing 'tg' field"}};

	uint32_t tg = cmd["tg"];

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::mmdvmclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "MMDVMClient protocol not active"}};
	}

	auto *mmdvm = static_cast<CMMDVMClientProtocol *>(proto);

	// Verify TG is actually mapped
	char module = mmdvm->GetTGMap().TGToModule(tg);
	if (module == ' ')
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "TG not mapped"}};
	}

	mmdvm->RequestKerchunk(tg);
	protocols.Unlock();

	// Also refresh local timer
	mmdvm->GetTGMap().RefreshActivity(tg);

	std::cout << "Admin: kerchunk requested for TG" << tg << std::endl;
	return {{"status", "ok"}, {"tg", tg}, {"module", std::string(1, module)}};
}

////////////////////////////////////////////////////////////////////////////////////////
// Block commands — runtime protocol blocking (not persisted to config)

nlohmann::json CAdminSocket::CmdBlock(const nlohmann::json &cmd)
{
	if (!cmd.contains("a") || !cmd.contains("b"))
		return {{"status", "error"}, {"message", "missing 'a' and 'b' fields"}};

	std::string aName = cmd["a"];
	std::string bName = cmd["b"];

	auto aIt = s_NameToProto.find(aName);
	auto bIt = s_NameToProto.find(bName);
	if (aIt == s_NameToProto.end())
		return {{"status", "error"}, {"message", "unknown protocol: " + aName}};
	if (bIt == s_NameToProto.end())
		return {{"status", "error"}, {"message", "unknown protocol: " + bName}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();

	// Bidirectional: block A as source on B, and B as source on A
	for (auto it = protocols.begin(); it != protocols.end(); ++it)
	{
		auto type = (*it)->GetProtocolType();
		if (type == aIt->second)
			(*it)->SetSourceBlocked(bIt->second);
		if (type == bIt->second)
			(*it)->SetSourceBlocked(aIt->second);
	}

	protocols.Unlock();
	std::cout << "Admin: blocked " << aName << " <-> " << bName << std::endl;
	return {{"status", "ok"}, {"message", aName + " <-> " + bName + " blocked"}};
}

nlohmann::json CAdminSocket::CmdUnblock(const nlohmann::json &cmd)
{
	if (!cmd.contains("a") || !cmd.contains("b"))
		return {{"status", "error"}, {"message", "missing 'a' and 'b' fields"}};

	std::string aName = cmd["a"];
	std::string bName = cmd["b"];

	auto aIt = s_NameToProto.find(aName);
	auto bIt = s_NameToProto.find(bName);
	if (aIt == s_NameToProto.end())
		return {{"status", "error"}, {"message", "unknown protocol: " + aName}};
	if (bIt == s_NameToProto.end())
		return {{"status", "error"}, {"message", "unknown protocol: " + bName}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();

	// Bidirectional: unblock both directions
	for (auto it = protocols.begin(); it != protocols.end(); ++it)
	{
		auto type = (*it)->GetProtocolType();
		if (type == aIt->second)
			(*it)->ClearSourceBlocked(bIt->second);
		if (type == bIt->second)
			(*it)->ClearSourceBlocked(aIt->second);
	}

	protocols.Unlock();
	std::cout << "Admin: unblocked " << aName << " <-> " << bName << std::endl;
	return {{"status", "ok"}, {"message", aName + " <-> " + bName + " unblocked"}};
}

nlohmann::json CAdminSocket::CmdBlockReset(void)
{
	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();

	for (auto it = protocols.begin(); it != protocols.end(); ++it)
		(*it)->ResetBlocksToDefault();

	protocols.Unlock();
	std::cout << "Admin: blocks reset to config defaults" << std::endl;
	return {{"status", "ok"}, {"message", "all blocks reset to config defaults"}};
}

////////////////////////////////////////////////////////////////////////////////////////
// DCSClient mapping management

nlohmann::json CAdminSocket::CmdDcsMapAdd(const nlohmann::json &cmd)
{
	if (!cmd.contains("host") || !cmd.contains("port") || !cmd.contains("remote_module") || !cmd.contains("local_module"))
		return {{"status", "error"}, {"message", "missing required fields: host, port, remote_module, local_module"}};

	std::string host = cmd["host"];
	uint16_t port = cmd["port"];
	std::string remoteMod = cmd["remote_module"];
	std::string localMod = cmd["local_module"];

	if (remoteMod.empty() || localMod.empty() || remoteMod[0] < 'A' || remoteMod[0] > 'Z' || localMod[0] < 'A' || localMod[0] > 'Z')
		return {{"status", "error"}, {"message", "modules must be A-Z"}};

	if (!g_Reflector.IsValidModule(localMod[0]))
		return {{"status", "error"}, {"message", std::string("module ") + localMod[0] + " is not configured"}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dcsclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "DCSClient protocol not active"}};
	}

	auto *dcs = static_cast<CDcsClientProtocol *>(proto);
	bool ok = dcs->AddMapping(host, port, remoteMod[0], localMod[0]);
	protocols.Unlock();

	if (!ok)
		return {{"status", "error"}, {"message", "failed to add mapping (local module already mapped)"}};

	return {{"status", "ok"}, {"message", "DCSClient mapping " + host + ":" + std::to_string(port) + " " + remoteMod[0] + " -> " + localMod[0] + " added"}};
}

nlohmann::json CAdminSocket::CmdDcsMapRemove(const nlohmann::json &cmd)
{
	if (!cmd.contains("local_module"))
		return {{"status", "error"}, {"message", "missing required field: local_module"}};

	std::string localMod = cmd["local_module"];
	if (localMod.empty() || localMod[0] < 'A' || localMod[0] > 'Z')
		return {{"status", "error"}, {"message", "module must be A-Z"}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dcsclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "DCSClient protocol not active"}};
	}

	auto *dcs = static_cast<CDcsClientProtocol *>(proto);
	bool ok = dcs->RemoveMapping(localMod[0]);
	protocols.Unlock();

	if (!ok)
		return {{"status", "error"}, {"message", "mapping not found for module " + localMod}};

	return {{"status", "ok"}, {"message", "DCSClient mapping for module " + localMod + " removed"}};
}

nlohmann::json CAdminSocket::CmdDcsMapList(void)
{
	nlohmann::json result;
	result["status"] = "ok";
	result["mappings"] = nlohmann::json::array();

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dcsclient);
	if (proto)
	{
		auto *dcs = static_cast<CDcsClientProtocol *>(proto);
		auto mappings = dcs->GetMappings();
		for (const auto &m : mappings)
		{
			result["mappings"].push_back({
				{"host", m.host},
				{"port", m.port},
				{"remote_module", std::string(1, m.remoteModule)},
				{"local_module", std::string(1, m.localModule)},
				{"connected", m.connected}
			});
		}
	}
	protocols.Unlock();

	return result;
}

////////////////////////////////////////////////////////////////////////////////////////
// DExtraClient mapping management

nlohmann::json CAdminSocket::CmdDExtraMapAdd(const nlohmann::json &cmd)
{
	if (!cmd.contains("host") || !cmd.contains("port") || !cmd.contains("remote_module") || !cmd.contains("local_module"))
		return {{"status", "error"}, {"message", "missing required fields: host, port, remote_module, local_module"}};

	std::string host = cmd["host"];
	uint16_t port = cmd["port"];
	std::string remoteMod = cmd["remote_module"];
	std::string localMod = cmd["local_module"];

	if (remoteMod.empty() || localMod.empty() || remoteMod[0] < 'A' || remoteMod[0] > 'Z' || localMod[0] < 'A' || localMod[0] > 'Z')
		return {{"status", "error"}, {"message", "modules must be A-Z"}};

	if (!g_Reflector.IsValidModule(localMod[0]))
		return {{"status", "error"}, {"message", std::string("module ") + localMod[0] + " is not configured"}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dextraclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "DExtraClient protocol not active"}};
	}

	auto *dextra = static_cast<CDExtraClientProtocol *>(proto);
	bool ok = dextra->AddMapping(host, port, remoteMod[0], localMod[0]);
	protocols.Unlock();

	if (!ok)
		return {{"status", "error"}, {"message", "failed to add mapping (local module already mapped)"}};

	return {{"status", "ok"}, {"message", "DExtraClient mapping " + host + ":" + std::to_string(port) + " " + remoteMod[0] + " -> " + localMod[0] + " added"}};
}

nlohmann::json CAdminSocket::CmdDExtraMapRemove(const nlohmann::json &cmd)
{
	if (!cmd.contains("local_module"))
		return {{"status", "error"}, {"message", "missing required field: local_module"}};

	std::string localMod = cmd["local_module"];
	if (localMod.empty() || localMod[0] < 'A' || localMod[0] > 'Z')
		return {{"status", "error"}, {"message", "module must be A-Z"}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dextraclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "DExtraClient protocol not active"}};
	}

	auto *dextra = static_cast<CDExtraClientProtocol *>(proto);
	bool ok = dextra->RemoveMapping(localMod[0]);
	protocols.Unlock();

	if (!ok)
		return {{"status", "error"}, {"message", "mapping not found for module " + localMod}};

	return {{"status", "ok"}, {"message", "DExtraClient mapping for module " + localMod + " removed"}};
}

nlohmann::json CAdminSocket::CmdDExtraMapList(void)
{
	nlohmann::json result;
	result["status"] = "ok";
	result["mappings"] = nlohmann::json::array();

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dextraclient);
	if (proto)
	{
		auto *dextra = static_cast<CDExtraClientProtocol *>(proto);
		auto mappings = dextra->GetMappings();
		for (const auto &m : mappings)
		{
			result["mappings"].push_back({
				{"host", m.host},
				{"port", m.port},
				{"remote_module", std::string(1, m.remoteModule)},
				{"local_module", std::string(1, m.localModule)},
				{"connected", m.connected}
			});
		}
	}
	protocols.Unlock();

	return result;
}

////////////////////////////////////////////////////////////////////////////////////////
// DPlusClient mapping management

nlohmann::json CAdminSocket::CmdDPlusMapAdd(const nlohmann::json &cmd)
{
	if (!cmd.contains("host") || !cmd.contains("port") || !cmd.contains("remote_module") || !cmd.contains("local_module"))
		return {{"status", "error"}, {"message", "missing required fields: host, port, remote_module, local_module"}};

	std::string host = cmd["host"];
	uint16_t port = cmd["port"];
	std::string remoteMod = cmd["remote_module"];
	std::string localMod = cmd["local_module"];

	if (remoteMod.empty() || localMod.empty() || remoteMod[0] < 'A' || remoteMod[0] > 'Z' || localMod[0] < 'A' || localMod[0] > 'Z')
		return {{"status", "error"}, {"message", "modules must be A-Z"}};

	if (!g_Reflector.IsValidModule(localMod[0]))
		return {{"status", "error"}, {"message", std::string("module ") + localMod[0] + " is not configured"}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dplusclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "DPlusClient protocol not active"}};
	}

	auto *dplus = static_cast<CDPlusClientProtocol *>(proto);
	bool ok = dplus->AddMapping(host, port, remoteMod[0], localMod[0]);
	protocols.Unlock();

	if (!ok)
		return {{"status", "error"}, {"message", "failed to add mapping (local module already mapped)"}};

	return {{"status", "ok"}, {"message", "DPlusClient mapping " + host + ":" + std::to_string(port) + " " + remoteMod[0] + " -> " + localMod[0] + " added"}};
}

nlohmann::json CAdminSocket::CmdDPlusMapRemove(const nlohmann::json &cmd)
{
	if (!cmd.contains("local_module"))
		return {{"status", "error"}, {"message", "missing required field: local_module"}};

	std::string localMod = cmd["local_module"];
	if (localMod.empty() || localMod[0] < 'A' || localMod[0] > 'Z')
		return {{"status", "error"}, {"message", "module must be A-Z"}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dplusclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "DPlusClient protocol not active"}};
	}

	auto *dplus = static_cast<CDPlusClientProtocol *>(proto);
	bool ok = dplus->RemoveMapping(localMod[0]);
	protocols.Unlock();

	if (!ok)
		return {{"status", "error"}, {"message", "mapping not found for module " + localMod}};

	return {{"status", "ok"}, {"message", "DPlusClient mapping for module " + localMod + " removed"}};
}

nlohmann::json CAdminSocket::CmdDPlusMapList(void)
{
	nlohmann::json result;
	result["status"] = "ok";
	result["mappings"] = nlohmann::json::array();

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::dplusclient);
	if (proto)
	{
		auto *dplus = static_cast<CDPlusClientProtocol *>(proto);
		auto mappings = dplus->GetMappings();
		for (const auto &m : mappings)
		{
			result["mappings"].push_back({
				{"host", m.host},
				{"port", m.port},
				{"remote_module", std::string(1, m.remoteModule)},
				{"local_module", std::string(1, m.localModule)},
				{"connected", m.connected}
			});
		}
	}
	protocols.Unlock();

	return result;
}

////////////////////////////////////////////////////////////////////////////////////////
// YSFClient mapping management

nlohmann::json CAdminSocket::CmdYsfMapAdd(const nlohmann::json &cmd)
{
	if (!cmd.contains("host") || !cmd.contains("port") || !cmd.contains("local_module"))
		return {{"status", "error"}, {"message", "missing required fields: host, port, local_module"}};

	std::string host = cmd["host"];
	uint16_t port = cmd["port"];
	std::string localMod = cmd["local_module"];
	uint8_t dgid = cmd.contains("dgid") ? (uint8_t)(int)cmd["dgid"] : 0;

	if (localMod.empty() || localMod[0] < 'A' || localMod[0] > 'Z')
		return {{"status", "error"}, {"message", "module must be A-Z"}};

	if (!g_Reflector.IsValidModule(localMod[0]))
		return {{"status", "error"}, {"message", std::string("module ") + localMod[0] + " is not configured"}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::ysfclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "YSFClient protocol not active"}};
	}

	auto *ysf = static_cast<CYsfClientProtocol *>(proto);
	bool ok = ysf->AddMapping(host, port, localMod[0], dgid);
	protocols.Unlock();

	if (!ok)
		return {{"status", "error"}, {"message", "failed to add mapping (local module already mapped)"}};

	std::string msg = "YSFClient mapping " + host + ":" + std::to_string(port) + " -> " + localMod[0];
	if (dgid > 0) msg += " DG-ID " + std::to_string(dgid);
	return {{"status", "ok"}, {"message", msg + " added"}};
}

nlohmann::json CAdminSocket::CmdYsfMapRemove(const nlohmann::json &cmd)
{
	if (!cmd.contains("local_module"))
		return {{"status", "error"}, {"message", "missing required field: local_module"}};

	std::string localMod = cmd["local_module"];
	if (localMod.empty() || localMod[0] < 'A' || localMod[0] > 'Z')
		return {{"status", "error"}, {"message", "module must be A-Z"}};

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::ysfclient);
	if (!proto)
	{
		protocols.Unlock();
		return {{"status", "error"}, {"message", "YSFClient protocol not active"}};
	}

	auto *ysf = static_cast<CYsfClientProtocol *>(proto);
	bool ok = ysf->RemoveMapping(localMod[0]);
	protocols.Unlock();

	if (!ok)
		return {{"status", "error"}, {"message", "mapping not found for module " + localMod}};

	return {{"status", "ok"}, {"message", "YSFClient mapping for module " + localMod + " removed"}};
}

nlohmann::json CAdminSocket::CmdYsfMapList(void)
{
	nlohmann::json result;
	result["status"] = "ok";
	result["mappings"] = nlohmann::json::array();

	auto &protocols = g_Reflector.GetProtocols();
	protocols.Lock();
	auto *proto = protocols.FindByType(EProtocol::ysfclient);
	if (proto)
	{
		auto *ysf = static_cast<CYsfClientProtocol *>(proto);
		auto mappings = ysf->GetMappings();
		for (const auto &m : mappings)
		{
			result["mappings"].push_back({
				{"host", m.host},
				{"port", m.port},
				{"local_module", std::string(1, m.localModule)},
				{"dgid", m.dgid},
				{"connected", m.connected}
			});
		}
	}
	protocols.Unlock();

	return result;
}
