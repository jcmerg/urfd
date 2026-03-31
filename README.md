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
```

**Multi-TG per Module**: The first TG listed for a module becomes the primary and is used for outbound traffic (TX). Additional TGs on the same module are secondary and only receive inbound traffic (RX). Dynamic TGs added via the Admin interface are always secondary unless the module has no primary yet.

**FallbackDmrId**: When a callsign cannot be resolved to a DMR ID, this ID is used instead. If not configured or set to 0, the stream is dropped.

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
- **Dynamic TG Management**: Add/remove talkgroups at runtime with configurable timeout (TTL). For MMDVM, triggers a reconnect to the master with updated options string and sends a kerchunk to activate the TG on BrandMeister. For SVX, sends a SELECT_TG command via TCP.
- **Multi-TG per Module**: Dynamically add secondary TGs (RX only) to modules that already have a primary TG.
- **Protocol Reconnect**: Force reconnect of MMDVM or SVX connections.
- **Transcoder Statistics**: Connection status, active codec, packet counts, round-trip time (min/avg/max) per transcoded module.
- **Live Log Viewer**: Last 200 log lines with timestamps, auto-refreshing every 10 seconds.
- **Protocol Block Rules**: Shows active bidirectional block rules between protocols.
- **Hidden Access**: No visible link in navigation. Access via the pi symbol in the bottom-right corner or directly via `?show=admin`.

**Socket Protocol**: Line-delimited JSON over TCP. Authenticate first, then send commands with the returned token:
```json
{"cmd": "auth", "password": "..."}
{"cmd": "tg_add", "token": "...", "protocol": "mmdvm", "tg": 26207, "module": "S", "ts": 1, "ttl": 900}
{"cmd": "tg_remove", "token": "...", "protocol": "mmdvm", "tg": 26207}
{"cmd": "tg_list", "token": "..."}
{"cmd": "tc_stats", "token": "..."}
{"cmd": "reconnect", "token": "...", "protocol": "mmdvm"}
{"cmd": "log", "token": "...", "lines": 50}
{"cmd": "status", "token": "..."}
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
- **Per-module mappings**: autolinks (YSF, NXDN, P25), TG mappings (MMDVMClient), USRP bridges
- **Enabled protocols**: name and port for each active protocol
- **Per-station protocol**: which protocol a user was heard on (DCS, MMDVMClient, YSF, etc.)

Module names are configured once in `urfd.ini` and automatically available in the dashboard.

### Dashboard v2.6.0
Complete redesign with dark mode theme.

**New pages:**
- **Active Users** - Connected nodes per module in card layout
- **Overview Modules** - Module table with DMR+ IDs, YSF DG-IDs, mappings, transcoder status, connected nodes (collapsible for large lists)
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
- **Admin panel** (hidden): dynamic TG management, transcoder stats, live log, protocol controls. Access via pi symbol or `?show=admin`.

### D-Star Slow Data for Transcoded Streams
Transcoded streams (DMR, YSF, SVX, M17, P25, USRP → D-Star) now include proper D-Star slow data:
- **Header**: Caller callsign (MY), reflector callsign (RPT1/RPT2)
- **Text message**: Source protocol and TG/DG-ID, e.g. `via SVX TG317424`, `via DMR TG26363`, `via YSF DG28`

Header and text message alternate every superframe (~420ms), so D-Star radios display both callsign info and source routing info.

### Bug Fixes
- Fix missing end-of-TX frame on transcoded streams causing D-Star BER spikes at end of call
- Fix SVX last-frame audio artifacts (squelch tail noise transcoded to AMBE)
- Fix D-Star slow data missing TG number for dynamically mapped talkgroups
- Fix orphaned SVX clients remaining in Links after dynamic TG removal
- Fix CodecStream uninitialized statistics (garbage RTT values after restart)
- Fix thread-unsafe cout in TCSocket (interleaved log lines)
- Fix OPUS decoder state leak between SVX calls (flush on stream close)
- Fix options string per-timeslot indexing for multi-TG configs
- Fix `Mode=both` DB loader failbit when file is empty
- Fix Via/Peer display matching both XLX and URF reflector name variants
- Fix callsign sanitization for malformed NXDN/DMR gateway callsigns (cherry-picked from dbehnke/urfd)
- Fix YSF CONN_REQ radio ID collision causing phantom module switches (cherry-picked from dbehnke/urfd)
- Fix transcoder module ID enforcement to prevent audio cross-contamination (cherry-picked from dbehnke/urfd)
- Remove spurious getsockname warning on ephemeral ports
- Fix concurrent stream blocking on MMDVMClient (one stream per module)
- Fix DCS disconnect with 0x00 at byte 9
- Fix OpenStream module lookup using wrong field
- Suppress repeated "voice from" log messages (once per stream)
- Reject streams on modules without transcoder connection

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
- `urfd.ini` - Main reflector configuration
- `urfd.interlink` - Peer linking (URF, XLX, DCS peers with DNS support)
- `urfd.blacklist` / `urfd.whitelist` - Access control
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

## Copyright

- Copyright (c) 2016 Jean-Luc Deltombe LX3JL and Luc Engelmann LX1IQ
- Copyright (c) 2022 Doug McLain AD8DP and Thomas A. Early N7TAE
- Copyright (c) 2024 Thomas A. Early N7TAE
- Copyright (c) 2025-2026 Jens-Christian Merg DL4JC (URF363 fork extensions)
