#pragma once

// SHA-256 implementation adapted from DMRGateway by G4KLX
// Original: Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
// Licensed under the GNU General Public License v2 or later

#include <cstdint>
#include <cstddef>

class CSHA256
{
public:
	CSHA256();

	// One-shot: hash input buffer, write 32-byte digest to output
	void buffer(const uint8_t *input, size_t length, uint8_t *output);

private:
	void init();
	void process(const uint8_t *data, size_t length);
	void finish(uint8_t *digest);
	void transform(const uint8_t *data);

	uint32_t m_state[8];
	uint64_t m_count;
	uint8_t  m_buf[64];
};
