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

#include <atomic>
#include <future>
#include <sys/eventfd.h>

#include "DVFramePacket.h"
#include "SafePacketQueue.h"
#include "DStarSlowData.h"

////////////////////////////////////////////////////////////////////////////////////////
// class

class CPacketStream;

class CCodecStream
{
public:
	// constructor
	CCodecStream(CPacketStream *packetstream, char module);
	bool InitCodecStream();

	void ResetStats(uint16_t streamid, ECodecType codectype);
	void ReportStats();

	// destructor
	virtual ~CCodecStream();

	// get
	uint16_t GetStreamId(void) const          { return m_uiStreamId; }
	bool     IsOpen(void) const               { return m_IsOpen; }
	ECodecType GetCodecIn(void) const         { return m_eCodecIn; }
	char     GetModule(void) const            { return m_CSModule; }

	// stats accessors (for admin interface)
	double   GetRTMin(void) const             { return m_RTMin; }
	double   GetRTMax(void) const             { return m_RTMax; }
	double   GetRTAvg(void) const             { return m_RTCount > 0 ? m_RTSum / m_RTCount : 0.0; }
	unsigned int GetRTCount(void) const       { return m_RTCount; }
	uint32_t GetTotalPackets(void) const      { return m_uiTotalPackets; }
	uint32_t GetMismatchCount(void) const     { return m_uiMismatchCount; }

	// task
	void Thread(void);
	void Task(void);

	// pass-through (signals eventfd to wake up Task)
	void Push(std::unique_ptr<CDvFramePacket> p);

protected:
	// identity
	const char      m_CSModule;
	// state
	std::atomic<bool> m_IsOpen;
	// data
	uint16_t        m_uiStreamId;
	uint16_t        m_uiPort;
	uint8_t         m_uiPid;
	ECodecType      m_eCodecIn;

	// associated packet stream
	CPacketStream  *m_PacketStream;

	// queues
	CSafePacketQueue<std::unique_ptr<CDvFramePacket>> m_LocalQueue, m_Queue;

	// thread
	std::atomic<bool> keep_running;
	std::future<void> m_Future;
	int m_EventFD;

	// D-Star slow data for transcoded streams
	CDStarSlowData m_SlowData;
	uint32_t     m_uiSuperframeCount;

	// statistics
	double       m_RTMin;
	double       m_RTMax;
	double       m_RTSum;
	unsigned int m_RTCount;
	uint32_t     m_uiTotalPackets;
	uint32_t     m_uiMismatchCount;
};
