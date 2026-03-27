# SvxReflector Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a SvxReflector client protocol to urfd so it can connect to existing SvxReflector servers (e.g., FM-Funknetz) and bridge audio between FM nodes and digital modes.

**Architecture:** urfd connects as a V2 client to a SvxReflector via TCP+UDP. OPUS-encoded audio is decoded to PCM internally and routed through the existing transcoder pipeline. The protocol is implemented as a standard urfd protocol plugin (inherits CProtocol), fully isolated from all other protocols.

**Tech Stack:** C++17, libopus (OPUS codec), HMAC-SHA1 (OpenSSL, already linked), TCP+UDP sockets.

**Spec:** `docs/specs/2026-03-27-svxreflector-client-design.md`

---

### Task 1: Add config keys and parsing for [SvxReflector]

**Files:**
- Modify: `reflector/JsonKeys.h:78` (before closing `};`)
- Modify: `reflector/Configure.h:28` (ESection enum)
- Modify: `reflector/Configure.cpp:94` (section define), `:225` (section mapping), `:551` (parsing), `:835` (validation)
- Modify: `reflector/Defines.h:53-54` (IPv4/IPv6), `:69` (EProtocol), `:124` (keepalive)

- [ ] **Step 1: Add SvxReflector struct to JsonKeys.h**

Add before the closing `};` on line 79:

```cpp
struct SVX { const std::string enable, host, port, callsign, password; }
svx { "svxEnable", "svxHost", "svxPort", "svxCallsign", "svxPassword" };
```

- [ ] **Step 2: Add EProtocol and defines to Defines.h**

Add to EProtocol enum on line 69 (append `svxreflector` after `bmhomebrew`):

```cpp
enum class EProtocol { any, none, dextra, dplus, dcs, g3, bm, urf, dmrplus, dmrmmdvm, nxdn, p25, usrp, ysf, m17, bmhomebrew, svxreflector };
```

Add IPv4/IPv6 defines after USRP (after line 53/64):

```cpp
#define SVX_IPV4 true
```

```cpp
#define SVX_IPV6 false
```

Add keepalive constants after USRP block (after line 124):

```cpp
// SvxReflector
#define SVX_TCP_KEEPALIVE_PERIOD       10                                  // in seconds
#define SVX_TCP_KEEPALIVE_TIMEOUT      15                                  // in seconds
#define SVX_UDP_KEEPALIVE_PERIOD       15                                  // in seconds
#define SVX_UDP_KEEPALIVE_TIMEOUT      60                                  // in seconds
#define SVX_RECONNECT_PERIOD           5                                   // in seconds
```

- [ ] **Step 3: Add ESection and section mapping to Configure**

In `Configure.h` line 28, add `svx` to the ESection enum:

```cpp
enum class ESection { none, names, ip, modules, urf, dplus, dextra, dcs, g3, dmrplus, mmdvm, nxdn, bm, ysf, p25, m17, usrp, dmrid, nxdnid, ysffreq, files, tc, bmhb, echo, svx };
```

In `Configure.cpp`, add section define after line 94:

```cpp
#define JSVXREFLECTOR            "SvxReflector"
```

Add section mapping after the `echo` block (after line 226):

```cpp
else if (0 == hname.compare(JSVXREFLECTOR))
    section = ESection::svx;
```

- [ ] **Step 4: Add config parsing section to Configure.cpp**

Add a new `case ESection::svx:` block after the `case ESection::echo:` block (after line 561):

```cpp
case ESection::svx:
    if (0 == key.compare(JENABLE))
        data[g_Keys.svx.enable] = IS_TRUE(value[0]);
    else if (0 == key.compare("Host"))
        data[g_Keys.svx.host] = value;
    else if (0 == key.compare(JPORT))
        data[g_Keys.svx.port] = getUnsigned(value, "SvxReflector Port", 1024, 65535, 5300);
    else if (0 == key.compare(JCALLSIGN))
        data[g_Keys.svx.callsign] = value;
    else if (0 == key.compare("Password"))
        data[g_Keys.svx.password] = value;
    else if (0 == key.compare(0, 2, "TG"))
    {
        std::string tgkey = "svxTG" + key.substr(2);
        data[tgkey] = value;
    }
    else
        badParam(key);
    break;
```

- [ ] **Step 5: Add config validation to Configure.cpp**

Add after the USRP validation block (after line ~835), before the return statement:

```cpp
// SvxReflector
if (isDefined(ErrorLevel::fatal, JSVXREFLECTOR, JENABLE, g_Keys.svx.enable, rval))
{
    if (GetBoolean(g_Keys.svx.enable))
    {
        if (tcport)
        {
            isDefined(ErrorLevel::fatal, JSVXREFLECTOR, "Host", g_Keys.svx.host, rval);
            isDefined(ErrorLevel::fatal, JSVXREFLECTOR, JPORT, g_Keys.svx.port, rval);
            isDefined(ErrorLevel::fatal, JSVXREFLECTOR, JCALLSIGN, g_Keys.svx.callsign, rval);
            isDefined(ErrorLevel::fatal, JSVXREFLECTOR, "Password", g_Keys.svx.password, rval);
            // Validate TG mappings: each module must be transcoded
            const auto &jdata = GetData();
            for (auto it = jdata.begin(); it != jdata.end(); ++it)
            {
                const std::string &k = it.key();
                if (k.substr(0, 5) == "svxTG")
                {
                    std::string val = it.value().get<std::string>();
                    if (val.size() >= 1 && std::string::npos == GetString(g_Keys.tc.modules).find(val[0]))
                    {
                        std::cerr << "ERROR: [" << JSVXREFLECTOR << "] TG" << k.substr(5) << " module " << val[0] << " is not a transcoded module" << std::endl;
                        rval = true;
                    }
                }
            }
        }
        else
        {
            std::cerr << "ERROR: " << JSVXREFLECTOR << " requires a transcoder" << std::endl;
            rval = true;
        }
    }
}
```

