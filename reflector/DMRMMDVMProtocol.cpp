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


#include <string.h>
#include <algorithm>
#include <chrono>
#include <fstream>

#include "Global.h"
#include "DMRMMDVMClient.h"
#include "DMRMMDVMProtocol.h"
#include "BPTC19696.h"
#include "RS129.h"
#include "Golay2087.h"
#include "QR1676.h"
#include "SHA256.h"

////////////////////////////////////////////////////////////////////////////////////////
// define

#define CMD_NONE        0
#define CMD_LINK        1
#define CMD_UNLINK      2

////////////////////////////////////////////////////////////////////////////////////////
// constants

static uint8_t g_DmrSyncBSVoice[]    = { 0x07,0x55,0xFD,0x7D,0xF7,0x5F,0x70 };
static uint8_t g_DmrSyncBSData[]     = { 0x0D,0xFF,0x57,0xD7,0x5D,0xF5,0xD0 };
static uint8_t g_DmrSyncMSVoice[]    = { 0x07,0xF7,0xD5,0xDD,0x57,0xDF,0xD0 };
static uint8_t g_DmrSyncMSData[]     = { 0x0D,0x5D,0x7F,0x77,0xFD,0x75,0x70 };


////////////////////////////////////////////////////////////////////////////////////////
// operation

bool CDmrmmdvmProtocol::Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	m_DefaultId = g_Configure.GetUnsigned(g_Keys.mmdvm.fallbackdmrid);
	// RequireAuth defaults to true; only disabled when explicitly set to false
	m_RequireAuth = !g_Configure.GetData().contains(g_Keys.mmdvm.requireauth) || g_Configure.GetBoolean(g_Keys.mmdvm.requireauth);
	// base class
	if (! CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	// update time
	m_LastKeepaliveTime.start();

	// random number generator
	m_Rng.seed(std::random_device{}());

	// load user passwords from config
	{
		const auto &jdata = g_Configure.GetData();
		for (auto it = jdata.begin(); it != jdata.end(); ++it)
		{
			const std::string &key = it.key();
			if (key.substr(0, 9) == "mmdvmUsr_")
			{
				std::string idOrCs = key.substr(9);
				std::string password = it.value().get<std::string>();
				// check if it's numeric (DMR ID) or callsign
				bool isNumeric = !idOrCs.empty() && std::all_of(idOrCs.begin(), idOrCs.end(), ::isdigit);
				if (isNumeric)
				{
					uint32_t baseId = (uint32_t)std::stoul(idOrCs);
					m_Passwords[baseId] = password;
					std::cout << "MMDVM: loaded user DMR ID " << baseId << std::endl;
				}
				else
				{
					// resolve callsign to DMR ID
					g_LDid.Lock();
					uint32_t dmrid = g_LDid.FindDmrid(CCallsign(idOrCs).GetKey());
					g_LDid.Unlock();
					if (dmrid != 0)
					{
						m_Passwords[dmrid] = password;
						std::cout << "MMDVM: loaded user " << idOrCs << " (DMR ID " << dmrid << ")" << std::endl;
					}
					else
					{
						std::cerr << "MMDVM: WARNING - cannot resolve callsign '" << idOrCs << "' to DMR ID (not in database)" << std::endl;
					}
				}
			}
		}
		if (!m_RequireAuth)
			std::cout << "MMDVM: authentication disabled (RequireAuth = false)" << std::endl;
		else if (m_Passwords.empty())
			std::cout << "MMDVM: no users configured, all logins rejected" << std::endl;
		else
			std::cout << "MMDVM: " << m_Passwords.size() << " user(s) configured" << std::endl;
	}

	// load TG mappings
	m_TGMap.LoadFromConfig("mmdvm", "MMDVM");

	// done
	return true;
}



////////////////////////////////////////////////////////////////////////////////////////
// task

void CDmrmmdvmProtocol::Task(void)
{
	CBuffer   Buffer;
	CIp       Ip;
	CCallsign Callsign;
	int       iRssi;
	uint8_t     Cmd;
	uint8_t     CallType;
	uint32_t  rawDmrId = 0;
	std::unique_ptr<CDvHeaderPacket>  Header;
	std::unique_ptr<CDvFramePacket>   LastFrame;
	std::array<std::unique_ptr<CDvFramePacket>, 3> Frames;

	// handle incoming packets
#if DMR_IPV6==true
#if DMR_IPV4==true
	if ( ReceiveDS(Buffer, Ip, 20) )
#else
	if ( Receive6(Buffer, Ip, 20) )
#endif
#else
	if ( Receive4(Buffer, Ip, 20) )
#endif
	{
		//Buffer.DebugDump(g_Reflector.m_DebugFile);
		// crack the packet
		if ( IsValidDvFramePacket(Ip, Buffer, Header, Frames) )
		{
			for ( int i = 0; i < 3; i++ )
			{
				OnDvFramePacketIn(Frames.at(i), &Ip);
			}
		}
		else if ( IsValidDvHeaderPacket(Buffer, Header, &Cmd, &CallType) )
		{
			// callsign muted?
			if ( g_GateKeeper.MayTransmit(Header->GetMyCallsign(), Ip, EProtocol::dmrmmdvm) )
			{
				// handle it
				OnDvHeaderPacketIn(Header, Ip, Cmd, CallType);
			}
		}
		else if ( IsValidDvLastFramePacket(Buffer, LastFrame) )
		{
			OnDvFramePacketIn(LastFrame, &Ip);
		}
		else if ( IsValidConnectPacket(Buffer, &Callsign, Ip, &rawDmrId) )
		{
			std::cout << "MMDVM: connect from " << Callsign << " at " << Ip << std::endl;

			// callsign authorized?
			if ( g_GateKeeper.MayLink(Callsign, Ip, EProtocol::dmrmmdvm) )
			{
				// acknowledge the request with per-connection salt
				uint32_t salt = m_Rng();
				m_PendingAuth[rawDmrId] = { salt, std::chrono::steady_clock::now() };
				EncodeConnectAckPacket(&Buffer, Callsign, salt);
				Send(Buffer, Ip);
			}
			else
			{
				// deny the request
				EncodeNackPacket(&Buffer, Callsign);
				Send(Buffer, Ip);
			}

		}
		else if ( IsValidAuthenticationPacket(Buffer, &Callsign, Ip, &rawDmrId) )
		{
			std::cout << "MMDVM: auth from " << Callsign << " at " << Ip << std::endl;

			// verify authentication
			bool authOk = m_RequireAuth ? VerifyAuthHash(rawDmrId, Buffer.data() + 8) : true;

			if (authOk && g_GateKeeper.MayLink(Callsign, Ip, EProtocol::dmrmmdvm))
			{
				// acknowledge the request
				EncodeAckPacket(&Buffer, Callsign);
				Send(Buffer, Ip);

				// add client if needed
				CClients *clients = g_Reflector.GetClients();
				std::shared_ptr<CClient>client = clients->FindClient(Callsign, Ip, EProtocol::dmrmmdvm);
				// client already connected ?
				if ( client == nullptr )
				{
					std::cout << "MMDVM: login " << Callsign << " at " << Ip << std::endl;

					// create the client and append
					clients->AddClient(std::make_shared<CDmrmmdvmClient>(Callsign, Ip));
				}
				else
				{
					client->Alive();
				}
				// and done
				g_Reflector.ReleaseClients();
			}
			else
			{
				if (!authOk)
					std::cout << "MMDVM: auth FAILED for " << Callsign << " (DMR ID " << rawDmrId << ") at " << Ip << std::endl;
				// deny the request
				EncodeNackPacket(&Buffer, Callsign);
				Send(Buffer, Ip);
			}

		}
		else if ( IsValidDisconnectPacket(Buffer, &Callsign) )
		{
			std::cout << "MMDVM: disconnect from " << Callsign << " at " << Ip << std::endl;

			// find client & remove it
			CClients *clients = g_Reflector.GetClients();
			std::shared_ptr<CClient>client = clients->FindClient(Ip, EProtocol::dmrmmdvm);
			if ( client != nullptr )
			{
				clients->RemoveClient(client);
			}
			g_Reflector.ReleaseClients();
		}
		else if ( IsValidConfigPacket(Buffer, &Callsign, Ip) )
		{
			// parse config before ACK (EncodeAckPacket overwrites the buffer)
			ParseConfigPacket(Buffer, Ip);

			// acknowledge the request
			EncodeAckPacket(&Buffer, Callsign);
			Send(Buffer, Ip);
		}
		else if ( IsValidKeepAlivePacket(Buffer, &Callsign) )
		{
			//std::cout << "DMRmmdvm keepalive packet from " << Callsign << " at " << Ip << std::endl;

			// find all clients with that callsign & ip and keep them alive
			CClients *clients = g_Reflector.GetClients();
			auto it = clients->begin();
			std::shared_ptr<CClient>client = nullptr;
			while ( (client = clients->FindNextClient(Callsign, Ip, EProtocol::dmrmmdvm, it)) != nullptr )
			{
				// acknowledge
				EncodeKeepAlivePacket(&Buffer, client);
				Send(Buffer, Ip);

				// and mark as alive
				client->Alive();
			}
			g_Reflector.ReleaseClients();
		}
		else if ( IsValidRssiPacket(Buffer, &Callsign, &iRssi) )
		{
			// std::cout << "DMRmmdvm RSSI packet from " << Callsign << " at " << Ip << std::endl

			// ignore...
		}
		else if ( IsValidOptionPacket(Buffer, &Callsign) )
		{
			std::cout << "MMDVM: options from " << Callsign << " at " << Ip << std::endl;

			// acknowledge the request
			EncodeAckPacket(&Buffer, Callsign);
			Send(Buffer, Ip);
		}
		else if ( Buffer.size() != 55 )
		{
			std::string title("Unknown DMRMMDVM packet from ");
			title += Ip.GetAddress();
		}
	}

	// handle end of streaming timeout
	CheckStreamsTimeout();

	// handle queue from reflector
	HandleQueue();


	// keep client alive
	if ( m_LastKeepaliveTime.time() > DMRMMDVM_KEEPALIVE_PERIOD )
	{
		//
		HandleKeepalives();

		// update time
		m_LastKeepaliveTime.start();
	}

	// cleanup stale pending auth entries
	CleanupPendingAuth();

}

