// SHA-256 implementation adapted from DMRGateway by G4KLX
// Original: Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
// Licensed under the GNU General Public License v2 or later

#include <cstring>
#include "SHA256.h"

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

CSHA256::CSHA256() {}

void CSHA256::init()
{
	m_state[0] = 0x6a09e667;
	m_state[1] = 0xbb67ae85;
	m_state[2] = 0x3c6ef372;
	m_state[3] = 0xa54ff53a;
	m_state[4] = 0x510e527f;
	m_state[5] = 0x9b05688c;
	m_state[6] = 0x1f83d9ab;
	m_state[7] = 0x5be0cd19;
	m_count = 0;
	::memset(m_buf, 0, sizeof(m_buf));
}

void CSHA256::transform(const uint8_t *data)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
		w[i] = ((uint32_t)data[i*4] << 24) | ((uint32_t)data[i*4+1] << 16) |
		        ((uint32_t)data[i*4+2] << 8) | (uint32_t)data[i*4+3];

	for (int i = 16; i < 64; i++)
		w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

	uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
	uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

	for (int i = 0; i < 64; i++)
	{
		uint32_t t1 = h + EP1(e) + CH(e, f, g) + K[i] + w[i];
		uint32_t t2 = EP0(a) + MAJ(a, b, c);
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
	m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
}

void CSHA256::process(const uint8_t *data, size_t length)
{
	size_t offset = (size_t)(m_count & 0x3F);
	m_count += length;

	for (size_t i = 0; i < length; i++)
	{
		m_buf[offset++] = data[i];
		if (offset == 64)
		{
			transform(m_buf);
			offset = 0;
		}
	}
}

void CSHA256::finish(uint8_t *digest)
{
	uint64_t bits = m_count * 8;
	size_t offset = (size_t)(m_count & 0x3F);

	m_buf[offset++] = 0x80;
	if (offset > 56)
	{
		::memset(m_buf + offset, 0, 64 - offset);
		transform(m_buf);
		offset = 0;
	}
	::memset(m_buf + offset, 0, 56 - offset);

	for (int i = 0; i < 8; i++)
		m_buf[63 - i] = (uint8_t)(bits >> (i * 8));

	transform(m_buf);

	for (int i = 0; i < 8; i++)
	{
		digest[i*4]   = (uint8_t)(m_state[i] >> 24);
		digest[i*4+1] = (uint8_t)(m_state[i] >> 16);
		digest[i*4+2] = (uint8_t)(m_state[i] >> 8);
		digest[i*4+3] = (uint8_t)(m_state[i]);
	}
}

void CSHA256::buffer(const uint8_t *input, size_t length, uint8_t *output)
{
	init();
	process(input, length);
	finish(output);
}
