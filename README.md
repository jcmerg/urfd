# URF363 - Universal Digital Voice Reflector

Fork of [urfd](https://github.com/nostar/urfd) with extended features for the URF363 reflector project.

**Version 3.2.1-dht** | Dashboard v2.6.0

## What's New in This Fork

### BMMmdvm Connector
Direct Brandmeister connection via the MMDVM protocol. Maps BM talkgroups to reflector modules with per-timeslot support.

```ini
[BMMmdvm]
Enable = true
MasterAddress = 2622.master.brandmeister.network
MasterPort = 62031
DmrId = 263135305
Password = yourpassword
Callsign = YOURCALL
TG26250 = S,TS2    # TG 26250 -> Module S on Timeslot 2
```

Each TG must also be configured as a static talkgroup in the BM Selfcare portal.

### XLX Interlink Support
Peer with XLX reflectors using the native XLX protocol (port 10002). DNS hostnames are supported in interlink entries.

```ini
# urfd.ini
[XLXPeer]
Enable = true
Port = 10002
```

```
# urfd.interlink - supports DNS hostnames
XLX269 xlx269.example.com A
XLX100 44.10.20.30 AFS
BM3104 bm3104.example.com E
URF270 urf270.example.com EF
```

The XLX interlink protocol has been verified compatible with [LX3JL/xlxd](https://github.com/LX3JL/xlxd). URF peers use port 10017, XLX/BM peers use port 10002 (auto-detected by callsign prefix).

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
- **Per-module mappings**: autolinks (YSF, NXDN, P25), TG mappings (BMMmdvm), USRP bridges
- **Enabled protocols**: name and port for each active protocol
- **Per-station protocol**: which protocol a user was heard on (DCS, BMMmdvm, YSF, etc.)

Module names are configured once in `urfd.ini` and automatically available in the dashboard.

### Dashboard v2.6.0
Complete redesign with dark mode theme.

**New pages:**
- **Active Users** - Connected nodes per module in card layout
- **Overview Modules** - Module table with DMR+ IDs, YSF DG-IDs, mappings, transcoder status, connected nodes (collapsible for large lists)
- **Enabled Protocols** - All active protocols with ports and type classification

**Features:**
- Dark mode with CSS custom properties
- Last Heard table with protocol column, pulsing TX indicator, module display
- MOTD/maintenance banner via `$PageOptions['MOTD']` in config.inc.php
- Module names read from urfd XML (single source of truth)
- Contact email obfuscated against bots
- CallingHome runs automatically via supervisor (every 5 min)
- QuadNet Live iframe with light background wrapper

### Bug Fixes
- Fix BM options string per-timeslot indexing for multi-TG configs
- Fix `Mode=both` DB loader failbit when file is empty
- Fix Via/Peer display matching both XLX and URF reflector name variants
- Fix callsign sanitization for malformed NXDN/DMR gateway callsigns (cherry-picked from dbehnke/urfd)
- Fix YSF CONN_REQ radio ID collision causing phantom module switches (cherry-picked from dbehnke/urfd)
- Fix transcoder module ID enforcement to prevent audio cross-contamination (cherry-picked from dbehnke/urfd)
- Remove spurious getsockname warning on ephemeral ports

## Introduction

The URF Multi-protocol Gateway Reflector Server, **urfd**, is part of the software system for a Digital Voice Network. It supports D-Star (DPlus, DCS, DExtra, G3), DMR (MMDVM, DMR+, BMMmdvm), M17, YSF, P25, NXDN and USRP (AllStar).

A key part of this is the hybrid transcoder, [tcd](https://github.com/n7tae/tcd), in a separate repository. The reflector can be built without a transcoder, but clients will only hear other clients using the same codec.

This build supports dual-stack operation (IPv4 + IPv6).

## Docker Deployment

This fork includes Docker support for easy deployment. See the `docker/` directory for all deployment files.

### Build and run

```bash
cd /opt/urfd
bash build.sh
```

The `build.sh` script builds the Docker image and restarts the container. It uses `docker-compose.yml` with `network_mode: host`.

### Container architecture

The container runs three services via supervisord:
- **urfd** - The reflector daemon
- **nginx + php-fpm** - Dashboard on port 8363
- **callhome** - Automatic CallingHome every 5 minutes

### Configuration files

All configuration is in `/opt/urfd/config/` (mounted as volume):
- `urfd.ini` - Main reflector configuration
- `urfd.interlink` - Peer linking (URF, XLX, BM peers with DNS support)
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
sudo apt install git apache2 php build-essential nlohmann-json3-dev libcurl4-gnutls-dev
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
- BMMmdvm TG mappings
- Database URLs for DMR ID, NXDN ID, YSF TX/RX lookups

### Dashboard

```bash
sudo cp -r ~/urfd/dashboard /var/www/urf
```

Edit `pgs/config.inc.php` for email, country, and CallingHome settings. Module names are read from the urfd XML output - no need for redundant `$PageOptions['ModuleNames']` configuration.

## Interlink / Peering

Three types of peering are supported in `urfd.interlink`:

| Type | Prefix | Default Port | Protocol | Requires |
|------|--------|-------------|----------|----------|
| URF Peer | `URF` | 10017 | URF native | - |
| XLX Peer | `XLX` | 10002 | XLX/BM | `[XLXPeer] Enable = true` |
| BM Peer | `BM` | 10002 | XLX/BM | `[XLXPeer] Enable = true` |

DNS hostnames are resolved via `getaddrinfo()` at load time. Both sides must list each other in their interlink files. DHT-based peering (no IP needed) is supported for URF peers when OpenDHT is enabled.

## Firewall

Required ports (only open ports for enabled protocols):

| Port | Protocol | Service |
|------|----------|---------|
| 80/tcp | HTTP | Dashboard |
| 8363/tcp | HTTP | Dashboard (Docker) |
| 8880/udp | DMR+ | DMO mode |
| 10002/udp | XLXPeer | XLX/BM peering |
| 10017/udp | URF | URF interlinking |
| 10100/tcp | TC | Transcoder |
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
| 62030/udp | MMDVM | DMR MMDVM |

## YSF

URF acts as a YSF Master providing Wires-X rooms (one per module). YSF users connect via hotspot software (Pi-Star/WPSD) which discovers URF reflectors through the XLX API. No registration at ysfreflector.de is needed. If `EnableDGID = true`, users can switch modules via DG-ID values (10=A, 11=B, ... 35=Z).

## Copyright

- Copyright (c) 2016 Jean-Luc Deltombe LX3JL and Luc Engelmann LX1IQ
- Copyright (c) 2022 Doug McLain AD8DP and Thomas A. Early N7TAE
- Copyright (c) 2024 Thomas A. Early N7TAE
- Copyright (c) 2025-2026 Jens-Christian Merg DL4JC (URF363 fork extensions)