- [ ] **Step 6: Test config parsing**

Add a test `[SvxReflector]` section to the local urfd.ini:

```ini
[SvxReflector]
Enable = false
Host = svxreflector.fm-funknetz.de
Port = 5300
Callsign = URF363
Password = test123
TG26250 = S
TG26363 = F
```

Run: `make inicheck && ./inicheck /path/to/urfd.ini`
Expected: No errors about SvxReflector section.

- [ ] **Step 7: Commit**

```
git add reflector/Defines.h reflector/JsonKeys.h reflector/Configure.h reflector/Configure.cpp
git commit -m "Add SvxReflector config keys, parsing and validation"
```

---

### Task 2: Create SvxReflectorClient class

**Files:**
- Create: `reflector/SvxReflectorClient.h`
- Create: `reflector/SvxReflectorClient.cpp`

- [ ] **Step 1: Create SvxReflectorClient.h**

```cpp
#pragma once

#include "Client.h"

class CSvxReflectorClient : public CClient
{
public:
	CSvxReflectorClient();
	CSvxReflectorClient(const CCallsign &, const CIp &, char = ' ');
	CSvxReflectorClient(const CSvxReflectorClient &);
	virtual ~CSvxReflectorClient() {};

	// identity
	EProtocol GetProtocol(void) const           { return EProtocol::svxreflector; }
	const char *GetProtocolName(void) const     { return "SvxReflector"; }
	bool IsNode(void) const                     { return true; }

	// status
	bool IsAlive(void) const;
};
```

- [ ] **Step 2: Create SvxReflectorClient.cpp**

```cpp
#include "SvxReflectorClient.h"

CSvxReflectorClient::CSvxReflectorClient()
{
}

CSvxReflectorClient::CSvxReflectorClient(const CCallsign &cs, const CIp &ip, char mod)
	: CClient(cs, ip, mod)
{
}

CSvxReflectorClient::CSvxReflectorClient(const CSvxReflectorClient &rhs)
	: CClient(rhs)
{
}

bool CSvxReflectorClient::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < SVX_TCP_KEEPALIVE_TIMEOUT);
}
```

- [ ] **Step 3: Verify it compiles**

Run: `make` (in reflector dir, or full docker build)
Expected: Compiles without errors. The new files are picked up automatically via `$(wildcard *.cpp)`.

- [ ] **Step 4: Commit**

```
git add reflector/SvxReflectorClient.h reflector/SvxReflectorClient.cpp
git commit -m "Add SvxReflectorClient class"
```

---

### Task 3: Create SvxReflectorProtocol — TCP connection and auth

**Files:**
- Create: `reflector/SvxReflectorProtocol.h`
- Create: `reflector/SvxReflectorProtocol.cpp`

This task implements the TCP layer: connect, V2 handshake, auth, heartbeat, reconnect. No audio yet.

- [ ] **Step 1: Create SvxReflectorProtocol.h**

```cpp
#pragma once

#include <opus/opus.h>
#include "Protocol.h"
#include "SvxReflectorClient.h"
#include "Timer.h"

// SvxReflector protocol V2 message types
#define SVX_TCP_MSG_HEARTBEAT           1
#define SVX_TCP_MSG_PROTO_VER           5
#define SVX_TCP_MSG_AUTH_CHALLENGE       10
#define SVX_TCP_MSG_AUTH_RESPONSE        11
#define SVX_TCP_MSG_AUTH_OK              12
#define SVX_TCP_MSG_ERROR                13
#define SVX_TCP_MSG_SERVER_INFO          100
#define SVX_TCP_MSG_NODE_LIST            101
#define SVX_TCP_MSG_NODE_JOINED          102
#define SVX_TCP_MSG_NODE_LEFT            103
#define SVX_TCP_MSG_TALKER_START         104
#define SVX_TCP_MSG_TALKER_STOP          105
#define SVX_TCP_MSG_SELECT_TG            106
#define SVX_TCP_MSG_NODE_INFO            111

#define SVX_UDP_MSG_HEARTBEAT            1
#define SVX_UDP_MSG_AUDIO                101
#define SVX_UDP_MSG_FLUSH_SAMPLES        102
#define SVX_UDP_MSG_ALL_FLUSHED          103

class CSvxReflectorProtocol : public CProtocol
{
public:
	CSvxReflectorProtocol();
	virtual ~CSvxReflectorProtocol();

	// initialization
	bool Initialize(const char *type, const EProtocol ptype, const uint16_t port, const bool has_ipv4, const bool has_ipv6);

	// task
	void Task(void);

protected:
	// queue helper
	void HandleQueue(void);
	void HandleKeepalives(void);

	// TCP connection
	bool TcpConnect(void);
	void TcpDisconnect(void);
	bool TcpSendFrame(const uint8_t *data, uint32_t len);
	bool TcpReceiveFrame(std::vector<uint8_t> &frame);

	// TCP message handlers
	void OnTcpMessage(uint16_t type, const std::vector<uint8_t> &payload);
	void OnAuthChallenge(const std::vector<uint8_t> &payload);
	void OnAuthOk(void);
	void OnServerInfo(const std::vector<uint8_t> &payload);
	void OnTalkerStart(const std::vector<uint8_t> &payload);
	void OnTalkerStop(const std::vector<uint8_t> &payload);

	// UDP audio
	void OnUdpAudio(const CBuffer &buffer);
	void OnUdpFlush(void);
	void EncodeAndSendAudio(const int16_t *pcm, uint32_t tg);

	// serialization helpers
	void PackUint16(std::vector<uint8_t> &buf, uint16_t val);
	void PackUint32(std::vector<uint8_t> &buf, uint32_t val);
	void PackString(std::vector<uint8_t> &buf, const std::string &str);
	uint16_t UnpackUint16(const std::vector<uint8_t> &buf, size_t &pos);
	uint32_t UnpackUint32(const std::vector<uint8_t> &buf, size_t &pos);
	std::string UnpackString(const std::vector<uint8_t> &buf, size_t &pos);

	// TG mapping
	char TGToModule(uint32_t tg) const;
	uint32_t ModuleToTG(char module) const;
	void LoadTGMap(void);

	// stream helpers
	void OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &, const CIp &);

	// state
	enum class EState { disconnected, connecting, authenticating, connected };
	EState m_State;

	// TCP
	int m_TcpFd;
	CIp m_ServerIp;

	// timers
	CTimer m_TcpHeartbeatTimer;
	CTimer m_TcpLastReceiveTimer;
	CTimer m_UdpHeartbeatTimer;
	CTimer m_UdpLastReceiveTimer;
	CTimer m_ReconnectTimer;
	int m_ReconnectBackoff;

	// auth
	uint16_t m_ClientId;
	std::string m_Password;
	std::string m_Callsign;
	std::string m_Host;

	// OPUS
	OpusEncoder *m_OpusEncoder;
	OpusDecoder *m_OpusDecoder;

	// TG mapping
	std::unordered_map<uint32_t, char> m_TGToModule;
	std::unordered_map<char, uint32_t> m_ModuleToTG;

	// incoming stream state
	struct SIncomingStream {
		uint32_t tg;
		char module;
		uint16_t streamId;
		bool open;
	};
	SIncomingStream m_InStream;

	// outgoing stream state per module
	std::unordered_map<char, uint32_t> m_OutStreamTG;
};
```

