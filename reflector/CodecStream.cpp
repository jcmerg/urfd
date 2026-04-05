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
#include <unistd.h>
#include <poll.h>
#include <sys/eventfd.h>

#include "Global.h"
#include "DVFramePacket.h"
#include "PacketStream.h"
#include "CodecStream.h"
#include "Reflector.h"
#include "DStarSlowData.h"
#include "MMDVMClientProtocol.h"
#include "SvxReflectorProtocol.h"
#include "NXDNProtocol.h"
#include "DCSClientProtocol.h"
#include "DExtraClientProtocol.h"
#include "DPlusClientProtocol.h"
#include "YSFClientProtocol.h"

////////////////////////////////////////////////////////////////////////////////////////
// constructor

CCodecStream::CCodecStream(CPacketStream *PacketStream, char module) : m_CSModule(module), m_IsOpen(false), m_EventFD(-1),
	m_uiStreamId(0), m_uiPort(0), m_uiPid(0), m_eCodecIn(ECodecType::none),
	m_RTMin(-1), m_RTMax(-1), m_RTSum(0), m_RTCount(0),
	m_uiTotalPackets(0), m_uiMismatchCount(0), m_uiSuperframeCount(0)
{
	m_PacketStream = PacketStream;
	m_EventFD = eventfd(0, EFD_NONBLOCK);
	if (m_EventFD < 0)
		std::cerr << "CodecStream[" << module << "]: eventfd creation failed" << std::endl;
}

////////////////////////////////////////////////////////////////////////////////////////
// destructor

void CCodecStream::Push(std::unique_ptr<CDvFramePacket> p)
{
	m_Queue.Push(std::move(p));
	if (m_EventFD >= 0)
	{
		uint64_t val = 1;
		write(m_EventFD, &val, sizeof(val));
	}
}

CCodecStream::~CCodecStream()
{
	// kill the thread
	keep_running = false;
	// wake up poll in Task
	if (m_EventFD >= 0)
	{
		uint64_t val = 1;
		write(m_EventFD, &val, sizeof(val));
	}
	if ( m_Future.valid() )
	{
		m_Future.get();
	}
	if (m_EventFD >= 0)
	{
		close(m_EventFD);
		m_EventFD = -1;
	}
}

