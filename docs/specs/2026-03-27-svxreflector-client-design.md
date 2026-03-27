# SvxReflector Client Protocol for urfd

**Date:** 2026-03-27
**Status:** Approved

## Overview

Add a SvxReflector client protocol to urfd, allowing direct connection to
SvxReflector servers (e.g., FM-Funknetz). urfd acts as a single node from the
SvxReflector's perspective. Audio is transported as OPUS over UDP, decoded to
PCM internally, and routed through the existing transcoder pipeline.

## Architecture

```
urfd (SvxReflectorClient)
    |
    +-- TCP:5300 --> SvxReflector (FM-Funknetz etc.)
    |   +-- Handshake: ProtoVer(2,0) -> AuthChallenge -> AuthResponse(HMAC-SHA1)
    |   +-- ServerInfo (Codec-Liste, Client-ID)
    |   +-- NodeInfo + SelectTG
    |   +-- Heartbeat (alle ~10s)
    |   +-- TalkerStart/Stop Signaling
    |
    +-- UDP:5300 --> gleicher SvxReflector
        +-- Heartbeat (alle ~15s)
        +-- MsgUdpAudio (OPUS-encoded Frames)
        +-- MsgUdpFlushSamples (End-of-Transmission)
```

Audio path:

```
Incoming: SvxReflector -> OPUS frames -> opus_decode() -> PCM -> Transcoder -> AMBE/IMBE/Codec2
Outgoing: AMBE/IMBE/Codec2 -> Transcoder -> PCM -> opus_encode() -> OPUS frames -> SvxReflector
```

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Role | Client only | Connecting to existing SvxReflectors; server can be added later |
| TG mapping | Fixed config, TG-to-Module | Consistent with BMMmdvm pattern |
| Protocol version | V2 only | Password auth (HMAC-SHA1), no TLS/encryption needed |
| Audio codec | OPUS | Standard codec on SvxReflectors, best quality |
| Module restriction | Transcoded modules only | PCM needs transcoder for DMR/D-Star clients |
| Connection mode | Auto-connect at start | Like all other urfd protocols, with reconnect backoff |

## Configuration

New section `[SvxReflector]` in `urfd.ini`:

```ini
[SvxReflector]
Enable = true
Host = svxreflector.fm-funknetz.de
Port = 5300
Callsign = URF363
Password = geheim
Codec = OPUS

; TG-to-Module mapping
TG26250 = S
TG26363 = F
```

Config keys in `JsonKeys.h`:
- `svxEnable`, `svxHost`, `svxPort`, `svxCallsign`, `svxPassword`, `svxCodec`
- `svxTG<n>` for TG-to-module mappings

Validation:
- Host: non-empty string (DNS or IP)
- Port: 1024-65535, default 5300
- Callsign: valid amateur radio callsign
- Password: non-empty string
- Module in TG mapping: must be a transcoded module

## New Files

| File | Purpose | ~LOC |
|------|---------|------|
| `SvxReflectorClient.h/cpp` | Client class, inherits CClient | ~80 |
| `SvxReflectorProtocol.h/cpp` | Protocol plugin, inherits CProtocol | ~1000 |

## Changes to Existing Files

| File | Change | ~Lines |
|------|--------|--------|
| `Defines.h` | `EProtocol::svxreflector`, keepalive constants, IP flags | 4 |
| `JsonKeys.h` | Config keys for `[SvxReflector]` | 10 |
| `Configure.cpp` | Validation of new config keys | 15 |
| `Protocols.cpp` | `#include` + init block | 6 |
| `Reflector.cpp` | XML `<Mapping>` output for SvxReflector TGs | 15 |
| `Makefile` | New .o files, `-lopus` linker flag | 3 |
| `Dockerfile` | `libopus-dev` (build), `libopus0` (runtime) | 2 |

**Not modified:** Reflector core, Protocol.h/cpp, PacketStream, CodecStream,
Router, any other protocol, dashboard PHP (mappings display generically).

## Class Structure

```
CSvxReflectorProtocol : CProtocol
+-- Initialize()        -- TCP+UDP sockets, start connect
+-- Task()              -- Main loop: TCP/UDP receive, reconnect logic
+-- HandleQueue()       -- Reflector->SvxReflector: PCM->OPUS encode, send
+-- HandleKeepalives()  -- TCP+UDP heartbeat, timeout detection
|
+-- Connect()           -- TCP handshake: ProtoVer, Auth, ServerInfo, NodeInfo
+-- SelectTG()          -- Send MsgSelectTG for each configured TG
+-- Disconnect()        -- Clean disconnect
|
+-- OnTcpMessage()      -- TCP dispatcher: AuthChallenge, AuthOk, TalkerStart/Stop
+-- OnUdpAudio()        -- OPUS decode -> CDvFramePacket(PCM) -> OnDvFramePacketIn()
+-- OnUdpFlush()        -- End-of-transmission -> LastPacket
|
+-- EncodeUdpAudio()    -- PCM -> OPUS encode -> UDP send
|
+-- m_TcpFd             -- TCP socket file descriptor
+-- m_OpusDecoder       -- OpusDecoder*
+-- m_OpusEncoder       -- OpusEncoder*
+-- m_TGMap             -- TG<->Module mapping (like BMMmdvm)
+-- m_ReconnectTimer    -- Backoff timer for reconnect
```

## Connection Lifecycle

### Handshake

