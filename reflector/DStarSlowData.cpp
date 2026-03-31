#include <cstring>
#include "DStarSlowData.h"
#include "CRC.h"

// D-Star slow data scramble pattern
static const uint8_t SCRAMBLE[] = { 0x70, 0x4F, 0x93 };

// Filler bytes for unused slow data slots
static const uint8_t FILLER[] = { 0x66, 0x66, 0x66 };

CDStarSlowData::CDStarSlowData() : m_bReady(false), m_bHasMessage(false)
{
	memset(m_HeaderData, 0x66, sizeof(m_HeaderData));
	memset(m_MessageData, 0x66, sizeof(m_MessageData));
}

void CDStarSlowData::Init(const CCallsign &my, const CCallsign &rpt1, const CCallsign &rpt2, const std::string &message)
{
	EncodeHeader(my, rpt1, rpt2);
	if (!message.empty())
	{
		EncodeMessage(message);
		m_bHasMessage = true;
	}
	m_bReady = true;
}

void CDStarSlowData::SetMessage(const std::string &message)
{
	if (!message.empty())
	{
		EncodeMessage(message);
		m_bHasMessage = true;
	}
}

const uint8_t *CDStarSlowData::GetSlowData(uint8_t frameInSuper, uint32_t superframeCount) const
{
	if (frameInSuper < 1 || frameInSuper > 20)
		return FILLER;

	// Alternate: odd superframes = header, even = message (if available)
	if (m_bHasMessage && (superframeCount % 2 == 0))
		return m_MessageData[frameInSuper - 1];
	else
		return m_HeaderData[frameInSuper - 1];
}

void CDStarSlowData::Scramble(uint8_t *data) const
{
	data[0] ^= SCRAMBLE[0];
	data[1] ^= SCRAMBLE[1];
	data[2] ^= SCRAMBLE[2];
}

void CDStarSlowData::EncodeHeader(const CCallsign &my, const CCallsign &rpt1, const CCallsign &rpt2)
{
	// Build 41-byte D-Star header: flags(3) + RPT2(8) + RPT1(8) + UR(8) + MY(8) + SUFFIX(4) + CRC(2)
	uint8_t header[41];
	memset(header, ' ', sizeof(header));
	header[0] = header[1] = header[2] = 0x00; // flags

	// RPT2 (bytes 3-10)
	uint8_t cs[8];
	rpt2.GetCallsign(cs);
	memcpy(&header[3], cs, 8);

	// RPT1 (bytes 11-18)
	rpt1.GetCallsign(cs);
	memcpy(&header[11], cs, 8);

	// UR = CQCQCQ (bytes 19-26)
	memcpy(&header[19], "CQCQCQ  ", 8);

	// MY (bytes 27-34)
	my.GetCallsign(cs);
	memcpy(&header[27], cs, 8);

	// MY suffix (bytes 35-38)
	memset(&header[35], ' ', 4);

	// CRC (bytes 39-40)
	CCRC::addCCITT161(header, 41);

	// Encode into 9 groups of 6 bytes (1 type + 5 data) → 18 slow data frames
	// Each group fills 2 consecutive 3-byte slow data frames
	uint8_t raw[20][3];
	memset(raw, 0x66, sizeof(raw));

	for (int group = 0; group < 9; group++)
	{
		uint8_t typeByte = 0x50 + group;
		uint8_t data[5];
		memset(data, 0x66, 5);

		int offset = group * 5;
		int remaining = 41 - offset;
		if (remaining > 5) remaining = 5;
		if (remaining > 0) memcpy(data, &header[offset], remaining);

		// First frame of the group: type byte + first 2 data bytes
		raw[group * 2][0] = typeByte;
		raw[group * 2][1] = data[0];
		raw[group * 2][2] = data[1];

		// Second frame of the group: remaining 3 data bytes
		raw[group * 2 + 1][0] = data[2];
		raw[group * 2 + 1][1] = data[3];
		raw[group * 2 + 1][2] = data[4];
	}

	// Frames 18-19 (indices 18, 19) are filler — already set to 0x66

	// Scramble all 20 frames
	for (int i = 0; i < 20; i++)
	{
		Scramble(raw[i]);
		memcpy(m_HeaderData[i], raw[i], 3);
	}
}

void CDStarSlowData::EncodeMessage(const std::string &message)
{
	// D-Star text message: type 0x40, 20 characters, split into 4 groups of 5 bytes
	// Same structure as header: type byte + 5 data bytes per group → 2 frames per group

	// Pad message to 20 chars
	char msg[20];
	memset(msg, ' ', 20);
	size_t len = message.size();
	if (len > 20) len = 20;
	memcpy(msg, message.c_str(), len);

	uint8_t raw[20][3];
	memset(raw, 0x66, sizeof(raw));

	for (int group = 0; group < 4; group++)
	{
		uint8_t typeByte = 0x40 + group;
		uint8_t data[5];
		memcpy(data, &msg[group * 5], 5);

		raw[group * 2][0] = typeByte;
		raw[group * 2][1] = data[0];
		raw[group * 2][2] = data[1];

		raw[group * 2 + 1][0] = data[2];
		raw[group * 2 + 1][1] = data[3];
		raw[group * 2 + 1][2] = data[4];
	}

	// Frames 8-19 are filler — already set to 0x66

	// Scramble all 20 frames
	for (int i = 0; i < 20; i++)
	{
		Scramble(raw[i]);
		memcpy(m_MessageData[i], raw[i], 3);
	}
}
