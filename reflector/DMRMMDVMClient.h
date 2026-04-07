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

#include "Defines.h"
#include "Client.h"

class CDmrmmdvmClient : public CClient
{
public:
	// constructors
	CDmrmmdvmClient();
	CDmrmmdvmClient(const CCallsign &, const CIp &, char = ' ');
	CDmrmmdvmClient(const CDmrmmdvmClient &);

	// destructor
	virtual ~CDmrmmdvmClient() {};

	// identity
	EProtocol GetProtocol(void) const           { return EProtocol::dmrmmdvm; }
	const char *GetProtocolName(void) const     { return "MMDVM"; }
	bool IsNode(void) const                     { return true; }

	// status
	bool IsAlive(void) const;

	// raw DMR ID (base or with extension, as received in RPTL/RPTK)
	void SetRawDmrId(uint32_t id)               { m_uiRawDmrId = id; }
	uint32_t GetRawDmrId(void) const             { return m_uiRawDmrId; }

	// dual-slot module linking (TS1 and TS2 independently)
	void SetSlotModule(uint8_t slot, char mod)  { if (slot >= 1 && slot <= 2) m_SlotModule[slot-1] = mod; }
	char GetSlotModule(uint8_t slot) const      { return (slot >= 1 && slot <= 2) ? m_SlotModule[slot-1] : ' '; }
	bool HasReflectorModule(void) const override { return m_SlotModule[0] != ' ' || m_SlotModule[1] != ' '; }
	char GetReflectorModule(void) const override { return (m_SlotModule[0] != ' ') ? m_SlotModule[0] : m_SlotModule[1]; }
	bool IsLinkedTo(char mod) const override     { return m_SlotModule[0] == mod || m_SlotModule[1] == mod; }

	// reporting
	void WriteXml(std::ofstream &) override;

protected:
	uint32_t m_uiRawDmrId = 0;
	char m_SlotModule[2] = {' ', ' '};  // index 0=TS1, index 1=TS2
};