```
1. DNS resolve (Host -> IP)
2. TCP connect to Host:Port
3. Send MsgProtoVer(2,0)
4. Receive MsgAuthChallenge(nonce)
5. Send MsgAuthResponse(HMAC-SHA1(password, nonce) + callsign)
6. Receive MsgAuthOk (on error: disconnect, reconnect timer)
7. Receive MsgServerInfo -> store client_id, verify OPUS in codec list
8. Send MsgNodeInfo (callsign, metadata as JSON)
9. Open UDP socket, send initial UDP heartbeat (server learns UDP address)
10. Send MsgSelectTG for each configured TG
11. Connection established
```

### Reconnect Strategy

- On disconnect (TCP closed, heartbeat timeout): immediate cleanup
- Reconnect with backoff: 5s, 10s, 30s, 60s, then every 60s
- On auth error (wrong password): longer backoff (5min), log warning
- Log message on each attempt

### Heartbeat

- TCP: send every 10s, timeout after 15s without receive
- UDP: send every 15s, timeout after 60s without receive
- On UDP timeout with TCP ok: resend UDP heartbeat (NAT recovery)

## Audio Stream Lifecycle

### Incoming (SvxReflector -> urfd)

```
MsgTalkerStart(TG) -> TG->Module lookup -> set client as master
MsgUdpAudio        -> opus_decode() -> PCM frame -> OnDvFramePacketIn()
MsgUdpFlushSamples -> LastPacket -> close stream
```

### Outgoing (urfd -> SvxReflector)

```
HandleQueue() Pop Header -> start opus encoder state
HandleQueue() Pop Frame  -> GetCodecData(ECodecType::usrp) -> PCM -> opus_encode() -> MsgUdpAudio
HandleQueue() Pop Last   -> MsgUdpFlushSamples
```

## TCP Framing & Serialization

### TCP Frame Format

```
[4 bytes big-endian length][payload]
Payload: [2 bytes uint16_t message type][message-specific fields]
```

### TCP Message Types (subset we implement)

| Type | Name | Direction | Fields |
|------|------|-----------|--------|
| 1 | Heartbeat | Both | (empty) |
| 5 | ProtoVer | ->Server | major(2), minor(2) |
| 10 | AuthChallenge | <-Server | nonce(20 bytes) |
| 11 | AuthResponse | ->Server | digest(20 bytes), callsign(string) |
| 12 | AuthOk | <-Server | (empty) |
| 13 | Error | <-Server | message(string) |
| 100 | ServerInfo | <-Server | client_id(2), codecs(list), nodes(list) |
| 104 | TalkerStart | <-Server | tg(4), callsign(string) |
| 105 | TalkerStop | <-Server | tg(4), callsign(string) |
| 106 | SelectTG | ->Server | tg(4) |
| 111 | NodeInfo | ->Server | JSON string |

### UDP Message Types

| Type | Name | Direction | Fields |
|------|------|-----------|--------|
| 1 | UdpHeartbeat | Both | client_id(2) |
| 101 | UdpAudio | Both | tg(4), audio_data(bytes) |
| 102 | UdpFlushSamples | Both | (empty) |

### Serialization

Simple helper functions in `SvxReflectorProtocol.cpp`:
- `PackUint16/32(buf, val)` -- big-endian
- `PackString(buf, str)` -- length-prefixed
- `UnpackUint16/32(buf, pos)`
- `UnpackString(buf, pos)`

No external serialization framework. Raw TCP/UDP with custom framing.

## OPUS Integration

OPUS is used **only** inside `SvxReflectorProtocol.cpp`:

```
Incoming: OPUS frame -> opus_decode(8000Hz) -> int16_t pcm[160] -> CDvFramePacket(ECodecType::usrp)
Outgoing: CDvFramePacket -> GetCodecData(ECodecType::usrp) -> int16_t pcm[160] -> opus_encode(8000Hz) -> UDP
```

We reuse `ECodecType::usrp` (= PCM) -- no new codec type needed since OPUS is
only a transport format and everything is PCM internally.

Sample rate: libopus supports 8 kHz natively via `opus_decode()` /
`opus_encode()` with 8000 Hz sample rate parameter. No resampling code needed.

Dependencies:
- Build: `libopus-dev` (Dockerfile)
- Runtime: `libopus0` (Dockerfile)
- Linker: `-lopus` (Makefile)
- Header: `#include <opus/opus.h>` (only in SvxReflectorProtocol.cpp)

## XML & Dashboard

### XML Output (Reflector.cpp)

Add `<Mapping>` elements per module for SvxReflector TG mappings, same pattern
as BMMmdvm:

```xml
<Mapping>
  <Protocol>SvxReflector</Protocol>
  <Type>TG</Type>
  <ID>26250</ID>
  <RemoteName>fm-funknetz.de</RemoteName>
</Mapping>
```

The SvxReflector connection also appears as a connected client in the nodes/links
list, showing callsign and protocol type.

### Dashboard

No PHP changes needed. `modulesd.php` already renders `<Mapping>` elements
generically as blue labels. The new mappings will display automatically as:
`SvxReflector: TG 26250 fm-funknetz.de`

## Testing

- Unit: Serialization helpers (pack/unpack roundtrip)
- Integration: Connect to a local SvxReflector instance, verify handshake
- Audio: Send test tone through SvxReflector -> verify PCM arrives at urfd module
- Reconnect: Kill TCP connection, verify reconnect with backoff
- Multi-TG: Configure two TGs on different modules, verify routing