////////////////////////////////////////////////////////////////////////////////////////
// streams helpers

void CDmrmmdvmProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip, uint8_t cmd, uint8_t CallType)
{
	bool lastheard = false;

	// find the stream
	auto stream = GetStream(Header->GetStreamId());
	if ( stream )
	{
		// stream already open
		// skip packet, but tickle the stream
		stream->Tickle();
	}
	else
	{
		CCallsign my(Header->GetMyCallsign());
		CCallsign rpt1(Header->GetRpt1Callsign());
		CCallsign rpt2(Header->GetRpt2Callsign());
		// no stream open yet, open a new one
		// firstfind this client
		std::shared_ptr<CClient>client = g_Reflector.GetClients()->FindClient(Ip, EProtocol::dmrmmdvm);
		if ( client )
		{
			// process cmd if any
			if ( !client->HasReflectorModule() )
			{
				// not linked yet
				if ( cmd == CMD_LINK )
				{
					if ( g_Reflector.IsValidModule(rpt2.GetCSModule()) )
					{
						std::cout << "MMDVM: " << client->GetCallsign() << " linking on module " << rpt2.GetCSModule() << std::endl;
						// link
						client->SetReflectorModule(rpt2.GetCSModule());
					}
					else
					{
						std::cout << "MMDVM: " << rpt1 << " link attempt on non-existing module" << std::endl;
					}
				}
				else if ( cmd == CMD_NONE && rpt2.GetCSModule() != ' ' && g_Reflector.IsValidModule(rpt2.GetCSModule()) )
				{
					// TG mapping resolved to a valid module — auto-link
					std::cout << "MMDVM: " << client->GetCallsign() << " auto-linking on module " << rpt2.GetCSModule() << std::endl;
					client->SetReflectorModule(rpt2.GetCSModule());
				}
			}
			else
			{
				// already linked — switch module if TG maps to a different one
				char targetMod = rpt2.GetCSModule();
				if ( targetMod != ' ' && targetMod != client->GetReflectorModule() && g_Reflector.IsValidModule(targetMod) )
				{
					std::cout << "MMDVM: " << client->GetCallsign() << " switching to module " << targetMod << std::endl;
					client->SetReflectorModule(targetMod);
				}

				if ( cmd == CMD_UNLINK )
				{
					std::cout << "MMDVM: " << client->GetCallsign() << " unlinking" << std::endl;
					// unlink
					client->SetReflectorModule(' ');
				}
				else
				{
					// replace rpt2 module with currently linked module
					auto m = client->GetReflectorModule();
					Header->SetRpt2Module(m);
					rpt2.SetCSModule(m);
				}
			}

			// and now, re-check module is valid && that it's not a private call
			if ( g_Reflector.IsValidModule(rpt2.GetCSModule()) && (CallType == DMR_GROUP_CALL) )
			{
				// yes, try to open the stream
				if ( (stream = g_Reflector.OpenStream(Header, client)) != nullptr )
				{
					// keep the handle
					m_Streams[stream->GetStreamId()] = stream;
					lastheard = true;
				}
			}
		}
		else
		{
			lastheard = true;
		}

		// release
		g_Reflector.ReleaseClients();

		// update last heard
		if ( lastheard )
		{
			g_Reflector.GetUsers()->Hearing(my, rpt1, rpt2, "DMR");
			g_Reflector.ReleaseUsers();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// queue helper

void CDmrmmdvmProtocol::HandleQueue(void)
{
	while (! m_Queue.IsEmpty())
	{
		// get the packet
		auto packet = m_Queue.Pop();

		// get our sender's id
		const auto mod = packet->GetPacketModule();

		// determine outbound slot from TGMap
		uint8_t outSlot = m_TGMap.ModuleToTimeslot(mod);
		if (outSlot == 0) outSlot = DMR_SLOT2;  // default TS2
		SCacheKey cacheKey{mod, outSlot};

		// encode
		CBuffer buffer;

		// check if it's header
		if ( packet->IsDvHeader() )
		{
			// update local stream cache
			// this relies on queue feeder setting valid module id
			m_StreamsCache[cacheKey].m_dvHeader = CDvHeaderPacket((const CDvHeaderPacket &)*packet.get());
			m_StreamsCache[cacheKey].m_uiSeqId = 0;

			// encode it
			EncodeMMDVMHeaderPacket((CDvHeaderPacket &)*packet.get(), m_StreamsCache[cacheKey].m_uiSeqId, &buffer);
			m_StreamsCache[cacheKey].m_uiSeqId = 1;
		}
		// check if it's a last frame
		else if ( packet->IsLastPacket() )
		{
			// encode it
			EncodeLastMMDVMPacket(m_StreamsCache[cacheKey].m_dvHeader, m_StreamsCache[cacheKey].m_uiSeqId, &buffer);
			m_StreamsCache[cacheKey].m_uiSeqId = (m_StreamsCache[cacheKey].m_uiSeqId + 1) & 0xFF;
		}
		// otherwise, just a regular DV frame
		else
		{
			// update local stream cache or send triplet when needed
			switch ( packet->GetDmrPacketSubid() )
			{
			case 1:
				m_StreamsCache[cacheKey].m_dvFrame0 = CDvFramePacket((const CDvFramePacket &)*packet.get());
				break;
			case 2:
				m_StreamsCache[cacheKey].m_dvFrame1 = CDvFramePacket((const CDvFramePacket &)*packet.get());
				break;
			case 3:
				EncodeMMDVMPacket(m_StreamsCache[cacheKey].m_dvHeader, m_StreamsCache[cacheKey].m_dvFrame0, m_StreamsCache[cacheKey].m_dvFrame1, (const CDvFramePacket &)*packet.get(), m_StreamsCache[cacheKey].m_uiSeqId, &buffer);
				m_StreamsCache[cacheKey].m_uiSeqId = (m_StreamsCache[cacheKey].m_uiSeqId + 1) & 0xFF;
				break;
			default:
				break;
			}
		}

		// send it
		if ( buffer.size() > 0 )
		{
			// and push it to all our clients linked to the module and who are not streaming in
			CClients *clients = g_Reflector.GetClients();
			auto it = clients->begin();
			std::shared_ptr<CClient>client = nullptr;
			while ( (client = clients->FindNextClient(EProtocol::dmrmmdvm, it)) != nullptr )
			{
				// is this client busy ?
				if ( !client->IsAMaster() && (client->GetReflectorModule() == packet->GetPacketModule()) )
				{
					// no, send the packet
					Send(buffer, client->GetIp());

				}
			}
			g_Reflector.ReleaseClients();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// keepalive helpers

void CDmrmmdvmProtocol::HandleKeepalives(void)
{
	// DMR MMDVM protocol keepalive request is client tasks
	// here, just check that all clients are still alive
	// and disconnect them if not

	// iterate on clients
	CClients *clients = g_Reflector.GetClients();
	auto it = clients->begin();
	std::shared_ptr<CClient>client = nullptr;
	while ( (client = clients->FindNextClient(EProtocol::dmrmmdvm, it)) != nullptr )
	{
		// is this client busy ?
		if ( client->IsAMaster() )
		{
			// yes, just tickle it
			client->Alive();
		}
		// check it's still with us
		else if ( !client->IsAlive() )
		{
			// no, disconnect
			CBuffer disconnect;
			Send(disconnect, client->GetIp());

			// remove it
			std::cout << "MMDVM: " << client->GetCallsign() << " keepalive timeout" << std::endl;
			clients->RemoveClient(client);
		}

	}
	g_Reflector.ReleaseClients();
}

////////////////////////////////////////////////////////////////////////////////////////
// packet decoding helpers

bool CDmrmmdvmProtocol::IsValidKeepAlivePacket(const CBuffer &Buffer, CCallsign *callsign)
{
	uint8_t tag[] = { 'R','P','T','P','I','N','G' };

	bool valid = false;
	if ( (Buffer.size() == 11) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[10],Buffer.data()[9]),MAKEWORD(Buffer.data()[8],Buffer.data()[7]));
		callsign->SetDmrid(uiRptrId, true);
		callsign->SetCSModule(MMDVM_MODULE_ID);
		valid = callsign->IsValid();
		if (!valid && uiRptrId > 9999999)
		{
			callsign->SetDmrid(uiRptrId / 100, true);
			callsign->SetCSModule(MMDVM_MODULE_ID);
			valid = callsign->IsValid();
		}
	}
	return valid;
}

bool CDmrmmdvmProtocol::IsValidConnectPacket(const CBuffer &Buffer, CCallsign *callsign, const CIp &Ip, uint32_t *rawDmrId)
{
	uint8_t tag[] = { 'R','P','T','L' };

	bool valid = false;
	if ( (Buffer.size() == 8) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],Buffer.data()[4]));
		*rawDmrId = uiRptrId;
		callsign->SetDmrid(uiRptrId, true);
		callsign->SetCSModule(MMDVM_MODULE_ID);
		valid = callsign->IsValid();
		// Extended DMR ID (9 digits) may not be in DB — try base ID
		if (!valid && uiRptrId > 9999999)
		{
			uint32_t baseId = uiRptrId / 100;
			callsign->SetDmrid(baseId, true);
			callsign->SetCSModule(MMDVM_MODULE_ID);
			valid = callsign->IsValid();
		}
		if ( !valid)
		{
			std::cout << "MMDVM: invalid callsign in RPTL from " << Ip << " CS:" << *callsign << " DMRID:" << callsign->GetDmrid() << std::endl;
		}
	}
	return valid;
}

bool CDmrmmdvmProtocol::IsValidAuthenticationPacket(const CBuffer &Buffer, CCallsign *callsign, const CIp &Ip, uint32_t *rawDmrId)
{
	uint8_t tag[] = { 'R','P','T','K' };

	bool valid = false;
	if ( (Buffer.size() == 40) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],Buffer.data()[4]));
		*rawDmrId = uiRptrId;
		callsign->SetDmrid(uiRptrId, true);
		callsign->SetCSModule(MMDVM_MODULE_ID);
		valid = callsign->IsValid();
		// Extended DMR ID (9 digits) may not be in DB — try base ID
		if (!valid && uiRptrId > 9999999)
		{
			uint32_t baseId = uiRptrId / 100;
			callsign->SetDmrid(baseId, true);
			callsign->SetCSModule(MMDVM_MODULE_ID);
			valid = callsign->IsValid();
		}
		if ( !valid)
		{
			std::cout << "MMDVM: invalid callsign in RPTK from " << Ip << " CS:" << *callsign << " DMRID:" << callsign->GetDmrid() << std::endl;
		}

	}
	return valid;
}

