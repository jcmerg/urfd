//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.

// urfd -- The universal reflector
// Copyright © 2021 Thomas A. Early N7TAE
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

#include <mutex>
#include <random>
#include <unordered_map>

#include "Defines.h"
#include "Timer.h"
#include "Protocol.h"
#include "DVHeaderPacket.h"
#include "DVFramePacket.h"
#include "TGModuleMap.h"

////////////////////////////////////////////////////////////////////////////////////////
// define

// frame type
#define DMRMMDVM_FRAMETYPE_VOICE     0
#define DMRMMDVM_FRAMETYPE_VOICESYNC 1
#define DMRMMDVM_FRAMETYPE_DATASYNC  2

// slot type
#define MMDVM_SLOTTYPE_HEADER        1
#define MMDVM_SLOTTYPE_TERMINATOR    2

// DMRMMDVM Module ID (legacy, used as default CSModule for client callsigns)


// pending auth timeout
#define MMDVM_AUTH_TIMEOUT          30

////////////////////////////////////////////////////////////////////////////////////////
// class

class CDmrmmdvmStreamCacheItem
{
public:
	CDvHeaderPacket m_dvHeader;
	CDvFramePacket  m_dvFrame0;
	CDvFramePacket  m_dvFrame1;

	uint8_t  m_uiSeqId;
};


class CDmrmmdvmProtocol : public CProtocol
{
public:
	// initialization
	bool Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6);

	// task
	void Task(void);

	// user management (thread-safe, called from admin socket)
	bool AddUser(uint32_t baseId, const std::string &password);
	bool AddUserByCallsign(const std::string &callsign, const std::string &password);
	bool RemoveUser(uint32_t baseId);
	bool RemoveUserByCallsign(const std::string &callsign);
	struct SUserEntry { uint32_t dmrid; std::string callsign; };
	std::vector<SUserEntry> GetUsers() const;

	// TG management
	CTGModuleMap &GetTGMap() { return m_TGMap; }
	const CTGModuleMap &GetTGMap() const { return m_TGMap; }

	// peer info
	struct SPeerInfo {
		uint32_t dmrid = 0;
		std::string callsign;
		std::string rxFreq, txFreq;
		std::string txPower, colorCode;
		std::string latitude, longitude;
		std::string height, location, description;
		std::string slots, url, softwareId, packageId;
		bool populated = false;
	};
	const std::unordered_map<std::string, SPeerInfo> &GetPeerInfo() const { return m_PeerInfoMap; }

protected:
	// queue helper
	void HandleQueue(void);

	// keepalive helpers
	void HandleKeepalives(void);

	// stream helpers
	void OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &, const CIp &, uint8_t, uint8_t, uint32_t sourceTG = 0);

	// packet decoding helpers
	bool IsValidConnectPacket(const CBuffer &, CCallsign *, const CIp &, uint32_t *rawDmrId);
	bool IsValidAuthenticationPacket(const CBuffer &, CCallsign *, const CIp &, uint32_t *rawDmrId);
	bool IsValidDisconnectPacket(const CBuffer &, CCallsign *);
	bool IsValidConfigPacket(const CBuffer &, CCallsign *, const CIp &);
	bool IsValidOptionPacket(const CBuffer &, CCallsign *);
	bool IsValidKeepAlivePacket(const CBuffer &, CCallsign *);
	bool IsValidRssiPacket(const CBuffer &, CCallsign *, int *);
	bool IsValidDvHeaderPacket(const CBuffer &, std::unique_ptr<CDvHeaderPacket> &, uint8_t *, uint8_t *);
	bool IsValidDvFramePacket(const CIp &, const CBuffer &, std::unique_ptr<CDvHeaderPacket> &, std::array<std::unique_ptr<CDvFramePacket>, 3> &);
	bool IsValidDvLastFramePacket(const CBuffer &, std::unique_ptr<CDvFramePacket> &);

	// packet encoding helpers
	void EncodeKeepAlivePacket(CBuffer *, std::shared_ptr<CClient>);
	void EncodeAckPacket(CBuffer *, const CCallsign &);
	void EncodeConnectAckPacket(CBuffer *, const CCallsign &, uint32_t);
	void EncodeNackPacket(CBuffer *, const CCallsign &);
	void EncodeClosePacket(CBuffer *, std::shared_ptr<CClient>);
	bool EncodeMMDVMHeaderPacket(const CDvHeaderPacket &, uint8_t, CBuffer *) const;
	void EncodeMMDVMPacket(const CDvHeaderPacket &, const CDvFramePacket &, const CDvFramePacket &, const CDvFramePacket &, uint8_t, CBuffer *) const;
	void EncodeLastMMDVMPacket(const CDvHeaderPacket &, uint8_t, CBuffer *) const;

	// dmr DstId to Module helper
	char DmrDstIdToModule(uint32_t) const;
	uint32_t ModuleToDmrDestId(char) const;

	// Buffer & LC helpers
	void AppendVoiceLCToBuffer(CBuffer *, uint32_t uiSrcId, uint32_t uiDstId) const;
	void AppendTerminatorLCToBuffer(CBuffer *, uint32_t uiSrcId, uint32_t uiDstId) const;
	void ReplaceEMBInBuffer(CBuffer *, uint8_t) const;
	void AppendDmrIdToBuffer(CBuffer *, uint32_t) const;
	void AppendDmrRptrIdToBuffer(CBuffer *, uint32_t) const;

	// auth helpers
	static uint32_t ResolveBaseId(uint32_t extId) { return (extId > 9999999) ? extId / 100 : extId; }
	bool VerifyAuthHash(uint32_t rawDmrId, const uint8_t *clientHash);
	void CleanupPendingAuth();

	// INI persistence for user management
	bool IniAddUser(uint32_t baseId, const std::string &password);
	bool IniRemoveUser(uint32_t baseId);

	// RPTC config parsing
	void ParseConfigPacket(const CBuffer &, const CIp &);

protected:
	// for keep alive
	CTimer         m_LastKeepaliveTime;

	// for stream id
	uint16_t              m_uiStreamId;

	// for queue header caches — keyed by (module, slot)
	struct SCacheKey {
		char module; uint8_t slot;
		bool operator==(const SCacheKey &o) const { return module == o.module && slot == o.slot; }
	};
	struct SCacheKeyHash {
		size_t operator()(const SCacheKey &k) const { return std::hash<char>()(k.module) ^ (std::hash<uint8_t>()(k.slot) << 8); }
	};
	std::unordered_map<SCacheKey, CDmrmmdvmStreamCacheItem, SCacheKeyHash> m_StreamsCache;

	// authentication — per-connection random salt
	struct SPendingAuthEntry {
		uint32_t salt;
		std::chrono::steady_clock::time_point created;
	};
	std::unordered_map<uint32_t, SPendingAuthEntry> m_PendingAuth;  // rawDmrId -> salt
	std::mt19937 m_Rng;

	// user passwords — base DMR ID -> password
	mutable std::mutex m_PasswordMutex;
	std::unordered_map<uint32_t, std::string> m_Passwords;

	// TG mapping
	CTGModuleMap m_TGMap;

	// INI file access
	std::mutex m_IniMutex;

	// peer info — keyed by IP address string
	std::unordered_map<std::string, SPeerInfo> m_PeerInfoMap;

	// config data
	unsigned m_DefaultId;
	bool m_RequireAuth = true;
};