- [ ] **Step 2: Create SvxReflectorProtocol.cpp — serialization helpers**

```cpp
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <openssl/hmac.h>
#include <cstring>
#include <iostream>

#include "SvxReflectorProtocol.h"
#include "Global.h"

CSvxReflectorProtocol::CSvxReflectorProtocol()
	: m_State(EState::disconnected)
	, m_TcpFd(-1)
	, m_ReconnectBackoff(SVX_RECONNECT_PERIOD)
	, m_ClientId(0)
	, m_OpusEncoder(nullptr)
	, m_OpusDecoder(nullptr)
{
	m_InStream.open = false;
}

CSvxReflectorProtocol::~CSvxReflectorProtocol()
{
	TcpDisconnect();
	if (m_OpusEncoder) opus_encoder_destroy(m_OpusEncoder);
	if (m_OpusDecoder) opus_decoder_destroy(m_OpusDecoder);
}

void CSvxReflectorProtocol::PackUint16(std::vector<uint8_t> &buf, uint16_t val)
{
	buf.push_back((val >> 8) & 0xFF);
	buf.push_back(val & 0xFF);
}

void CSvxReflectorProtocol::PackUint32(std::vector<uint8_t> &buf, uint32_t val)
{
	buf.push_back((val >> 24) & 0xFF);
	buf.push_back((val >> 16) & 0xFF);
	buf.push_back((val >> 8) & 0xFF);
	buf.push_back(val & 0xFF);
}

void CSvxReflectorProtocol::PackString(std::vector<uint8_t> &buf, const std::string &str)
{
	PackUint16(buf, (uint16_t)str.size());
	buf.insert(buf.end(), str.begin(), str.end());
}

uint16_t CSvxReflectorProtocol::UnpackUint16(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint16_t val = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
	pos += 2;
	return val;
}

uint32_t CSvxReflectorProtocol::UnpackUint32(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint32_t val = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16)
	             | ((uint32_t)buf[pos+2] << 8) | buf[pos+3];
	pos += 4;
	return val;
}

std::string CSvxReflectorProtocol::UnpackString(const std::vector<uint8_t> &buf, size_t &pos)
{
	uint16_t len = UnpackUint16(buf, pos);
	std::string str((const char *)&buf[pos], len);
	pos += len;
	return str;
}

char CSvxReflectorProtocol::TGToModule(uint32_t tg) const
{
	auto it = m_TGToModule.find(tg);
	return (it != m_TGToModule.end()) ? it->second : ' ';
}

uint32_t CSvxReflectorProtocol::ModuleToTG(char module) const
{
	auto it = m_ModuleToTG.find(module);
	return (it != m_ModuleToTG.end()) ? it->second : 0;
}

void CSvxReflectorProtocol::LoadTGMap(void)
{
	m_TGToModule.clear();
	m_ModuleToTG.clear();
	const auto &jdata = g_Configure.GetData();
	for (auto it = jdata.begin(); it != jdata.end(); ++it)
	{
		const std::string &key = it.key();
		if (key.substr(0, 5) == "svxTG")
		{
			try {
				uint32_t tg = std::stoul(key.substr(5));
				char mod = it.value().get<std::string>()[0];
				m_TGToModule[tg] = mod;
				m_ModuleToTG[mod] = tg;
				std::cout << "SvxReflector TG mapping: TG" << tg << " <-> Module " << mod << std::endl;
			} catch (...) {}
		}
	}
}
```