bool CDmrmmdvmProtocol::IsValidDisconnectPacket(const CBuffer &Buffer, CCallsign *callsign)
{
	uint8_t tag[] = { 'R','P','T','C','L' };

	bool valid = false;
	if ( (Buffer.size() == 13) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],Buffer.data()[4]));
		callsign->SetDmrid(uiRptrId, true);
		callsign->SetCSModule(MMDVM_MODULE_ID);
		valid = callsign->IsValid();
		if (!valid && uiRptrId > 9999999)
		{
			callsign->SetDmrid(uiRptrId / 100, true);
			callsign->SetCSModule(MMDVM_MODULE_ID);
			valid = callsign->IsValid();
		}
	}
	return valid;
}

bool CDmrmmdvmProtocol::IsValidConfigPacket(const CBuffer &Buffer, CCallsign *callsign, const CIp &Ip)
{
	uint8_t tag[] = { 'R','P','T','C' };

	bool valid = false;
	if ( (Buffer.size() == 302) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],Buffer.data()[4]));
		callsign->SetDmrid(uiRptrId, true);
		callsign->SetCSModule(MMDVM_MODULE_ID);
		valid = callsign->IsValid();
		if (!valid && uiRptrId > 9999999)
		{
			callsign->SetDmrid(uiRptrId / 100, true);
			callsign->SetCSModule(MMDVM_MODULE_ID);
			valid = callsign->IsValid();
		}
		if ( !valid)
		{
			std::cout << "MMDVM: invalid callsign in RPTC from " << Ip << " CS:" << *callsign << " DMRID:" << callsign->GetDmrid() << std::endl;
		}

	}
	return valid;
}

bool CDmrmmdvmProtocol::IsValidOptionPacket(const CBuffer &Buffer, CCallsign *callsign)
{
	uint8_t tag[] = { 'R','P','T','O' };

	bool valid = false;
	if ( (Buffer.size() >= 8) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],Buffer.data()[4]));
		callsign->SetDmrid(uiRptrId, true);
		callsign->SetCSModule(MMDVM_MODULE_ID);
		valid = callsign->IsValid();
		if (!valid && uiRptrId > 9999999)
		{
			callsign->SetDmrid(uiRptrId / 100, true);
			callsign->SetCSModule(MMDVM_MODULE_ID);
			valid = callsign->IsValid();
		}
	}
	return valid;
}

bool CDmrmmdvmProtocol::IsValidRssiPacket(const CBuffer &Buffer, CCallsign *callsign, int *rssi)
{
	uint8_t tag[] = { 'R','P','T','I','N','T','R' };

	bool valid = false;
	if ( (Buffer.size() == 17) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		// ignore rest of it, as not used
		// dmrid is asci hex on 8 bytes
		// rssi is ascii :x-xx
		valid = true;
	}
	return valid;
}

