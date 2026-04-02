# URF363 - Universal Digital Voice Reflector

Fork of [urfd](https://github.com/nostar/urfd) with extended features for the URF363 reflector project.

**Version 3.2.1-dht** | Dashboard v2.6.0

## What's New in This Fork

### MMDVMClient Connector
Connect to any DMR master server (e.g. BrandMeister) via the MMDVM protocol. Maps talkgroups to reflector modules with per-timeslot support. Multiple TGs can share a module: the first becomes the primary (TX/RX), additional TGs are secondary (inbound RX only). Dynamic TGs can be added at runtime via the Admin interface.

```ini
[MMDVMClient]
Enable = true
MasterAddress = master.example.com    # DMR master server
MasterPort = 62031
DmrId = 123456701
Password = yourpassword
Callsign = YOURCALL                   # Used for DMR-ID lookup on kerchunk
TG26250 = S,TS1    # TG 26250 -> Module S on Timeslot 1 (primary, TX/RX)
TG26207 = S,TS1    # TG 26207 -> Module S (secondary, RX only)
TG26363 = F,TS2    # TG 26363 -> Module F (primary)
# FallbackDmrId = 1234567  # For callers not in DMR database (omit = drop stream)
# BlockProtocols = SvxReflector,YSF  # Block audio from these protocols (comma-separated)
# BrandMeisterApiKey = eyJ0eXAiOiJKV1Qi...  # Optional: BM API for static TG management
```

**Multi-TG per Module**: The first TG listed for a module becomes the primary and is used for outbound traffic (TX). Additional TGs on the same module are secondary and only receive inbound traffic (RX). Dynamic TGs added via the Admin interface are always secondary unless the module has no primary yet.

**FallbackDmrId**: When a callsign cannot be resolved to a DMR ID, this ID is used instead. If not configured or set to 0, the stream is dropped.

**BrandMeisterApiKey** (optional): When set, enables BrandMeister REST API v2 integration. Get your key from BrandMeister SelfCare -> Profile -> Security -> API Keys. Effects:
- **Startup sync**: Fetches BM's static TGs, removes orphans (TGs not in config), adds missing ones
- **Dynamic TG add**: Registers as static on BM via API (no kerchunk needed)
- **Dynamic TG remove**: Deletes from BM via API
- Without API key, falls back to kerchunk behavior as before

### SvxReflector Client
Connect to SvxLink SvxReflector servers (e.g. FM-Funknetz) for bidirectional FM audio bridging. Uses TCP for signaling and UDP for OPUS-encoded audio. Requires transcoded modules. Dynamic TGs can be added at runtime via the Admin interface.

```ini
[SvxReflector]
Enable = true
Host = svxreflector.example.com
Port = 5300
Callsign = YOURCALL-HS
Password = yourpassword
TG26363 = S              # SvxReflector TG -> Module S
# BlockProtocols = MMDVMClient,USRP  # Block audio from these protocols (comma-separated)
RxGain = 0               # Incoming audio gain in dB (-40 to +40, default 0)
TxGain = 0               # Outgoing audio gain in dB (-40 to +40, default 0)
```

**BlockProtocols** (MMDVMClient and SvxReflector): Prevents audio routing between the specified protocols bidirectionally. Available protocols: `MMDVMClient`, `SvxReflector`, `DExtra`, `DPlus`, `DCS`, `DMRPlus`, `DMRMMDVM`, `YSF`, `M17`, `NXDN`, `P25`, `USRP`, `URF`, `XLXPeer`, `G3`. Comma-separated.

**RxGain / TxGain**: Static gain applied to SVX audio independently from USRP gain (which is configured in tcd.ini). RxGain is applied after OPUS decode before the transcoder, TxGain after the transcoder before OPUS encode. AGC in tcd still runs on SVX audio after RxGain.

### Dynamic Talkgroup Timer Behavior

Both MMDVM and SVX dynamic TGs have a 15-minute inactivity TTL. The timer is refreshed on any transmission start (DvHeader) on the module, regardless of which protocol originated the traffic. This means cross-protocol activity keeps all timers alive consistently.

**BrandMeister note**: BM maintains its own TG subscription timer independently. When the BM subscription expires, urfd's local mapping will also expire if there is no further activity. Use the **kerchunk** admin command to manually extend the BM-side subscription without needing a radio.

**SVX note**: When all dynamic SVX TGs expire, urfd sends `SELECT_TG(0)` to the SVX server to unsubscribe cleanly and stop receiving traffic for expired TGs.

### Admin Interface
Runtime management via a JSON-based TCP socket, accessible through the dashboard web panel. Runs in its own thread and does not block audio processing.

```ini
[Admin]
Enable = true
Port = 10101
Password = yoursecretpassword
# BindAddress = 127.0.0.1      # Default: loopback only
```

Dashboard config (`config.inc.php`):
```php
$Admin['Enable']   = true;
$Admin['Host']     = '127.0.0.1';
$Admin['Port']     = 10101;
$Admin['Password'] = 'yoursecretpassword';  # must match urfd.ini
```

**Features:**
- **Dynamic TG Management**: Add/remove talkgroups at runtime with configurable TTL. With BM API key: registers as static via REST API (no kerchunk needed). Without: kerchunk to activate on BM. For SVX, sends SELECT_TG via TCP.
- **Runtime Protocol Blocking**: Block/unblock any protocol pair bidirectionally at runtime. Not persisted — resets on restart. "Reset to Config Default" button restores INI settings.
- **Multi-TG per Module**: Dynamically add secondary TGs (RX only) to modules that already have a primary TG.
- **Kerchunk on Demand**: Send a kerchunk to BrandMeister for a specific TG without a radio. MMDVM only. Not needed when BM API key is configured.
- **Protocol Reconnect**: Force reconnect of MMDVM or SVX connections.
- **Transcoder Statistics**: Connection status, active codec, packet counts, round-trip time per transcoded module.
- **Live Log Viewer**: Last 200 log lines with timestamps, auto-refreshing every 10 seconds.
- **Hidden Access**: Access via the pi symbol in the bottom-right corner or directly via `?show=admin`.

**Socket Protocol**: Line-delimited JSON over TCP. Authenticate first, then send commands with the returned token:
```json
{"cmd": "auth", "password": "..."}
{"cmd": "tg_add", "token": "...", "protocol": "mmdvm", "tg": 26207, "module": "S", "ts": 1, "ttl": 900}
{"cmd": "tg_remove", "token": "...", "protocol": "mmdvm", "tg": 26207}
{"cmd": "tg_list", "token": "..."}
{"cmd": "kerchunk", "token": "...", "tg": 26207}
{"cmd": "tc_stats", "token": "..."}
{"cmd": "reconnect", "token": "...", "protocol": "mmdvm"}
{"cmd": "log", "token": "...", "lines": 50}
{"cmd": "status", "token": "..."}
{"cmd": "block", "token": "...", "a": "SVX", "b": "MMDVM"}
{"cmd": "unblock", "token": "...", "a": "SVX", "b": "MMDVM"}
{"cmd": "block_reset", "token": "..."}
```

### SIGHUP Configuration Reload

The reflector supports hot-reloading configuration without dropping client sessions. Send `SIGHUP` to the urfd process to trigger a reload.

**Reloaded on SIGHUP:**
- TG mappings (MMDVMClient and SvxReflector) — dynamic TGs are preserved
- Whitelist, blacklist, and interlink files
- Transcoder module assignment
- Reflector metadata (callsign, sponsor, country, URLs)
- Database refresh parameters

**Requires full restart:**
- Module list changes (adding/removing modules)
- Protocol enable/disable or port changes
- IP address binding changes
- Admin interface settings
- Echo module assignment

```bash
# Docker (signal must reach urfd, not supervisord)
docker exec urfd kill -HUP $(docker exec urfd pgrep urfd)

# Bare metal
kill -HUP $(cat /var/run/urfd.pid)
```

### Reflector Interlinking
Peer with URF, XLX and DCS reflectors. DNS hostnames are supported. Configure interlinking in `urfd.interlink`:

```
# urfd.interlink
# Format: <Callsign> <Address> <Modules> [Port] [Protocol]
URF270 urf270.example.com EF
XLX269 xlx269.example.com A
DCS002 dcs002.xreflector.net S             # native DCS protocol (default)
DCS002 dcs002.xreflector.net S XLX         # XLX protocol instead
```

DCS entries default to native DCS protocol (port 30051). Append `XLX` to use XLX peering (port 10002) instead. The protocol field is only supported for DCS entries.

XLX and DCS peering support optional callsign overrides (e.g. present as XLX363 instead of URF363):

```ini
# urfd.ini
[XLXPeer]
Enable = true               # required for XLX peers and DCS peers with XLX protocol
Port = 10002
# Callsign = XLX363         # optional: callsign for XLX peering

[DCS]
Port = 30051
# Callsign = DCS363         # optional: callsign for native DCS peering
```

### Echo Module
Built-in echo/parrot function. Assign it to any module - audio is recorded and played back after 3 seconds of silence.

```ini
[Modules]
Modules = AFSZ

[Echo]
Enable = true
Module = Z
```

Set `Enable = false` to disable without removing the configuration.

### M17 LSTN Support
M17 listen-only clients (LSTN packets) are accepted and registered as normal clients. This enables monitoring services like dvref.com to check reflector availability.

### Protocol Enable Flags
All protocols can now be individually enabled/disabled. Default is `true` (backwards-compatible).

```ini
[DPlus]
Enable = false
Port = 20001

[P25]
Enable = false
Port = 41000
```

### Extended XML Output
The XML status file now includes:

- **Reflector metadata**: callsign, country, sponsor, dashboard URL, email
- **Module configuration**: description, linked node count, transcoded status, DMR+ TG ID, YSF DG-ID
- **Per-module mappings**: autolinks (YSF, NXDN, P25), TG mappings (MMDVMClient, SvxReflector) including dynamic TGs with remaining TTL
- **Enabled protocols**: name and port for each active protocol
- **Per-station protocol**: which protocol a user was heard on (DCS, MMDVMClient, YSF, etc.)
- **Dynamic TG indicators**: `<Dynamic>true</Dynamic>` and `<Remaining>` seconds for runtime-added TGs

Module names are configured once in `urfd.ini` and automatically available in the dashboard.

The JSON report also includes a `DynamicTGs` array with protocol, TG, module, and remaining seconds for all active dynamic mappings.

### Dashboard v2.6.0
Complete redesign with dark mode theme.

**New pages:**
- **Active Users** - Connected nodes per module in card layout
- **Overview Modules** - Module table with DMR+ IDs, YSF DG-IDs, mappings (static + dynamic), transcoder status, connected nodes (collapsible for large lists)
- **Enabled Protocols** - All active protocols with ports and type classification

**Features:**
- Dark mode with CSS custom properties
- Mobile-responsive tables for peers, repeaters, users
- Last Heard table with protocol column, pulsing TX indicator, module display
- MOTD/maintenance banner via `$PageOptions['MOTD']` in config.inc.php
- Module names read from urfd XML (single source of truth)
- Contact email obfuscated against bots
- CallingHome runs automatically via supervisor (every 5 min)
- QuadNet Live: native PHP proxy table with search and auto-refresh (replaces iframe)
- Reflector list: client-side search and pagination (25 per page) with CSS status dots
- QRZ links use callsign without module suffix
- **Admin panel** (hidden): dynamic TG management with kerchunk button, transcoder stats, live log, protocol controls. Access via pi symbol or `?show=admin`.

### D-Star Slow Data for Transcoded Streams
Transcoded streams (DMR, YSF, SVX, M17, P25, USRP -> D-Star) now include proper D-Star slow data:
- **Header**: Caller callsign (MY), reflector callsign (RPT1/RPT2)
- **Text message**: Source protocol and TG/DG-ID, e.g. `via SVX TG317424`, `via DMR TG26363`, `via YSF DG28`
- **Operator name**: If the caller's callsign is found in the DMR ID database, the operator's name is shown alternating with the protocol/TG info (~5 seconds each)

Header and text message alternate every superframe (~420ms). When an operator name is available, the text message cycles between protocol/TG info and name every ~5 seconds:

```
[Header: DL4JC > URF363 S] → [via DMR TG26363] → [Header] → [via DMR TG26363] → ...
                    (after ~5s)
[Header: DL4JC > URF363 S] → [Jens-Christian]   → [Header] → [Jens-Christian]   → ...
                    (after ~5s, back to via/TG)
```

Name lookup works for all protocols — any callsign with a DMR ID database entry will have its name displayed on D-Star radios.

## Introduction

The URF Multi-protocol Gateway Reflector Server, **urfd**, is part of the software system for a Digital Voice Network. It supports D-Star (DPlus, DCS, DExtra, G3), DMR (MMDVM, DMR+, MMDVMClient), M17, YSF, P25, NXDN, USRP (AllStar) and SvxReflector (SvxLink FM).

A key part of this is the hybrid transcoder, [tcd](https://github.com/jcmerg/tcd), in a separate repository. The reflector can be built without a transcoder, but clients will only hear other clients using the same codec.

This build supports dual-stack operation (IPv4 + IPv6).

## Docker Deployment

This fork includes Docker support for easy deployment. See [`docker/README.md`](docker/README.md) for full details.

### Quick start

```bash
cd docker
docker compose build
docker compose up -d
```

The transcoder ([tcd](https://github.com/jcmerg/tcd)) runs on a separate host with a DVSI AMBE device attached via USB. With the [md380_vocoder](https://github.com/jcmerg/md380_vocoder) library, only one DVSI device is needed — DMR/YSF runs in software.

### Container architecture

The urfd container runs three services via supervisord:
- **urfd** - The reflector daemon
- **nginx + php-fpm** - Dashboard on port 8363
- **callhome** - Automatic CallingHome every 5 minutes

### Configuration files

All configuration is in `/opt/urfd/config/` (mounted as volume):
- `urfd.ini` - Main reflector configuration (supports SIGHUP reload)
- `urfd.interlink` - Peer linking (URF, XLX, DCS peers with DNS support)
- `urfd.blacklist` / `urfd.whitelist` - Access control (reloaded on SIGHUP)
- `urfd.terminal` - G3 terminal configuration

Dashboard config at `/opt/urfd/dashboard/config.inc.php` (mounted into container):
- `$PageOptions['MOTD']` - Maintenance banner (shown on all pages)
- `$PageOptions['ContactEmail']` - Footer contact (obfuscated)
- `$CallingHome['Active']` - Enable/disable XLX directory registration

## Installation (Bare Metal)

### Required packages

```bash
sudo apt update && sudo apt upgrade
sudo apt install git apache2 php build-essential nlohmann-json3-dev libcurl4-gnutls-dev libopus-dev libssl-dev
```

### OpenDHT support (recommended)

```bash
sudo apt install libopendht-dev
```

If not available as a package, build from source: [OpenDHT Wiki](https://github.com/savoirfairelinux/opendht/wiki/Build-the-library).

### Build

```bash
git clone https://github.com/jcmerg/urfd.git
cd urfd/reflector
cp ../config/* .
# Edit urfd.ini with your configuration
make
sudo make install
```

### Configuration

Edit `urfd.ini` to set:
- Reflector callsign and IP addresses
- Modules (A-Z, non-contiguous allowed)
- Transcoder port and modules (set port to 0 if no transcoder)
- Protocol-specific settings (ports, enable flags, autolink modules)
- Echo module assignment
- MMDVMClient TG mappings (multi-TG per module supported)
- SvxReflector connection and TG mappings
- Admin interface (port, password, bind address)
- Database URLs for DMR ID, NXDN ID, YSF TX/RX lookups

### Dashboard

```bash
sudo cp -r ~/urfd/dashboard /var/www/urf
```

Edit `pgs/config.inc.php` for email, country, CallingHome, and Admin settings. Module names are read from the urfd XML output. For the Admin panel, set `$Admin['Enable'] = true` and match the password with `urfd.ini`.

## Interlink / Peering

Three types of peering are supported in `urfd.interlink`:

| Type | Prefix | Default Port | Protocol | Requires |
|------|--------|-------------|----------|----------|
| URF Peer | `URF` | 10017 | URF native | - |
| XLX Peer | `XLX` | 10002 | XLX | `[XLXPeer] Enable = true` |
| DCS Peer | `DCS` | 30051 | DCS native | - |
| DCS Peer (XLX) | `DCS` + `XLX` | 10002 | XLX | `[XLXPeer] Enable = true` |

DNS hostnames are resolved via `getaddrinfo()` at load time. Both sides must list each other in their interlink files. DHT-based peering (no IP needed) is supported for URF peers when OpenDHT is enabled.

## Firewall

Required ports (only open ports for enabled protocols):

| Port | Protocol | Service |
|------|----------|---------|
| 80/tcp | HTTP | Dashboard |
| 8363/tcp | HTTP | Dashboard (Docker) |
| 8880/udp | DMR+ | DMO mode |
| 10002/udp | XLXPeer | XLX/DCS peering |
| 10017/udp | URF | URF interlinking |
| 10100/tcp | TC | Transcoder |
| 10101/tcp | Admin | Admin socket (loopback only) |
| 12345-12346/udp | G3 | Icom Terminal |
| 17000/udp | M17 | M17 protocol |
| 20001/udp | DPlus | D-Star DPlus |
| 30001/udp | DExtra | D-Star DExtra |
| 30051/udp | DCS | D-Star DCS |
| 32000/udp | USRP | AllStar |
| 40000/udp | G3 | Icom Terminal data |
| 41000/udp | P25 | P25 protocol |
| 41400/udp | NXDN | NXDN protocol |
| 42000/udp | YSF | YSF/C4FM protocol |
| 5300/tcp+udp | SvxReflector | SvxLink (outgoing only) |
| 62030/udp | MMDVM | DMR MMDVM |

## YSF

URF acts as a YSF Master providing Wires-X rooms (one per module). YSF users connect via hotspot software (Pi-Star/WPSD) which discovers URF reflectors through the XLX API. No registration at ysfreflector.de is needed. If `EnableDGID = true`, users can switch modules via DG-ID values (10=A, 11=B, ... 35=Z).

## Changes from upstream

- **MMDVMClient connector**: Full MMDVM protocol client for BrandMeister/DMR+ masters with multi-TG per module, primary/secondary routing, per-timeslot support, fallback DMR ID, options string generation
- **SvxReflector client**: TCP/UDP client for SvxLink reflector servers with OPUS codec, bidirectional FM audio bridging, configurable RX/TX gain, SELECT_TG protocol support
- **Admin interface**: JSON-over-TCP socket for runtime management — dynamic TG add/remove with auto-kerchunk, protocol reconnect, transcoder stats, live log viewer, protocol block rules
- **Dashboard v2.6.0**: Complete dark mode redesign, admin panel, module overview with TG mappings, protocol list, QuadNet Live proxy, reflector list with search/pagination
- **Dynamic TG management**: Runtime TG add/remove via admin API, configurable TTL, cross-protocol timer refresh, kerchunk-on-demand for BrandMeister, SELECT_TG(0) cleanup on expiry
- **SIGHUP config reload**: Hot-reload TG mappings, whitelist/blacklist/interlink, transcoder modules without dropping client sessions. Thread-safe via sigwait, exception-safe with config rollback
- **D-Star slow data**: Transcoded streams include caller callsign, protocol/TG info, and operator name from DMR ID database, rotating every ~5 seconds
- **Protocol blocking**: Bidirectional per-protocol audio routing blocks (e.g. block SVX→MMDVM)
- **Extended XML/JSON output**: Module mappings (static + dynamic with TTL), reflector metadata, protocol list, per-user protocol info, dynamic TG array
- **Echo module**: Built-in parrot with enable/disable flag
- **M17 LSTN support**: Listen-only M17 clients accepted for monitoring services
- **DCS interlinking**: Native DCS reflector-to-reflector peering with PeerCallsign and protocol field in interlink file
- **BrandMeister API integration**: Optional REST API v2 for static TG management — startup sync (remove orphans, add missing), API-based TG add/remove instead of kerchunk, configurable via `BrandMeisterApiKey`
- **Runtime protocol blocking**: Bidirectional block/unblock of any protocol pair at runtime via admin dashboard, thread-safe with shared_mutex, resets on restart
- **SVX reconnect fix**: TcpSendFrame auto-disconnects on send failure for immediate reconnect instead of waiting for heartbeat timeout
- **MMDVM late-entry**: Resolves DMR ID from active stream callsign (prefers cached ID from source protocol over DB lookup), rate-limited warning
- **Self-echo prevention**: MMDVMClient blocks self-routing back to BrandMeister to prevent audio loops
- **SVX/USRP codec separation**: Independent codec paths (`ECodecType::svx` vs `ECodecType::usrp`) with separate gain control — SVX gain in urfd.ini, USRP gain in tcd.ini
- **Transcoder resilience**: TCP keepalive + non-blocking poll for dead connection detection, queue drain on disconnect. Non-blocking accept/connect, client-side keepalive, active dead socket probing, automatic reconnect after network outages
- **Database HTTP retry**: Retry with backoff on failed initial HTTP load for DMR/NXDN/YSF databases (prevents empty databases after transient network errors)
- **Service optimizations**: Realtime scheduling and voice CPU priority in systemd service
- **Thread-safe logging**: Atomic cout via ostringstream to prevent interleaved log lines from concurrent threads
- **Cherry-picked from [dbehnke/urfd](https://github.com/dbehnke/urfd)**: Callsign sanitization, YSF radio ID collision fix, transcoder module ID enforcement, DG-ID module selection, CConfigure::GetBoolean safety

## Copyright

- Copyright (c) 2016 Jean-Luc Deltombe LX3JL and Luc Engelmann LX1IQ
- Copyright (c) 2022 Doug McLain AD8DP and Thomas A. Early N7TAE
- Copyright (c) 2024 Thomas A. Early N7TAE
- Copyright (c) 2025-2026 Jens-Christian Merg DL4JC (URF363 fork extensions)