- [ ] **Step 3: Add TCP connect/disconnect and framing**

Append to `SvxReflectorProtocol.cpp`:

```cpp
bool CSvxReflectorProtocol::TcpConnect(void)
{
	// DNS resolve
	struct addrinfo hints{}, *res;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	std::string portStr = std::to_string(g_Configure.GetUnsigned(g_Keys.svx.port));
	int err = getaddrinfo(m_Host.c_str(), portStr.c_str(), &hints, &res);
	if (err != 0)
	{
		std::cerr << "SvxReflector: DNS resolution failed for " << m_Host << ": " << gai_strerror(err) << std::endl;
		return false;
	}

	m_TcpFd = socket(res->ai_family, SOCK_STREAM, 0);
	if (m_TcpFd < 0)
	{
		freeaddrinfo(res);
		return false;
	}

	// non-blocking connect with timeout
	fcntl(m_TcpFd, F_SETFL, O_NONBLOCK);
	int ret = connect(m_TcpFd, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

	if (ret < 0 && errno != EINPROGRESS)
	{
		close(m_TcpFd);
		m_TcpFd = -1;
		return false;
	}

	// wait for connect (5s timeout)
	struct pollfd pfd = { m_TcpFd, POLLOUT, 0 };
	ret = poll(&pfd, 1, 5000);
	if (ret <= 0 || !(pfd.revents & POLLOUT))
	{
		close(m_TcpFd);
		m_TcpFd = -1;
		return false;
	}

	// check for connect error
	int sockerr = 0;
	socklen_t len = sizeof(sockerr);
	getsockopt(m_TcpFd, SOL_SOCKET, SO_ERROR, &sockerr, &len);
	if (sockerr != 0)
	{
		close(m_TcpFd);
		m_TcpFd = -1;
		return false;
	}

	// back to blocking with short timeouts for reads
	int flags = fcntl(m_TcpFd, F_GETFL);
	fcntl(m_TcpFd, F_SETFL, flags & ~O_NONBLOCK);
	struct timeval tv = { 0, 100000 }; // 100ms read timeout
	setsockopt(m_TcpFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	std::cout << "SvxReflector: TCP connected to " << m_Host << ":" << portStr << std::endl;
	return true;
}

void CSvxReflectorProtocol::TcpDisconnect(void)
{
	if (m_TcpFd >= 0)
	{
		close(m_TcpFd);
		m_TcpFd = -1;
	}
	m_State = EState::disconnected;
	m_InStream.open = false;
	m_ClientId = 0;
}

bool CSvxReflectorProtocol::TcpSendFrame(const uint8_t *data, uint32_t len)
{
	if (m_TcpFd < 0) return false;

	// 4-byte big-endian length header
	uint8_t hdr[4];
	hdr[0] = (len >> 24) & 0xFF;
	hdr[1] = (len >> 16) & 0xFF;
	hdr[2] = (len >> 8) & 0xFF;
	hdr[3] = len & 0xFF;

	if (send(m_TcpFd, hdr, 4, MSG_NOSIGNAL) != 4) return false;
	if (send(m_TcpFd, data, len, MSG_NOSIGNAL) != (ssize_t)len) return false;

	m_TcpHeartbeatTimer.start();
	return true;
}

bool CSvxReflectorProtocol::TcpReceiveFrame(std::vector<uint8_t> &frame)
{
	if (m_TcpFd < 0) return false;

	uint8_t hdr[4];
	ssize_t n = recv(m_TcpFd, hdr, 4, MSG_WAITALL);
	if (n != 4) return false;

	uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16)
	             | ((uint32_t)hdr[2] << 8) | hdr[3];

	if (len > 32768) return false; // safety limit

	frame.resize(len);
	n = recv(m_TcpFd, frame.data(), len, MSG_WAITALL);
	if (n != (ssize_t)len) return false;

	m_TcpLastReceiveTimer.start();
	return true;
}
```

- [ ] **Step 4: Add V2 authentication handshake**

Append to `SvxReflectorProtocol.cpp`:

```cpp
void CSvxReflectorProtocol::OnAuthChallenge(const std::vector<uint8_t> &payload)
{
	// payload: 2 bytes type (already consumed) + 20 bytes nonce
	if (payload.size() < 22) return;

	const uint8_t *nonce = &payload[2];

	// HMAC-SHA1(password, nonce)
	unsigned int digest_len = 20;
	uint8_t digest[20];
	HMAC(EVP_sha1(),
	     m_Password.c_str(), (int)m_Password.size(),
	     nonce, 20,
	     digest, &digest_len);

	// Build MsgAuthResponse: type(2) + digest(20) + callsign(string)
	std::vector<uint8_t> resp;
	PackUint16(resp, SVX_TCP_MSG_AUTH_RESPONSE);
	resp.insert(resp.end(), digest, digest + 20);
	PackString(resp, m_Callsign);

	TcpSendFrame(resp.data(), (uint32_t)resp.size());
	std::cout << "SvxReflector: auth response sent for " << m_Callsign << std::endl;
}

void CSvxReflectorProtocol::OnAuthOk(void)
{
	std::cout << "SvxReflector: authentication successful" << std::endl;
	m_State = EState::connected;
}

void CSvxReflectorProtocol::OnServerInfo(const std::vector<uint8_t> &payload)
{
	if (payload.size() < 4) return;
	size_t pos = 2; // skip type
	m_ClientId = UnpackUint16(payload, pos);
	std::cout << "SvxReflector: received ServerInfo, client_id=" << m_ClientId << std::endl;

	// Send MsgNodeInfo (JSON metadata)
	std::string json = "{\"sw\":\"urfd\",\"callsign\":\"" + m_Callsign + "\"}";
	std::vector<uint8_t> msg;
	PackUint16(msg, SVX_TCP_MSG_NODE_INFO);
	PackString(msg, json);
	TcpSendFrame(msg.data(), (uint32_t)msg.size());

	// Select all configured TGs
	for (const auto &tg : m_TGToModule)
	{
		std::vector<uint8_t> sel;
		PackUint16(sel, SVX_TCP_MSG_SELECT_TG);
		PackUint32(sel, tg.first);
		TcpSendFrame(sel.data(), (uint32_t)sel.size());
		std::cout << "SvxReflector: selected TG" << tg.first << std::endl;
	}

	// Start UDP heartbeats
	m_UdpHeartbeatTimer.start();
	m_UdpLastReceiveTimer.start();

	// Reset reconnect backoff
	m_ReconnectBackoff = SVX_RECONNECT_PERIOD;
}

void CSvxReflectorProtocol::OnTcpMessage(uint16_t type, const std::vector<uint8_t> &payload)
{
	switch (type)
	{
		case SVX_TCP_MSG_HEARTBEAT:
			break; // timer already reset by TcpReceiveFrame
		case SVX_TCP_MSG_AUTH_CHALLENGE:
			OnAuthChallenge(payload);
			break;
		case SVX_TCP_MSG_AUTH_OK:
			OnAuthOk();
			break;
		case SVX_TCP_MSG_SERVER_INFO:
			OnServerInfo(payload);
			break;
		case SVX_TCP_MSG_TALKER_START:
			OnTalkerStart(payload);
			break;
		case SVX_TCP_MSG_TALKER_STOP:
			OnTalkerStop(payload);
			break;
		case SVX_TCP_MSG_ERROR:
		{
			size_t pos = 2;
			std::string errmsg = (payload.size() > 2) ? UnpackString(payload, pos) : "unknown";
			std::cerr << "SvxReflector: error from server: " << errmsg << std::endl;
			TcpDisconnect();
			m_ReconnectBackoff = 300; // 5min on server error
			break;
		}
		case SVX_TCP_MSG_NODE_LIST:
		case SVX_TCP_MSG_NODE_JOINED:
		case SVX_TCP_MSG_NODE_LEFT:
			break; // informational, ignore
		default:
			break;
	}
}
```

- [ ] **Step 5: Add Initialize and Task (main loop)**

Append to `SvxReflectorProtocol.cpp`:

```cpp
bool CSvxReflectorProtocol::Initialize(const char *type, const EProtocol ptype,
	const uint16_t port, const bool has_ipv4, const bool has_ipv6)
{
	m_Host = g_Configure.GetString(g_Keys.svx.host);
	m_Password = g_Configure.GetString(g_Keys.svx.password);
	m_Callsign = g_Configure.GetString(g_Keys.svx.callsign);

	LoadTGMap();

	// Initialize OPUS encoder/decoder at 8kHz mono
	int err;
	m_OpusEncoder = opus_encoder_create(8000, 1, OPUS_APPLICATION_VOIP, &err);
	if (err != OPUS_OK)
	{
		std::cerr << "SvxReflector: opus_encoder_create failed: " << opus_strerror(err) << std::endl;
		return false;
	}
	m_OpusDecoder = opus_decoder_create(8000, 1, &err);
	if (err != OPUS_OK)
	{
		std::cerr << "SvxReflector: opus_decoder_create failed: " << opus_strerror(err) << std::endl;
		return false;
	}

	// Initialize base protocol (opens UDP socket)
	if (!CProtocol::Initialize(type, ptype, port, has_ipv4, has_ipv6))
		return false;

	// Start timers
	m_ReconnectTimer.start();

	std::cout << "SvxReflector: initialized, connecting to " << m_Host << std::endl;
	return true;
}

void CSvxReflectorProtocol::Task(void)
{
	// Handle TCP connection state machine
	switch (m_State)
	{
		case EState::disconnected:
			if (m_ReconnectTimer.time() >= m_ReconnectBackoff)
			{
				m_ReconnectTimer.start();
				if (TcpConnect())
				{
					// Send ProtoVer(2,0)
					std::vector<uint8_t> ver;
					PackUint16(ver, SVX_TCP_MSG_PROTO_VER);
					PackUint16(ver, 2); // major
					PackUint16(ver, 0); // minor
					TcpSendFrame(ver.data(), (uint32_t)ver.size());
					m_State = EState::authenticating;
					m_TcpHeartbeatTimer.start();
					m_TcpLastReceiveTimer.start();
				}
				else
				{
					// Increase backoff: 5, 10, 30, 60, 60, ...
					if (m_ReconnectBackoff < 60)
						m_ReconnectBackoff = std::min(m_ReconnectBackoff * 2, 60);
				}
			}
			break;

		case EState::authenticating:
		case EState::connected:
		{
			// Receive TCP frames
			std::vector<uint8_t> frame;
			while (TcpReceiveFrame(frame))
			{
				if (frame.size() >= 2)
				{
					uint16_t type = ((uint16_t)frame[0] << 8) | frame[1];
					OnTcpMessage(type, frame);
				}
			}

			// Check TCP timeout
			if (m_TcpLastReceiveTimer.time() > SVX_TCP_KEEPALIVE_TIMEOUT)
			{
				std::cerr << "SvxReflector: TCP heartbeat timeout" << std::endl;
				TcpDisconnect();
				m_ReconnectTimer.start();
				break;
			}

			// Send TCP heartbeat
			if (m_TcpHeartbeatTimer.time() >= SVX_TCP_KEEPALIVE_PERIOD)
			{
				std::vector<uint8_t> hb;
				PackUint16(hb, SVX_TCP_MSG_HEARTBEAT);
				TcpSendFrame(hb.data(), (uint32_t)hb.size());
			}

			if (m_State == EState::connected)
			{
				// Receive UDP audio
				CBuffer buffer;
				CIp ip;
				if (Receive4(buffer, ip))
				{
					m_UdpLastReceiveTimer.start();
					if (buffer.size() >= 4)
					{
						uint16_t type = ((uint16_t)buffer.data()[0] << 8) | buffer.data()[1];
						if (type == SVX_UDP_MSG_AUDIO)
							OnUdpAudio(buffer);
						else if (type == SVX_UDP_MSG_FLUSH_SAMPLES)
							OnUdpFlush();
					}
				}

				// Send UDP heartbeat
				if (m_UdpHeartbeatTimer.time() >= SVX_UDP_KEEPALIVE_PERIOD)
				{
					CBuffer hb;
					uint8_t hbdata[4];
					hbdata[0] = 0; hbdata[1] = SVX_UDP_MSG_HEARTBEAT;
					hbdata[2] = (m_ClientId >> 8) & 0xFF;
					hbdata[3] = m_ClientId & 0xFF;
					hb.Set(hbdata, 4);
					Send(hb, m_ServerIp);
					m_UdpHeartbeatTimer.start();
				}

				// Check UDP timeout
				if (m_UdpLastReceiveTimer.time() > SVX_UDP_KEEPALIVE_TIMEOUT)
				{
					std::cerr << "SvxReflector: UDP heartbeat timeout" << std::endl;
					TcpDisconnect();
					m_ReconnectTimer.start();
				}

				// Handle outbound queue
				HandleQueue();
			}
			break;
		}

		default:
			break;
	}
}

void CSvxReflectorProtocol::HandleKeepalives(void)
{
	// handled in Task() directly
}
```