bool CDmrmmdvmProtocol::IsValidDvHeaderPacket(const CBuffer &Buffer, std::unique_ptr<CDvHeaderPacket> &header, uint8_t *cmd, uint8_t *CallType)
{
	uint8_t tag[] = { 'D','M','R','D' };

	*cmd = CMD_NONE;

	if ( (Buffer.size() == 55) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		// frame details
		uint8_t uiFrameType = (Buffer.data()[15] & 0x30) >> 4;
		uint8_t uiSlot = (Buffer.data()[15] & 0x80) ? DMR_SLOT2 : DMR_SLOT1;
		uint8_t uiCallType = (Buffer.data()[15] & 0x40) ? DMR_PRIVATE_CALL : DMR_GROUP_CALL;
		uint8_t uiSlotType = Buffer.data()[15] & 0x0F;
		//std::cout << (int)uiSlot << std::endl;
		if ( (uiFrameType == DMRMMDVM_FRAMETYPE_DATASYNC) &&
				(uiSlotType == MMDVM_SLOTTYPE_HEADER) )
		{
			// extract sync
			uint8_t dmrsync[7];
			dmrsync[0] = Buffer.data()[33] & 0x0F;
			memcpy(&dmrsync[1], &Buffer.data()[34], 5);
			dmrsync[6] = Buffer.data()[39] & 0xF0;
			// and check
			if ( (memcmp(dmrsync, g_DmrSyncMSData, sizeof(dmrsync)) == 0) ||
					(memcmp(dmrsync, g_DmrSyncBSData, sizeof(dmrsync)) == 0))
			{
				// get payload
				//CBPTC19696 bptc;
				//uint8_t lcdata[12];
				//bptc.decode(&(Buffer.data()[20]), lcdata);

				// crack DMR header
				//uint8_t uiSeqId = Buffer.data()[4];
				uint32_t uiSrcId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],0));
				uint32_t uiDstId = MAKEDWORD(MAKEWORD(Buffer.data()[10],Buffer.data()[9]),MAKEWORD(Buffer.data()[8],0));
				uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[14],Buffer.data()[13]),MAKEWORD(Buffer.data()[12],Buffer.data()[11]));
				//uint8_t uiVoiceSeq = (Buffer.data()[15] & 0x0F);
				uint32_t uiStreamId;
				memcpy(&uiStreamId, &Buffer.data()[16], sizeof(uiStreamId));
				// encode slot into streamId high byte
				uiStreamId = (uiStreamId & 0x00FFFFFF) | ((uint32_t)uiSlot << 24);

				// call type
				*CallType = uiCallType;

				// link/unlink command ?
				if ( uiDstId == 4000 )
				{
					*cmd = CMD_UNLINK;
				}
				else if ( DmrDstIdToModule(uiDstId) != ' ' )
				{
					*cmd = CMD_LINK;
				}
				else
				{
					*cmd = CMD_NONE;
				}

				// build DVHeader
				// resolve extended DMR IDs to base for callsign lookup
				uint32_t mySrcId = (uiSrcId > 9999999) ? uiSrcId / 100 : uiSrcId;
				uint32_t myRptrId = (uiRptrId > 9999999) ? uiRptrId / 100 : uiRptrId;
				char dstModule = DmrDstIdToModule(uiDstId);
				CCallsign csMY = CCallsign("", mySrcId);
				CCallsign rpt1 = CCallsign("", myRptrId);
				rpt1.SetCSModule(MMDVM_MODULE_ID);
				CCallsign rpt2 = m_ReflectorCallsign;
				rpt2.SetCSModule(dstModule);

				// and packet
				header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket(mySrcId, CCallsign("CQCQCQ"), rpt1, rpt2, uiStreamId, 0, 0));
				if ( header && header->IsValid() )
					return true;
			}
		}
	}
	return false;
}

bool CDmrmmdvmProtocol::IsValidDvFramePacket(const CIp &Ip, const CBuffer &Buffer, std::unique_ptr<CDvHeaderPacket> &header, std::array<std::unique_ptr<CDvFramePacket>, 3> &frames)
{
	uint8_t tag[] = { 'D','M','R','D' };

	if ( (Buffer.size() == 55) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		// frame details
		uint8_t uiFrameType = (Buffer.data()[15] & 0x30) >> 4;
		uint8_t uiSlot = (Buffer.data()[15] & 0x80) ? DMR_SLOT2 : DMR_SLOT1;
		uint8_t uiCallType = (Buffer.data()[15] & 0x40) ? DMR_PRIVATE_CALL : DMR_GROUP_CALL;
		if ( ((uiFrameType == DMRMMDVM_FRAMETYPE_VOICE) || (uiFrameType == DMRMMDVM_FRAMETYPE_VOICESYNC)) &&
				(uiCallType == DMR_GROUP_CALL) )
		{
			// crack DMR header
			//uint8_t uiSeqId = Buffer.data()[4];
			uint32_t uiSrcId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],0));
			uint32_t uiDstId = MAKEDWORD(MAKEWORD(Buffer.data()[10],Buffer.data()[9]),MAKEWORD(Buffer.data()[8],0));
			uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[14],Buffer.data()[13]),MAKEWORD(Buffer.data()[12],Buffer.data()[11]));
			uint8_t uiVoiceSeq = (Buffer.data()[15] & 0x0F);
			uint32_t uiStreamId;
			memcpy(&uiStreamId, &Buffer.data()[16], sizeof(uiStreamId));
			// encode slot into streamId high byte
			uiStreamId = (uiStreamId & 0x00FFFFFF) | ((uint32_t)uiSlot << 24);

			auto stream = GetStream(uiStreamId, &Ip);
			if ( !stream )
			{
				std::cout << "MMDVM: late entry voice frame, stream " << std::hex << ntohl(uiStreamId) << std::dec << " from " << Ip << std::endl;
				uint8_t cmd;

				// link/unlink command ?
				if ( uiDstId == 4000 )
				{
					cmd = CMD_UNLINK;
				}
				else if ( DmrDstIdToModule(uiDstId) != ' ' )
				{
					cmd = CMD_LINK;
				}
				else
				{
					cmd = CMD_NONE;
				}

				// build DVHeader — resolve extended DMR IDs to base
				uint32_t mySrcId = (uiSrcId > 9999999) ? uiSrcId / 100 : uiSrcId;
				uint32_t myRptrId = (uiRptrId > 9999999) ? uiRptrId / 100 : uiRptrId;
				CCallsign csMY = CCallsign("", mySrcId);
				CCallsign rpt1 = CCallsign("", myRptrId);
				rpt1.SetCSModule(MMDVM_MODULE_ID);
				CCallsign rpt2 = m_ReflectorCallsign;
				rpt2.SetCSModule(DmrDstIdToModule(uiDstId));

				// and packet
				header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket(mySrcId, CCallsign("CQCQCQ"), rpt1, rpt2, uiStreamId, 0, 0));

				if ( g_GateKeeper.MayTransmit(header->GetMyCallsign(), Ip, EProtocol::dmrmmdvm) )
				{
					// handle it
					OnDvHeaderPacketIn(header, Ip, cmd, uiCallType);
				}
			}

			// crack payload
			uint8_t dmrframe[33];
			uint8_t dmr3ambe[27];
			uint8_t dmrambe[9];
			uint8_t dmrsync[7];
			// get the 33 bytes ambe
			memcpy(dmrframe, &(Buffer.data()[20]), 33);
			// extract the 3 ambe frames
			memcpy(dmr3ambe, dmrframe, 14);
			dmr3ambe[13] &= 0xF0;
			dmr3ambe[13] |= (dmrframe[19] & 0x0F);
			memcpy(&dmr3ambe[14], &dmrframe[20], 13);
			// extract sync
			dmrsync[0] = dmrframe[13] & 0x0F;
			memcpy(&dmrsync[1], &dmrframe[14], 5);
			dmrsync[6] = dmrframe[19] & 0xF0;

			// debug
			//CBuffer dump;
			//dump.Set(dmrsync, 6);
			//dump.DebugDump(g_Reflector.m_DebugFile);

			// and create 3 dv frames
			// frame1
			memcpy(dmrambe, &dmr3ambe[0], 9);
			frames[0] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(dmrambe, dmrsync, uiStreamId, uiVoiceSeq, 1, false));

			// frame2
			memcpy(dmrambe, &dmr3ambe[9], 9);
			frames[1] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(dmrambe, dmrsync, uiStreamId, uiVoiceSeq, 2, false));

			// frame3
			memcpy(dmrambe, &dmr3ambe[18], 9);
			frames[2] = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(dmrambe, dmrsync, uiStreamId, uiVoiceSeq, 3, false));

			// check
			if (frames[0] && frames[1] && frames[2])
				return true;
		}
	}
	return false;
}

