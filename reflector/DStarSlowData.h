#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "Callsign.h"

class CDStarSlowData
{
public:
	CDStarSlowData();

	// Initialize with callsign data and optional text message
	void Init(const CCallsign &my, const CCallsign &rpt1, const CCallsign &rpt2, const std::string &message = "");

	// Get the 3-byte slow data for a given frame within the superframe
	// frameInSuper: 1-20 (frame 0 is sync, not slow data)
	// superframeCount: alternates between header (odd) and message (even)
	const uint8_t *GetSlowData(uint8_t frameInSuper, uint32_t superframeCount) const;

	bool IsReady(void) const { return m_bReady; }

private:
	void EncodeHeader(const CCallsign &my, const CCallsign &rpt1, const CCallsign &rpt2);
	void EncodeMessage(const std::string &message);
	void Scramble(uint8_t *data) const;

	uint8_t m_HeaderData[20][3];  // header slow data (frames 1-20)
	uint8_t m_MessageData[20][3]; // text message slow data (frames 1-20)
	bool m_bReady;
	bool m_bHasMessage;
};