- [ ] **Step 6: Verify it compiles (with stubs for OnTalkerStart, OnTalkerStop, OnUdpAudio, OnUdpFlush, HandleQueue, OnDvHeaderPacketIn)**

Add stubs at the end of `SvxReflectorProtocol.cpp`:

```cpp
void CSvxReflectorProtocol::OnTalkerStart(const std::vector<uint8_t> &payload)
{
	// TODO: implement in Task 4
}

void CSvxReflectorProtocol::OnTalkerStop(const std::vector<uint8_t> &payload)
{
	// TODO: implement in Task 4
}

void CSvxReflectorProtocol::OnUdpAudio(const CBuffer &buffer)
{
	// TODO: implement in Task 4
}

void CSvxReflectorProtocol::OnUdpFlush(void)
{
	// TODO: implement in Task 4
}

void CSvxReflectorProtocol::HandleQueue(void)
{
	// TODO: implement in Task 5
}

void CSvxReflectorProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
{
	// TODO: implement in Task 4
}
```

Run: `make` — should compile with `-lopus`.

- [ ] **Step 7: Commit**

```
git add reflector/SvxReflectorProtocol.h reflector/SvxReflectorProtocol.cpp
git commit -m "Add SvxReflectorProtocol with TCP connection, auth and heartbeat"
```

---

### Task 4: Implement incoming audio path (SvxReflector -> urfd)

**Files:**
- Modify: `reflector/SvxReflectorProtocol.cpp` (replace stubs for OnTalkerStart, OnTalkerStop, OnUdpAudio, OnUdpFlush, OnDvHeaderPacketIn)

- [ ] **Step 1: Implement OnTalkerStart and OnTalkerStop**

Replace the stubs:

```cpp
void CSvxReflectorProtocol::OnTalkerStart(const std::vector<uint8_t> &payload)
{
	if (payload.size() < 6) return;
	size_t pos = 2;
	uint32_t tg = UnpackUint32(payload, pos);
	char module = TGToModule(tg);
	if (module == ' ') return;

	// Generate stream ID
	static uint16_t s_nextId = 0x5C00;
	uint16_t streamId = s_nextId++;
	if (s_nextId >= 0x5CFF) s_nextId = 0x5C00;

	m_InStream.tg = tg;
	m_InStream.module = module;
	m_InStream.streamId = streamId;
	m_InStream.open = false; // will be opened on first audio

	std::cout << "SvxReflector: talker start on TG" << tg << " -> Module " << module << std::endl;
}

void CSvxReflectorProtocol::OnTalkerStop(const std::vector<uint8_t> &payload)
{
	if (!m_InStream.open) return;
	// Send last packet to close stream
	OnUdpFlush();
	std::cout << "SvxReflector: talker stop on TG" << m_InStream.tg << std::endl;
}
```

- [ ] **Step 2: Implement OnUdpAudio**

Replace the stub:

```cpp
void CSvxReflectorProtocol::OnUdpAudio(const CBuffer &buffer)
{
	if (m_InStream.tg == 0) return;

	// UDP audio format: type(2) + tg(4) + audio_data(variable)
	if (buffer.size() < 7) return;

	// Decode OPUS to PCM
	int16_t pcm[160];
	int samples = opus_decode(m_OpusDecoder,
	                          buffer.data() + 6,
	                          (opus_int32)(buffer.size() - 6),
	                          pcm, 160, 0);
	if (samples <= 0) return;

	// Open stream on first audio frame
	if (!m_InStream.open)
	{
		CCallsign my(m_Callsign);
		CCallsign rpt1;
		rpt1.SetCallsign(m_Callsign, false);
		rpt1.SetCSModule(m_InStream.module);
		CCallsign rpt2(g_Reflector.GetCallsign());
		rpt2.SetCSModule(m_InStream.module);

		auto header = std::unique_ptr<CDvHeaderPacket>(new CDvHeaderPacket(
			my, CCallsign("CQCQCQ"), rpt1, rpt2, m_InStream.streamId, 0, 0));

		OnDvHeaderPacketIn(header, m_ServerIp);
		m_InStream.open = true;
	}

	// Create frame packet with PCM data
	auto frame = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(
		pcm, m_InStream.streamId, 0, 0, false));
	frame->SetPacketModule(m_InStream.module);

	auto stream = GetStream(m_InStream.streamId, &m_ServerIp);
	if (stream)
	{
		stream->Push(std::move(frame));
	}
}
```

- [ ] **Step 3: Implement OnUdpFlush**

Replace the stub:

```cpp
void CSvxReflectorProtocol::OnUdpFlush(void)
{
	if (!m_InStream.open) return;

	// Send silence as last packet
	int16_t silence[160] = {};
	auto frame = std::unique_ptr<CDvFramePacket>(new CDvFramePacket(
		silence, m_InStream.streamId, 0, 0, true));
	frame->SetPacketModule(m_InStream.module);

	auto stream = GetStream(m_InStream.streamId, &m_ServerIp);
	if (stream)
	{
		stream->Push(std::move(frame));
	}

	m_InStream.open = false;
	m_InStream.tg = 0;
}
```

- [ ] **Step 4: Implement OnDvHeaderPacketIn**

Replace the stub:

```cpp
void CSvxReflectorProtocol::OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &Header, const CIp &Ip)
{
	// Find or create client for this module
	CClients *clients = g_Reflector.GetClients();
	char module = Header->GetRpt2Module();
	CCallsign cs;
	cs.SetCallsign(m_Callsign, false);
	cs.SetCSModule(module);
	std::shared_ptr<CClient> client = clients->FindClient(Ip, EProtocol::svxreflector);
	if (client == nullptr)
	{
		clients->AddClient(std::make_shared<CSvxReflectorClient>(cs, Ip, module));
		client = clients->FindClient(Ip, EProtocol::svxreflector);
	}

	if (client)
	{
		client->Alive();
		client->SetReflectorModule(module);
		auto stream = g_Reflector.OpenStream(Header, client);
		if (stream)
		{
			m_Streams[stream->GetStreamId()] = stream;
		}
	}

	g_Reflector.ReleaseClients();
}
```

- [ ] **Step 5: Verify it compiles**

Run: `make`
Expected: Compiles without errors.

- [ ] **Step 6: Commit**

```
git add reflector/SvxReflectorProtocol.cpp
git commit -m "Implement incoming audio path: OPUS decode, stream management"
```

---

### Task 5: Implement outgoing audio path (urfd -> SvxReflector)

**Files:**
- Modify: `reflector/SvxReflectorProtocol.cpp` (replace HandleQueue stub)

- [ ] **Step 1: Implement HandleQueue and EncodeAndSendAudio**

Replace the HandleQueue stub:

```cpp
void CSvxReflectorProtocol::HandleQueue(void)
{
	while (!m_Queue.IsEmpty())
	{
		auto packet = m_Queue.Pop();

		if (packet->IsDvHeader())
		{
			auto &header = (CDvHeaderPacket &)*packet;
			char module = header.GetPacketModule();
			uint32_t tg = ModuleToTG(module);
			if (tg == 0) continue;
			m_OutStreamTG[module] = tg;
		}
		else if (packet->IsDvFrame())
		{
			auto &frame = (CDvFramePacket &)*packet;
			char module = frame.GetPacketModule();
			auto it = m_OutStreamTG.find(module);
			if (it == m_OutStreamTG.end()) continue;
			uint32_t tg = it->second;

			if (frame.IsLastPacket())
			{
				// Send flush
				CBuffer flush;
				uint8_t flushdata[4];
				flushdata[0] = 0; flushdata[1] = SVX_UDP_MSG_FLUSH_SAMPLES;
				flushdata[2] = (m_ClientId >> 8) & 0xFF;
				flushdata[3] = m_ClientId & 0xFF;
				flush.Set(flushdata, 4);
				Send(flush, m_ServerIp);
				m_OutStreamTG.erase(it);
			}
			else
			{
				const int16_t *pcm = (const int16_t *)frame.GetCodecData(ECodecType::usrp);
				if (pcm)
					EncodeAndSendAudio(pcm, tg);
			}
		}
	}
}

void CSvxReflectorProtocol::EncodeAndSendAudio(const int16_t *pcm, uint32_t tg)
{
	uint8_t opus_buf[256];
	int encoded = opus_encode(m_OpusEncoder, pcm, 160, opus_buf, sizeof(opus_buf));
	if (encoded <= 0) return;

	// Build UDP audio packet: type(2) + client_id(2) + tg(4) + audio_data
	CBuffer buf;
	uint8_t hdr[8];
	hdr[0] = 0; hdr[1] = SVX_UDP_MSG_AUDIO;
	hdr[2] = (m_ClientId >> 8) & 0xFF;
	hdr[3] = m_ClientId & 0xFF;
	hdr[4] = (tg >> 24) & 0xFF;
	hdr[5] = (tg >> 16) & 0xFF;
	hdr[6] = (tg >> 8) & 0xFF;
	hdr[7] = tg & 0xFF;
	buf.Set(hdr, 8);
	buf.Append(opus_buf, encoded);
	Send(buf, m_ServerIp);
}
```