void CCodecStream::ResetStats(uint16_t streamid, ECodecType type)
{
	// Drain stale packets from previous stream
	while (!m_LocalQueue.IsEmpty())
		m_LocalQueue.Pop();

	m_IsOpen = true;
	keep_running = true;
	m_uiStreamId = streamid;
	m_uiPid = 0;
	m_eCodecIn = type;
	m_RTMin = -1;
	m_RTMax = -1;
	m_RTSum = 0;
	m_RTCount = 0;
	m_uiTotalPackets = 0;
	m_uiMismatchCount = 0;
	m_uiSuperframeCount = 0;

	// Initialize D-Star slow data for transcoded (non-D-Star) streams
	if (type != ECodecType::dstar && m_PacketStream)
	{
		CCallsign my = m_PacketStream->GetUserCallsign();
		CCallsign rpt1 = g_Reflector.GetCallsign();
		rpt1.SetCSModule(m_CSModule);
		CCallsign rpt2 = g_Reflector.GetCallsign();
		rpt2.SetCSModule('G');

		// Build message: "via <Protocol> <TG/info>"
		std::string msg;
		auto owner = m_PacketStream->GetOwnerClient();
		if (owner)
		{
			EProtocol proto = owner->GetProtocol();
			// Short protocol names to fit 20-char D-Star text
			const char *shortName;
			switch (proto) {
				case EProtocol::svxreflector: shortName = "SVX"; break;
				case EProtocol::mmdvmclient: shortName = "DMR"; break;
				case EProtocol::ysf:         shortName = "YSF"; break;
				case EProtocol::dmrmmdvm:    shortName = "DMR"; break;
				case EProtocol::dmrplus:     shortName = "DMR+"; break;
				case EProtocol::m17:         shortName = "M17"; break;
				case EProtocol::p25:         shortName = "P25"; break;
				case EProtocol::nxdn:        shortName = "NXDN"; break;
				case EProtocol::usrp:        shortName = "USRP"; break;
				case EProtocol::dcsclient:   shortName = "DCS"; break;
				case EProtocol::dextraclient:shortName = "DExtra"; break;
				case EProtocol::dplusclient: shortName = "DPlus"; break;
				case EProtocol::ysfclient:   shortName = "YSF"; break;
				default:                     shortName = owner->GetProtocolName(); break;
			}
			msg = std::string("via ") + shortName;

			// For TG-based protocols, TG is appended later via UpdateSlowDataTG()
			// because the source TG is not yet known at stream-open time
			if (proto == EProtocol::svxreflector || proto == EProtocol::mmdvmclient)
			{
				// msg stays as "via DMR" or "via SVX" — TG appended by UpdateSlowDataTG
			}
			else if (proto == EProtocol::ysf)
			{
				int dgid = 10 + (m_CSModule - 'A');
				msg += " DG" + std::to_string(dgid);
			}
			else if (proto == EProtocol::nxdn)
			{
				uint8_t ran = CNXDNProtocol::ModuleToRAN(m_CSModule);
				if (ran != 0)
					msg += " RAN" + std::to_string(ran);
			}
			else if (proto == EProtocol::dcsclient || proto == EProtocol::dextraclient || proto == EProtocol::dplusclient)
			{
				// Show remote host + module, e.g. "via DCS DCS002 A"
				auto &protocols = g_Reflector.GetProtocols();
				protocols.Lock();
				auto *p = protocols.FindByType(proto);
				if (p)
				{
					if (proto == EProtocol::dcsclient)
					{
						auto *cp = static_cast<CDcsClientProtocol *>(p);
						for (auto &m : cp->GetMappings())
							if (m.localModule == m_CSModule)
							{
								// extract reflector name from host (e.g. "dcs002.xreflector.net" -> "DCS002")
								std::string h = m.host;
								auto dot = h.find('.');
								if (dot != std::string::npos) h = h.substr(0, dot);
								for (auto &c : h) c = toupper(c);
								msg = "via " + h + " " + m.remoteModule;
								break;
							}
					}
					else if (proto == EProtocol::dextraclient)
					{
						auto *cp = static_cast<CDExtraClientProtocol *>(p);
						for (auto &m : cp->GetMappings())
							if (m.localModule == m_CSModule)
							{
								std::string h = m.host;
								auto dot = h.find('.');
								if (dot != std::string::npos) h = h.substr(0, dot);
								for (auto &c : h) c = toupper(c);
								msg = "via " + h + " " + m.remoteModule;
								break;
							}
					}
					else
					{
						auto *cp = static_cast<CDPlusClientProtocol *>(p);
						for (auto &m : cp->GetMappings())
							if (m.localModule == m_CSModule)
							{
								std::string h = m.host;
								auto dot = h.find('.');
								if (dot != std::string::npos) h = h.substr(0, dot);
								for (auto &c : h) c = toupper(c);
								msg = "via " + h + " " + m.remoteModule;
								break;
							}
					}
				}
				protocols.Unlock();
			}
			else if (proto == EProtocol::ysfclient)
			{
				// Show DG-ID if configured, e.g. "via YSF DG15"
				auto &protocols = g_Reflector.GetProtocols();
				protocols.Lock();
				auto *p = protocols.FindByType(EProtocol::ysfclient);
				if (p)
				{
					auto *cp = static_cast<CYsfClientProtocol *>(p);
					for (auto &m : cp->GetMappings())
						if (m.localModule == m_CSModule && m.dgid > 0)
						{
							msg += " DG" + std::to_string(m.dgid);
							break;
						}
				}
				protocols.Unlock();
			}

			// D-Star text message is max 20 chars
			if (msg.size() > 20)
				msg.resize(20);
		}

		// Look up operator name from DMR or NXDN ID database via callsign
		std::string nameMsg;
		g_LDid.Lock();
		uint32_t dmrid = g_LDid.FindDmrid(my.GetKey());
		if (dmrid != 0)
		{
			std::string name = g_LDid.FindName(dmrid);
			if (!name.empty())
				nameMsg = name;
		}
		g_LDid.Unlock();
		if (nameMsg.empty())
		{
			g_LNid.Lock();
			uint16_t nxdnid = g_LNid.FindNXDNid(my.GetKey());
			if (nxdnid != 0)
			{
				std::string name = g_LNid.FindName(nxdnid);
				if (!name.empty())
					nameMsg = name;
			}
			g_LNid.Unlock();
		}
		if (nameMsg.size() > 20)
			nameMsg.resize(20);

		m_SlowDataMsg1 = msg;
		m_SlowDataMsg2 = nameMsg;
		m_SlowData.Init(my, rpt1, rpt2, msg);
	}
}

void CCodecStream::ReportStats()
{
	m_IsOpen = false;
	// display stats
	if (m_RTCount > 0)
	{
		double min = 1000.0 * m_RTMin;
		double max = 1000.0 * m_RTMax;
		double ave = 1000.0 * m_RTSum / double(m_RTCount);
		auto prec = std::cout.precision();
		std::cout.precision(1);
		std::cout << std::fixed << "TC round-trip time(ms): " << min << '/' << ave << '/' << max << ", " << m_RTCount << " total packets" << std::endl;
		std::cout.precision(prec);
	}
}