bool CDmrmmdvmProtocol::IsValidDvLastFramePacket(const CBuffer &Buffer, std::unique_ptr<CDvFramePacket> &frame)
{
	uint8_t tag[] = { 'D','M','R','D' };

	if ( (Buffer.size() == 55) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		// frame details
		uint8_t uiFrameType = (Buffer.data()[15] & 0x30) >> 4;
		uint8_t uiSlot = (Buffer.data()[15] & 0x80) ? DMR_SLOT2 : DMR_SLOT1;
		//uint8_t uiCallType = (Buffer.data()[15] & 0x40) ? DMR_PRIVATE_CALL : DMR_GROUP_CALL;
		uint8_t uiSlotType = Buffer.data()[15] & 0x0F;
		//std::cout << (int)uiSlot << std::endl;
		if ( (uiFrameType == DMRMMDVM_FRAMETYPE_DATASYNC) &&
				(uiSlotType == MMDVM_SLOTTYPE_TERMINATOR) )
		{
			// extract sync
			uint8_t dmrsync[7];
			dmrsync[0] = Buffer.data()[33] & 0x0F;
			memcpy(&dmrsync[1], &Buffer.data()[34], 5);
			dmrsync[6] = Buffer.data()[39] & 0xF0;
			// and check
			if ( (memcmp(dmrsync, g_DmrSyncMSData, sizeof(dmrsync)) == 0) ||
					(memcmp(dmrsync, g_DmrSyncBSData, sizeof(dmrsync)) == 0))
			{
				// get payload
				//CBPTC19696 bptc;
				//uint8_t lcdata[12];
				//bptc.decode(&(Buffer.data()[20]), lcdata);

				// crack DMR header
				//uint8_t uiSeqId = Buffer.data()[4];
				//uint32_t uiSrcId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],0));
				//uint32_t uiDstId = MAKEDWORD(MAKEWORD(Buffer.data()[10],Buffer.data()[9]),MAKEWORD(Buffer.data()[8],0));
				//uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[14],Buffer.data()[13]),MAKEWORD(Buffer.data()[12],Buffer.data()[11]));
				//uint8_t uiVoiceSeq = (Buffer.data()[15] & 0x0F);
				uint32_t uiStreamId;
				memcpy(&uiStreamId, &Buffer.data()[16], sizeof(uiStreamId));
				// encode slot into streamId high byte
				uiStreamId = (uiStreamId & 0x00FFFFFF) | ((uint32_t)uiSlot << 24);

				// dummy ambe
				uint8_t ambe[9];
				memset(ambe, 0, sizeof(ambe));


				// and packet
				frame = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(ambe, dmrsync, uiStreamId, 0, 0, true));
				if (frame)
					return true;
			}
		}
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////
// packet encoding helpers

void CDmrmmdvmProtocol::EncodeKeepAlivePacket(CBuffer *Buffer, std::shared_ptr<CClient>Client)
{
	uint8_t tag[] = { 'M','S','T','P','O','N','G' };

	Buffer->Set(tag, sizeof(tag));
	uint32_t uiDmrId = Client->GetCallsign().GetDmrid();
	Buffer->Append((uint8_t *)&uiDmrId, 4);
}

void CDmrmmdvmProtocol::EncodeAckPacket(CBuffer *Buffer, const CCallsign &Callsign)
{
	uint8_t tag[] = { 'R','P','T','A','C','K' };

	Buffer->Set(tag, sizeof(tag));
}

void CDmrmmdvmProtocol::EncodeConnectAckPacket(CBuffer *Buffer, const CCallsign &Callsign, uint32_t AuthSeed)
{
	uint8_t tag[] = { 'R','P','T','A','C','K' };

	Buffer->Set(tag, sizeof(tag));
	Buffer->Append(AuthSeed);
}

void CDmrmmdvmProtocol::EncodeNackPacket(CBuffer *Buffer, const CCallsign &Callsign)
{
	uint8_t tag[] = { 'M','S','T','N','A','K' };

	Buffer->Set(tag, sizeof(tag));
}

void CDmrmmdvmProtocol::EncodeClosePacket(CBuffer *Buffer, std::shared_ptr<CClient>Client)
{
	uint8_t tag[] = { 'M','S','T','C','L' };

	Buffer->Set(tag, sizeof(tag));
}


bool CDmrmmdvmProtocol::EncodeMMDVMHeaderPacket(const CDvHeaderPacket &Packet, uint8_t seqid, CBuffer *Buffer) const
{
	uint8_t tag[] = { 'D','M','R','D' };

	Buffer->Set(tag, sizeof(tag));

	// DMR header
	// uiSeqId
	Buffer->Append((uint8_t)seqid);
	// uiSrcId
	uint32_t uiSrcId = Packet.GetMyCallsign().GetDmrid();
	AppendDmrIdToBuffer(Buffer, uiSrcId);
	// uiDstId from TG map
	uint32_t uiDstId = ModuleToDmrDestId(Packet.GetRpt2Module());
	AppendDmrIdToBuffer(Buffer, uiDstId);
	// uiRptrId
	uint32_t uiRptrId = Packet.GetRpt1Callsign().GetDmrid();
	AppendDmrRptrIdToBuffer(Buffer, uiRptrId);
	// uiBitField
	uint8_t outSlot = m_TGMap.ModuleToTimeslot(Packet.GetRpt2Module());
	if (outSlot == 0) outSlot = DMR_SLOT2;  // default TS2
	uint8_t uiBitField =
		(DMRMMDVM_FRAMETYPE_DATASYNC << 4) |
		((outSlot == DMR_SLOT2) ? 0x80 : 0x00) |
		MMDVM_SLOTTYPE_HEADER;
	Buffer->Append((uint8_t)uiBitField);
	// uiStreamId
	uint32_t uiStreamId = Packet.GetStreamId();
	Buffer->Append((uint32_t)uiStreamId);

	// Payload
	AppendVoiceLCToBuffer(Buffer, uiSrcId, uiDstId);

	// BER
	Buffer->Append((uint8_t)0);

	// RSSI
	Buffer->Append((uint8_t)0);

	// done
	return true;
}

void CDmrmmdvmProtocol::EncodeMMDVMPacket(const CDvHeaderPacket &Header, const CDvFramePacket &DvFrame0, const CDvFramePacket &DvFrame1, const CDvFramePacket &DvFrame2, uint8_t seqid, CBuffer *Buffer) const
{
	uint8_t tag[] = { 'D','M','R','D' };
	Buffer->Set(tag, sizeof(tag));
	// DMR header
	// uiSeqId
	Buffer->Append((uint8_t)seqid);
	// uiSrcId
	uint32_t uiSrcId = Header.GetMyCallsign().GetDmrid();

    if(uiSrcId == 0){
		uiSrcId = DvFrame0.GetMyCallsign().GetDmrid();
	}
	if(uiSrcId == 0){
		uiSrcId = DvFrame1.GetMyCallsign().GetDmrid();
	}
	if(uiSrcId == 0){
		uiSrcId = DvFrame2.GetMyCallsign().GetDmrid();
	}
	if(uiSrcId == 0){
		uiSrcId = m_DefaultId;
	}

	AppendDmrIdToBuffer(Buffer, uiSrcId);
	// uiDstId from TG map
	uint32_t uiDstId = ModuleToDmrDestId(Header.GetRpt2Module());
	AppendDmrIdToBuffer(Buffer, uiDstId);
	// uiRptrId
	uint32_t uiRptrId = Header.GetRpt1Callsign().GetDmrid();
	AppendDmrRptrIdToBuffer(Buffer, uiRptrId);
	// uiBitField
	uint8_t outSlot = m_TGMap.ModuleToTimeslot(Header.GetRpt2Module());
	if (outSlot == 0) outSlot = DMR_SLOT2;  // default TS2
	uint8_t uiBitField =
		((outSlot == DMR_SLOT2) ? 0x80 : 0x00);
	if ( DvFrame0.GetDmrPacketId() == 0 )
	{
		uiBitField |= (DMRMMDVM_FRAMETYPE_VOICESYNC << 4);
	}
	else
	{
		uiBitField |= (DMRMMDVM_FRAMETYPE_VOICE << 4);
	}
	uiBitField |= (DvFrame0.GetDmrPacketId() & 0x0F);
	Buffer->Append((uint8_t)uiBitField);

	// uiStreamId
	uint32_t uiStreamId = Header.GetStreamId();
	Buffer->Append((uint32_t)uiStreamId);

	// Payload
	// frame0
	Buffer->ReplaceAt(20, DvFrame0.GetCodecData(ECodecType::dmr), 9);
	// 1/2 frame1
	Buffer->ReplaceAt(29, DvFrame1.GetCodecData(ECodecType::dmr), 5);
	Buffer->ReplaceAt(33, (uint8_t)(Buffer->at(33) & 0xF0));
	// 1/2 frame1
	Buffer->ReplaceAt(39, DvFrame1.GetCodecData(ECodecType::dmr)+4, 5);
	Buffer->ReplaceAt(39, (uint8_t)(Buffer->at(39) & 0x0F));
	// frame2
	Buffer->ReplaceAt(44, DvFrame2.GetCodecData(ECodecType::dmr), 9);

	// sync or embedded signaling
	ReplaceEMBInBuffer(Buffer, DvFrame0.GetDmrPacketId());

	// debug
	//CBuffer dump;
	//dump.Set(&(Buffer->data()[33]), 7);
	//dump.DebugDump(g_Reflector.m_DebugFile);

	// BER
	Buffer->Append((uint8_t)0);

	// RSSI
	Buffer->Append((uint8_t)0);
}


void CDmrmmdvmProtocol::EncodeLastMMDVMPacket(const CDvHeaderPacket &Packet, uint8_t seqid, CBuffer *Buffer) const
{
	uint8_t tag[] = { 'D','M','R','D' };

	Buffer->Set(tag, sizeof(tag));

	// DMR header
	// uiSeqId
	Buffer->Append((uint8_t)seqid);
	// uiSrcId
	uint32_t uiSrcId = Packet.GetMyCallsign().GetDmrid();
	AppendDmrIdToBuffer(Buffer, uiSrcId);
	// uiDstId from TG map
	uint32_t uiDstId = ModuleToDmrDestId(Packet.GetRpt2Module());
	AppendDmrIdToBuffer(Buffer, uiDstId);
	// uiRptrId
	uint32_t uiRptrId = Packet.GetRpt1Callsign().GetDmrid();
	AppendDmrRptrIdToBuffer(Buffer, uiRptrId);
	// uiBitField
	uint8_t outSlot = m_TGMap.ModuleToTimeslot(Packet.GetRpt2Module());
	if (outSlot == 0) outSlot = DMR_SLOT2;  // default TS2
	uint8_t uiBitField =
		(DMRMMDVM_FRAMETYPE_DATASYNC << 4) |
		((outSlot == DMR_SLOT2) ? 0x80 : 0x00) |
		MMDVM_SLOTTYPE_TERMINATOR;
	Buffer->Append((uint8_t)uiBitField);
	// uiStreamId
	uint32_t uiStreamId = Packet.GetStreamId();
	Buffer->Append((uint32_t)uiStreamId);

	// Payload
	AppendTerminatorLCToBuffer(Buffer, uiSrcId, uiDstId);

	// BER
	Buffer->Append((uint8_t)0);

	// RSSI
	Buffer->Append((uint8_t)0);
}


////////////////////////////////////////////////////////////////////////////////////////
// DestId to Module helper

char CDmrmmdvmProtocol::DmrDstIdToModule(uint32_t tg) const
{
	// first check TG map (static + dynamic)
	char mod = m_TGMap.TGToModule(tg);
	if (mod != ' ')
		return mod;
	// fallback: legacy 4001-4026 module linking
	if (tg > 4000 && tg < 4027)
	{
		const char m = 'A' + (tg - 4001U);
		if (g_Reflector.IsValidModule(m))
			return m;
	}
	return ' ';
}

uint32_t CDmrmmdvmProtocol::ModuleToDmrDestId(char m) const
{
	uint32_t tg = m_TGMap.ModuleToTG(m);
	if (tg != 0)
		return tg;
	// fallback: legacy
	return (uint32_t)(m - 'A') + 4001;
}

////////////////////////////////////////////////////////////////////////////////////////
// Buffer & LC helpers

void CDmrmmdvmProtocol::AppendVoiceLCToBuffer(CBuffer *buffer, uint32_t uiSrcId, uint32_t uiDstId) const
{
	uint8_t payload[33];

	// fill payload
	CBPTC19696 bptc;
	memset(payload, 0, sizeof(payload));
	// LC data
	uint8_t lc[12];
	{
		memset(lc, 0, sizeof(lc));
		// uiDstId
		lc[3] = (uint8_t)LOBYTE(HIWORD(uiDstId));
		lc[4] = (uint8_t)HIBYTE(LOWORD(uiDstId));
		lc[5] = (uint8_t)LOBYTE(LOWORD(uiDstId));
		// uiSrcId
		lc[6] = (uint8_t)LOBYTE(HIWORD(uiSrcId));
		lc[7] = (uint8_t)HIBYTE(LOWORD(uiSrcId));
		lc[8] = (uint8_t)LOBYTE(LOWORD(uiSrcId));
		// parity
		uint8_t parity[4];
		CRS129::encode(lc, 9, parity);
		lc[9]  = parity[2] ^ DMR_VOICE_LC_HEADER_CRC_MASK;
		lc[10] = parity[1] ^ DMR_VOICE_LC_HEADER_CRC_MASK;
		lc[11] = parity[0] ^ DMR_VOICE_LC_HEADER_CRC_MASK;
	}
	// sync
	memcpy(payload+13, g_DmrSyncBSData, sizeof(g_DmrSyncBSData));
	// slot type
	{
		// slot type
		uint8_t slottype[3];
		memset(slottype, 0, sizeof(slottype));
		slottype[0]  = (DMRMMDVM_REFLECTOR_COLOUR << 4) & 0xF0;
		slottype[0] |= (DMR_DT_VOICE_LC_HEADER  << 0) & 0x0FU;
		CGolay2087::encode(slottype);
		payload[12U] = (payload[12U] & 0xC0U) | ((slottype[0U] >> 2) & 0x3FU);
		payload[13U] = (payload[13U] & 0x0FU) | ((slottype[0U] << 6) & 0xC0U) | ((slottype[1U] >> 2) & 0x30U);
		payload[19U] = (payload[19U] & 0xF0U) | ((slottype[1U] >> 2) & 0x0FU);
		payload[20U] = (payload[20U] & 0x03U) | ((slottype[1U] << 6) & 0xC0U) | ((slottype[2U] >> 2) & 0x3CU);

	}
	// and encode
	bptc.encode(lc, payload);

	// and append
	buffer->Append(payload, sizeof(payload));
}

void CDmrmmdvmProtocol::AppendTerminatorLCToBuffer(CBuffer *buffer, uint32_t uiSrcId, uint32_t uiDstId) const
{
	uint8_t payload[33];

	// fill payload
	CBPTC19696 bptc;
	memset(payload, 0, sizeof(payload));
	// LC data
	uint8_t lc[12];
	{
		memset(lc, 0, sizeof(lc));
		// uiDstId
		lc[3] = (uint8_t)LOBYTE(HIWORD(uiDstId));
		lc[4] = (uint8_t)HIBYTE(LOWORD(uiDstId));
		lc[5] = (uint8_t)LOBYTE(LOWORD(uiDstId));
		// uiSrcId
		lc[6] = (uint8_t)LOBYTE(HIWORD(uiSrcId));
		lc[7] = (uint8_t)HIBYTE(LOWORD(uiSrcId));
		lc[8] = (uint8_t)LOBYTE(LOWORD(uiSrcId));
		// parity
		uint8_t parity[4];
		CRS129::encode(lc, 9, parity);
		lc[9]  = parity[2] ^ DMR_TERMINATOR_WITH_LC_CRC_MASK;
		lc[10] = parity[1] ^ DMR_TERMINATOR_WITH_LC_CRC_MASK;
		lc[11] = parity[0] ^ DMR_TERMINATOR_WITH_LC_CRC_MASK;
	}
	// sync
	memcpy(payload+13, g_DmrSyncBSData, sizeof(g_DmrSyncBSData));
	// slot type
	{
		// slot type
		uint8_t slottype[3];
		memset(slottype, 0, sizeof(slottype));
		slottype[0]  = (DMRMMDVM_REFLECTOR_COLOUR << 4) & 0xF0;
		slottype[0] |= (DMR_DT_TERMINATOR_WITH_LC  << 0) & 0x0FU;
		CGolay2087::encode(slottype);
		payload[12U] = (payload[12U] & 0xC0U) | ((slottype[0U] >> 2) & 0x3FU);
		payload[13U] = (payload[13U] & 0x0FU) | ((slottype[0U] << 6) & 0xC0U) | ((slottype[1U] >> 2) & 0x30U);
		payload[19U] = (payload[19U] & 0xF0U) | ((slottype[1U] >> 2) & 0x0FU);
		payload[20U] = (payload[20U] & 0x03U) | ((slottype[1U] << 6) & 0xC0U) | ((slottype[2U] >> 2) & 0x3CU);
	}
	// and encode
	bptc.encode(lc, payload);

	// and append
	buffer->Append(payload, sizeof(payload));
}

void CDmrmmdvmProtocol::ReplaceEMBInBuffer(CBuffer *buffer, uint8_t uiDmrPacketId) const
{
	// voice packet A ?
	if ( uiDmrPacketId == 0 )
	{
		// sync
		buffer->ReplaceAt(33, (uint8_t)(buffer->at(33) | (g_DmrSyncBSVoice[0] & 0x0F)));
		buffer->ReplaceAt(34, g_DmrSyncBSVoice+1, 5);
		buffer->ReplaceAt(39, (uint8_t)(buffer->at(39) | (g_DmrSyncBSVoice[6] & 0xF0)));
	}
	// voice packet B,C,D,E ?
	else if ( (uiDmrPacketId >= 1) && (uiDmrPacketId <= 4 ) )
	{
		// EMB LC
		uint8_t emb[2];
		emb[0]  = (DMRMMDVM_REFLECTOR_COLOUR << 4) & 0xF0;
		//emb[0] |= PI ? 0x08U : 0x00;
		//emb[0] |= (LCSS << 1) & 0x06;
		emb[1]  = 0x00;
		// encode
		CQR1676::encode(emb);
		// and append
		buffer->ReplaceAt(33, (uint8_t)((buffer->at(33) & 0xF0) | ((emb[0U] >> 4) & 0x0F)));
		buffer->ReplaceAt(34, (uint8_t)((emb[0U] << 4) & 0xF0));
		buffer->ReplaceAt(35, (uint8_t)0);
		buffer->ReplaceAt(36, (uint8_t)0);
		buffer->ReplaceAt(37, (uint8_t)0);
		buffer->ReplaceAt(38, (uint8_t)((emb[1U] >> 4) & 0x0F));
		buffer->ReplaceAt(39, (uint8_t)((buffer->at(39) & 0x0F) | ((emb[1U] << 4) & 0xF0)));
	}
	// voice packet F
	else
	{
		uint8_t emb[2];
		emb[0]  = (DMRMMDVM_REFLECTOR_COLOUR << 4) & 0xF0;
		//emb[0] |= PI ? 0x08U : 0x00;
		//emb[0] |= (LCSS << 1) & 0x06;
		emb[1]  = 0x00;
		// encode
		CQR1676::encode(emb);
		// and append
		buffer->ReplaceAt(33, (uint8_t)((buffer->at(33) & 0xF0) | ((emb[0U] >> 4) & 0x0F)));
		buffer->ReplaceAt(34, (uint8_t)((emb[0U] << 4) & 0xF0));
		buffer->ReplaceAt(35, (uint8_t)0);
		buffer->ReplaceAt(36, (uint8_t)0);
		buffer->ReplaceAt(37, (uint8_t)0);
		buffer->ReplaceAt(38, (uint8_t)((emb[1U] >> 4) & 0x0F));
		buffer->ReplaceAt(39, (uint8_t)((buffer->at(39) & 0x0F) | ((emb[1U] << 4) & 0xF0)));
	}
}

void CDmrmmdvmProtocol::AppendDmrIdToBuffer(CBuffer *buffer, uint32_t id) const
{
	buffer->Append((uint8_t)LOBYTE(HIWORD(id)));
	buffer->Append((uint8_t)HIBYTE(LOWORD(id)));
	buffer->Append((uint8_t)LOBYTE(LOWORD(id)));
}

void CDmrmmdvmProtocol::AppendDmrRptrIdToBuffer(CBuffer *buffer, uint32_t id) const
{
	buffer->Append((uint8_t)HIBYTE(HIWORD(id)));
	buffer->Append((uint8_t)LOBYTE(HIWORD(id)));
	buffer->Append((uint8_t)HIBYTE(LOWORD(id)));
	buffer->Append((uint8_t)LOBYTE(LOWORD(id)));
}

////////////////////////////////////////////////////////////////////////////////////////
// auth helpers

bool CDmrmmdvmProtocol::VerifyAuthHash(uint32_t rawDmrId, const uint8_t *clientHash)
{
	// find the pending auth salt for this DMR ID
	auto it = m_PendingAuth.find(rawDmrId);
	if (it == m_PendingAuth.end())
	{
		std::cerr << "MMDVM: auth failed for DMR ID " << rawDmrId << " - no pending auth" << std::endl;
		return false;
	}
	uint32_t salt = it->second.salt;
	m_PendingAuth.erase(it);

	// resolve base ID and find password
	uint32_t baseId = ResolveBaseId(rawDmrId);
	std::string password;
	{
		std::lock_guard<std::mutex> lock(m_PasswordMutex);
		auto pit = m_Passwords.find(baseId);
		if (pit == m_Passwords.end())
		{
			std::cerr << "MMDVM: auth failed for DMR ID " << rawDmrId << " (base " << baseId << ") - no password configured" << std::endl;
			return false;
		}
		password = pit->second;
	}

	// empty password = DMR ID is whitelisted, accept any hash
	if (password.empty())
		return true;

	// compute expected hash: SHA256(salt_bytes + password_bytes)
	// salt was sent via Buffer::Append(uint32_t) which uses native byte order (memcpy)
	// the client reads the 4 raw bytes and hashes them — so we must use the same byte order
	uint8_t saltBytes[4];
	memcpy(saltBytes, &salt, 4);  // native byte order, matches what was sent in RPTACK

	std::vector<uint8_t> input;
	input.insert(input.end(), saltBytes, saltBytes + 4);
	input.insert(input.end(), password.begin(), password.end());

	uint8_t expectedHash[32];
	CSHA256 sha256;
	sha256.buffer(input.data(), input.size(), expectedHash);

	// the client sends 32 bytes of hash at Buffer.data()[8..39]
	// constant-time comparison
	uint8_t diff = 0;
	for (int i = 0; i < 32; i++)
		diff |= clientHash[i] ^ expectedHash[i];

	return (diff == 0);
}

void CDmrmmdvmProtocol::CleanupPendingAuth()
{
	auto now = std::chrono::steady_clock::now();
	for (auto it = m_PendingAuth.begin(); it != m_PendingAuth.end(); )
	{
		if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.created).count() > MMDVM_AUTH_TIMEOUT)
			it = m_PendingAuth.erase(it);
		else
			++it;
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// user management

bool CDmrmmdvmProtocol::AddUser(uint32_t baseId, const std::string &password)
{
	{
		std::lock_guard<std::mutex> lock(m_PasswordMutex);
		m_Passwords[baseId] = password;
	}
	IniAddUser(baseId, password);
	std::cout << "MMDVM: added user DMR ID " << baseId << std::endl;
	return true;
}

bool CDmrmmdvmProtocol::AddUserByCallsign(const std::string &callsign, const std::string &password)
{
	g_LDid.Lock();
	uint32_t dmrid = g_LDid.FindDmrid(CCallsign(callsign).GetKey());
	g_LDid.Unlock();
	if (dmrid == 0)
		return false;
	return AddUser(dmrid, password);
}

bool CDmrmmdvmProtocol::RemoveUser(uint32_t baseId)
{
	{
		std::lock_guard<std::mutex> lock(m_PasswordMutex);
		auto it = m_Passwords.find(baseId);
		if (it == m_Passwords.end())
			return false;
		m_Passwords.erase(it);
	}
	IniRemoveUser(baseId);
	std::cout << "MMDVM: removed user DMR ID " << baseId << std::endl;
	return true;
}

bool CDmrmmdvmProtocol::RemoveUserByCallsign(const std::string &callsign)
{
	g_LDid.Lock();
	uint32_t dmrid = g_LDid.FindDmrid(CCallsign(callsign).GetKey());
	g_LDid.Unlock();
	if (dmrid == 0)
		return false;
	return RemoveUser(dmrid);
}

std::vector<CDmrmmdvmProtocol::SUserEntry> CDmrmmdvmProtocol::GetUsers() const
{
	std::lock_guard<std::mutex> lock(m_PasswordMutex);
	std::vector<SUserEntry> users;
	for (const auto &[id, pw] : m_Passwords)
	{
		SUserEntry e;
		e.dmrid = id;
		// resolve callsign (try extended ID first, fall back to base ID)
		g_LDid.Lock();
		const UCallsign *ucs = g_LDid.FindCallsign(id);
		if (!ucs && id > 9999999)
			ucs = g_LDid.FindCallsign(id / 100);
		g_LDid.Unlock();
		if (ucs)
			e.callsign = std::string(ucs->c, strnlen(ucs->c, CALLSIGN_LEN));
		users.push_back(e);
	}
	return users;
}

////////////////////////////////////////////////////////////////////////////////////////
// INI persistence

bool CDmrmmdvmProtocol::IniAddUser(uint32_t baseId, const std::string &password)
{
	const std::string &path = g_Reflector.GetConfigPath();
	if (path.empty()) return false;

	std::lock_guard<std::mutex> lock(m_IniMutex);

	std::string key = std::to_string(baseId);

	// Read INI file
	std::ifstream in(path);
	if (!in.is_open()) return false;
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(in, line))
		lines.push_back(line);
	in.close();

	// Find [MMDVM] section and insert/update user line
	bool inSection = false;
	int insertPos = -1;
	int lastKvPos = -1;
	for (size_t i = 0; i < lines.size(); i++)
	{
		std::string trimmed = lines[i];
		// trim leading whitespace
		size_t start = trimmed.find_first_not_of(" \t");
		if (start != std::string::npos) trimmed = trimmed.substr(start);

		if (!trimmed.empty() && trimmed[0] == '[')
		{
			if (inSection) break; // next section
			if (trimmed.find("[MMDVM]") == 0) inSection = true;
			continue;
		}
		if (!inSection) continue;

		// Check if this key already exists (key = value)
		auto eq = trimmed.find('=');
		if (eq != std::string::npos)
		{
			std::string k = trimmed.substr(0, eq);
			// trim trailing whitespace from key
			size_t end = k.find_last_not_of(" \t");
			if (end != std::string::npos) k = k.substr(0, end + 1);
			if (k == key)
			{
				// Update existing line
				lines[i] = key + " = " + password;
				// Write back
				std::ofstream out(path);
				if (!out.is_open()) return false;
				for (const auto &l : lines) out << l << "\n";
				return true;
			}
			lastKvPos = (int)i;
		}
	}

	// Insert after last key=value line in section (not before next section header)
	insertPos = (lastKvPos >= 0) ? lastKvPos + 1 : (int)lines.size();
	lines.insert(lines.begin() + insertPos, key + " = " + password);

	std::ofstream out(path);
	if (!out.is_open()) return false;
	for (const auto &l : lines) out << l << "\n";
	return true;
}

