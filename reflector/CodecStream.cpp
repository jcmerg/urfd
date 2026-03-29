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

////////////////////////////////////////////////////////////////////////////////////////
// constructor

CCodecStream::CCodecStream(CPacketStream *PacketStream, char module) : m_CSModule(module), m_IsOpen(false), m_EventFD(-1)
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
				default:                     shortName = owner->GetProtocolName(); break;
			}
			msg = std::string("via ") + shortName;

			// Append TG/DG-ID info from config mappings
			const auto &cfg = g_Configure.GetData();
			if (proto == EProtocol::svxreflector)
			{
				for (auto it = cfg.begin(); it != cfg.end(); ++it)
				{
					const std::string &key = it.key();
					if (key.substr(0, 5) == "svxTG" && it->is_string())
					{
						std::string val = it->get<std::string>();
						if (!val.empty() && val[0] == m_CSModule)
						{
							msg += " TG" + key.substr(5);
							break;
						}
					}
				}
			}
			else if (proto == EProtocol::mmdvmclient)
			{
				for (auto it = cfg.begin(); it != cfg.end(); ++it)
				{
					const std::string &key = it.key();
					if (key.substr(0, 10) == "mmdvmcliTG" && it->is_string())
					{
						std::string val = it->get<std::string>();
						if (!val.empty() && val[0] == m_CSModule)
						{
							msg += " TG" + key.substr(10);
							break;
						}
					}
				}
			}
			else if (proto == EProtocol::ysf)
			{
				int dgid = 10 + (m_CSModule - 'A');
				msg += " DG" + std::to_string(dgid);
			}

			// D-Star text message is max 20 chars
			if (msg.size() > 20)
				msg.resize(20);
		}

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

			int fd = g_TCServer.GetFD(Frame->GetCodecPacket()->module);
			if (fd < 0)
				return;

			Frame->m_rtTimer.start();
			if (g_TCServer.Send(Frame->GetCodecPacket()))
				return;

			m_LocalQueue.Push(std::move(m_Queue.Pop()));
		}
		else
		{
			m_Queue.Pop();
		}
	}
}