void CCodecStream::UpdateSlowDataTG(uint32_t tg)
{
	if (tg == 0 || m_SlowDataMsg1.empty())
		return;

	// Append TG to existing message (e.g. "via DMR" -> "via DMR TG2625")
	std::string updated = m_SlowDataMsg1 + " TG" + std::to_string(tg);
	if (updated.size() > 20)
		updated.resize(20);
	m_SlowDataMsg1 = updated;

	// Re-init slow data with updated message
	CCallsign my = m_PacketStream ? m_PacketStream->GetUserCallsign() : CCallsign();
	CCallsign rpt1 = g_Reflector.GetCallsign();
	rpt1.SetCSModule(m_CSModule);
	CCallsign rpt2 = g_Reflector.GetCallsign();
	rpt2.SetCSModule('G');
	m_SlowData.Init(my, rpt1, rpt2, m_SlowDataMsg1);
}

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CCodecStream::InitCodecStream()
{
	keep_running = true;
	try
	{
		m_Future = std::async(std::launch::async, &CCodecStream::Thread, this);
	}
	catch(const std::exception& e)
	{
		std::cerr << "Could not start Codec processing on module '" << m_CSModule << "': " << e.what() << std::endl;
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////
// thread

void CCodecStream::Thread()
{
	while (keep_running)
	{
		Task();
	}
}

void CCodecStream::Task(void)
{
	// Drain all pending transcoder responses (prevents TCP buffer buildup / deadlock)
	STCPacket pack;
	bool receivedAny = false;
	while (g_TCServer.Receive(m_CSModule, &pack, 0))
	{
		receivedAny = true;
		if (m_IsOpen && pack.streamid == m_uiStreamId && !m_LocalQueue.IsEmpty())
		{
			auto Packet = m_LocalQueue.Pop();

			if ((pack.streamid == Packet->GetCodecPacket()->streamid) && (pack.sequence == Packet->GetCodecPacket()->sequence))
			{
				auto rt = Packet->m_rtTimer.time();
				if (0 == m_RTCount)
				{
					m_RTMin = rt;
					m_RTMax = rt;
				}
				else
				{
					if (rt < m_RTMin)
						m_RTMin = rt;
					else if (rt > m_RTMax)
						m_RTMax = rt;
				}
				m_RTSum += rt;
				m_RTCount++;

				Packet->SetCodecData(&pack);
				// Set D-Star sync and slow data for transcoded streams
				if (ECodecType::dstar != Packet->GetCodecIn())
				{
					uint8_t frameInSuper = Packet->GetPacketId() % 21;
					if (frameInSuper == 0)
					{
						const uint8_t DStarSync[] = { 0x55, 0x2D, 0x16 };
						Packet->SetDvData(DStarSync);
						m_uiSuperframeCount++;
						// Rotate slow data text every ~5s (12 superframes)
						if (!m_SlowDataMsg2.empty() && m_uiSuperframeCount % 24 == 12)
							m_SlowData.SetMessage(m_SlowDataMsg2);
						else if (!m_SlowDataMsg2.empty() && m_uiSuperframeCount % 24 == 0)
							m_SlowData.SetMessage(m_SlowDataMsg1);
					}
					else if (m_SlowData.IsReady())
					{
						Packet->SetDvData(m_SlowData.GetSlowData(frameInSuper, m_uiSuperframeCount));
					}
				}

				m_PacketStream->ReturnPacket(std::move(Packet));
			}
		}
		else if (m_IsOpen && pack.streamid != m_uiStreamId)
		{
			if (m_uiMismatchCount++ == 0)
				std::cerr << "Transcoder mismatch on module " << m_CSModule << " (stale packet discarded)" << std::endl;
		}
	}

	if (!receivedAny && m_Queue.IsEmpty())
	{
		// Nothing to receive and nothing to send — wait for eventfd signal
		struct pollfd pfd = { m_EventFD, POLLIN, 0 };
		poll(&pfd, 1, 20);
		if (pfd.revents & POLLIN)
		{
			uint64_t val;
			read(m_EventFD, &val, sizeof(val));
		}
	}

	// Send queued packets to transcoder
	while (!m_Queue.IsEmpty())
	{
		auto &Frame = m_Queue.Front();

		if (m_IsOpen)
		{
			Frame->SetTCParams(m_uiTotalPackets++, m_CSModule);

			int fd = g_TCServer.GetFD(m_CSModule);
			if (fd < 0)
			{
				// Transcoder disconnected — drain queues silently
				while (!m_Queue.IsEmpty()) m_Queue.Pop();
				while (!m_LocalQueue.IsEmpty()) m_LocalQueue.Pop();
				return;
			}

			Frame->m_rtTimer.start();
			if (g_TCServer.Send(Frame->GetCodecPacket()))
			{
				// Send failed — drain queues
				while (!m_Queue.IsEmpty()) m_Queue.Pop();
				while (!m_LocalQueue.IsEmpty()) m_LocalQueue.Pop();
				return;
			}

			m_LocalQueue.Push(std::move(m_Queue.Pop()));
		}
		else
		{
			m_Queue.Pop();
		}
	}
}