bool CDmrmmdvmProtocol::IniRemoveUser(uint32_t baseId)
{
	const std::string &path = g_Reflector.GetConfigPath();
	if (path.empty()) return false;

	std::lock_guard<std::mutex> lock(m_IniMutex);

	std::string key = std::to_string(baseId);

	std::ifstream in(path);
	if (!in.is_open()) return false;
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(in, line))
		lines.push_back(line);
	in.close();

	bool inSection = false;
	bool found = false;
	for (auto it = lines.begin(); it != lines.end(); )
	{
		std::string trimmed = *it;
		size_t start = trimmed.find_first_not_of(" \t");
		if (start != std::string::npos) trimmed = trimmed.substr(start);

		if (!trimmed.empty() && trimmed[0] == '[')
		{
			if (inSection) break; // left section
			if (trimmed.find("[MMDVM]") == 0) inSection = true;
			++it;
			continue;
		}
		if (inSection && !trimmed.empty() && trimmed[0] != '#')
		{
			auto eq = trimmed.find('=');
			if (eq != std::string::npos)
			{
				std::string k = trimmed.substr(0, eq);
				size_t end = k.find_last_not_of(" \t");
				if (end != std::string::npos) k = k.substr(0, end + 1);
				if (k == key)
				{
					it = lines.erase(it);
					found = true;
					continue;
				}
			}
		}
		++it;
	}

	if (!found) return false;

	std::ofstream out(path);
	if (!out.is_open()) return false;
	for (const auto &l : lines) out << l << "\n";
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// RPTC config parsing