- [ ] **Step 2: Verify it compiles**

Run: `make`
Expected: Compiles without errors.

- [ ] **Step 3: Commit**

```
git add reflector/SvxReflectorProtocol.cpp
git commit -m "Implement outgoing audio path: PCM to OPUS encode and send"
```

---

### Task 6: Register protocol and add build dependencies

**Files:**
- Modify: `reflector/Protocols.cpp:34` (include), `:130` (init block)
- Modify: `reflector/Makefile:35` (LDFLAGS)
- Modify: `docker/Dockerfile:11` (build deps), `:33` (runtime deps)
- Modify: `reflector/Reflector.cpp:640` (XML mapping), `:670` (protocol list)

- [ ] **Step 1: Add include and init block to Protocols.cpp**

Add include after line 32 (`#include "USRPProtocol.h"`):

```cpp
#include "SvxReflectorProtocol.h"
```

Add init block after the USRP init block (after line ~130):

```cpp
if (g_Configure.Contains(g_Keys.svx.enable) && g_Configure.GetBoolean(g_Keys.svx.enable))
{
    m_Protocols.emplace_back(std::unique_ptr<CSvxReflectorProtocol>(new CSvxReflectorProtocol));
    if (! m_Protocols.back()->Initialize("SvxReflector", EProtocol::svxreflector, uint16_t(g_Configure.GetUnsigned(g_Keys.svx.port)), SVX_IPV4, SVX_IPV6))
        return false;
}
```

- [ ] **Step 2: Add -lopus to Makefile**

Change line 35 from:

```makefile
LDFLAGS=-pthread -lcurl
```

to:

```makefile
LDFLAGS=-pthread -lcurl -lopus
```

- [ ] **Step 3: Add libopus to Dockerfile**

In build dependencies (after `nlohmann-json3-dev`), add:

```dockerfile
    libopus-dev \
```

In runtime dependencies (after `libopendht3t64`), add:

```dockerfile
    libopus0 \
```

- [ ] **Step 4: Add XML mapping output to Reflector.cpp**

After the BMMmdvm TG mapping block (after line ~640), add:

```cpp
// SvxReflector TG mappings for this module
if (g_Configure.Contains(g_Keys.svx.enable) && g_Configure.GetBoolean(g_Keys.svx.enable))
{
    const auto &jdata = g_Configure.GetData();
    for (auto it = jdata.begin(); it != jdata.end(); ++it)
    {
        const std::string &key = it.key();
        if (key.substr(0, 5) == "svxTG")
        {
            try {
                std::string val = it.value().get<std::string>();
                if (val.size() >= 1 && val[0] == m)
                {
                    uint32_t tg = std::stoul(key.substr(5));
                    xmlFile << "\t<Mapping><Protocol>SvxReflector</Protocol><Type>TG</Type>";
                    xmlFile << "<ID>" << tg << "</ID>";
                    if (g_Configure.Contains(g_Keys.svx.host))
                        xmlFile << "<RemoteName>" << g_Configure.GetString(g_Keys.svx.host) << "</RemoteName>";
                    xmlFile << "</Mapping>" << std::endl;
                }
            } catch (...) {}
        }
    }
}
```

After the USRP protocol list entry (after line ~670), add:

```cpp
if (g_Configure.Contains(g_Keys.svx.enable) && g_Configure.GetBoolean(g_Keys.svx.enable))
    xmlFile << "<Protocol><Name>SvxReflector</Name><Port>" << g_Configure.GetUnsigned(g_Keys.svx.port) << "</Port></Protocol>" << std::endl;
```

- [ ] **Step 5: Full Docker build test**

Run: `scp files to server && ssh build.sh`
Expected: Docker build succeeds, container starts.

- [ ] **Step 6: Commit**

```
git add reflector/Protocols.cpp reflector/Makefile docker/Dockerfile reflector/Reflector.cpp
git commit -m "Register SvxReflector protocol, add libopus dependency, add XML output"
```

---

### Task 7: Integration test with SvxReflector

**Files:**
- Modify: server config `urfd.ini`

- [ ] **Step 1: Add SvxReflector config to urfd.ini on server**

```ini
[SvxReflector]
Enable = true
Host = svxreflector.fm-funknetz.de
Port = 5300
Callsign = URF363
Password = <password>
TG26250 = S
```

- [ ] **Step 2: Deploy and check logs**

Deploy to server, then check logs:

```bash
docker logs -f urfd 2>&1 | grep -i svx
```

Expected output:
```
SvxReflector TG mapping: TG26250 <-> Module S
SvxReflector: initialized, connecting to svxreflector.fm-funknetz.de
SvxReflector: TCP connected to svxreflector.fm-funknetz.de:5300
SvxReflector: auth response sent for URF363
SvxReflector: authentication successful
SvxReflector: received ServerInfo, client_id=<N>
SvxReflector: selected TG26250
```

- [ ] **Step 3: Test audio path**

- Send audio via FM-Funknetz TG26250
- Verify it appears on Module S in urfd dashboard
- Send audio from D-Star/DMR on Module S
- Verify it is heard on FM-Funknetz TG26250

- [ ] **Step 4: Test reconnect**

- Restart the SvxReflector or kill the TCP connection
- Verify urfd reconnects with backoff
- Verify audio resumes after reconnect

- [ ] **Step 5: Verify dashboard display**

- Check Overview Modules page: Module S should show `SvxReflector: TG 26250 svxreflector.fm-funknetz.de`

- [ ] **Step 6: Final commit with any fixes**

```
git add -A && git commit -m "SvxReflector integration: tested and working"
```