void CDmrmmdvmProtocol::ParseConfigPacket(const CBuffer &Buffer, const CIp &Ip)
{
	if (Buffer.size() < 302) return;

	SPeerInfo info;
	uint32_t uiRptrId = MAKEDWORD(MAKEWORD(Buffer.data()[7],Buffer.data()[6]),MAKEWORD(Buffer.data()[5],Buffer.data()[4]));
	info.dmrid = uiRptrId;

	auto extractField = [&](size_t offset, size_t len) -> std::string {
		std::string s((const char *)&Buffer.data()[offset], len);
		// trim trailing spaces and nulls
		while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
		return s;
	};

	info.callsign = extractField(8, 8);
	info.rxFreq = extractField(16, 9);
	info.txFreq = extractField(25, 9);
	info.txPower = extractField(34, 2);
	info.colorCode = extractField(36, 2);
	info.latitude = extractField(38, 8);
	info.longitude = extractField(46, 9);
	info.height = extractField(55, 3);
	info.location = extractField(58, 20);
	info.description = extractField(78, 19);
	info.slots = extractField(97, 1);
	info.url = extractField(98, 124);
	info.softwareId = extractField(222, 40);
	info.packageId = extractField(262, 40);
	info.populated = true;

	m_PeerInfoMap[Ip.GetAddress()] = info;

	std::cout << "MMDVM: config from " << info.callsign << ": " << info.softwareId
	          << " @ " << info.location << " (" << info.rxFreq << "/" << info.txFreq << " Hz)" << std::endl;
}
