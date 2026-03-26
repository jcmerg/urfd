# URF363 - Universal Digital Voice Reflector

Fork of [urfd](https://github.com/nostar/urfd) with extended features for the URF363 reflector project.

**Version 3.2.1-dht** | Dashboard v2.6.0

## What's New in This Fork

### BMMmdvm Connector
Direct Brandmeister connection via the Homebrew/MMDVM protocol. Maps BM talkgroups to reflector modules with per-timeslot support.

```ini
[BMHomebrew]
Enable = true
MasterAddress = 2622.master.brandmeister.network
MasterPort = 62031
DmrId = 263135305
Password = yourpassword
Callsign = YOURCALL
TG26250 = S,TS2    # TG 26250 -> Module S on Timeslot 2
```

Each TG must also be configured as a static talkgroup in the BM Selfcare portal.

### Echo Module
Built-in echo/parrot function. Assign it to any module - audio is recorded and played back after 3 seconds of silence.

```ini
[Modules]
Modules = AFSZ

[Echo]
Module = Z
```

The echo module is automatically labeled "Echo" in the dashboard and hidden from protocol listings.

### Protocol Enable Flags
All protocols can now be individually enabled/disabled. Default is `true` (backwards-compatible). Previously only BM, USRP, G3 and BMHomebrew had enable flags.

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

Module names are configured once in `urfd.ini` and automatically available in the dashboard - no need for redundant PHP configuration.

### Dashboard v2.6.0
Complete redesign with dark mode theme.

**New pages:**
- **Active Users** - Connected nodes per module in card layout
- **Overview Modules** - Module table with DMR+ IDs, YSF DG-IDs, mappings, transcoder status, connected nodes (collapsible for large lists)
- **Enabled Protocols** - All active protocols with ports and type classification

**Improvements:**
- Dark mode with CSS custom properties
- Last Heard table with protocol column and pulsing TX indicator
- Module names read from urfd XML (single source of truth)
- Unicode emoji icons replacing black PNG images
- Via column with nowrap, clean layout

## Introduction

The URF Multi-protocol Gateway Reflector Server, **urfd**, is part of the software system for a Digital Voice Network. It supports D-Star (DPlus, DCS, DExtra, G3), DMR (MMDVM, DMR+, BMMmdvm), M17, YSF, P25, NXDN and USRP (AllStar).

A key part of this is the hybrid transcoder, [tcd](https://github.com/n7tae/tcd), in a separate repository. The reflector can be built without a transcoder, but clients will only hear other clients using the same codec.

This build supports dual-stack operation (IPv4 + IPv6).

## Docker Deployment

This fork includes Docker support for easy deployment.

### Build and run

```bash
cd /opt/urfd
bash build.sh
```

The `build.sh` script builds the Docker image and restarts the container. It uses `docker-compose.yml` with `network_mode: host`.

### Configuration files

All configuration is in `/opt/urfd/config/`:
- `urfd.ini` - Main reflector configuration
- `urfd.interlink` - Peer linking configuration
- `urfd.blacklist` / `urfd.whitelist` - Access control
- `urfd.terminal` - G3 terminal configuration

The dashboard config is at `/opt/urfd/dashboard/config.inc.php` (mounted into the container).

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
- Database URLs for DMR ID, NXDN ID, YSF TX/RX lookups

### Dashboard

```bash
sudo cp -r ~/urfd/dashboard /var/www/urf
```

Edit `pgs/config.inc.php` for email, country, and CallingHome settings. Module names are now read from the urfd XML output.

## Firewall

Required ports (only open ports for enabled protocols):

| Port | Protocol | Service |
|------|----------|---------|
| 80/tcp | HTTP | Dashboard |
| 8880/udp | DMR+ | DMO mode |
| 10002/udp | BM | Brandmeister peering |
| 10017/udp | URF | Reflector interlinking |
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

## Copyright

- Copyright (c) 2016 Jean-Luc Deltombe LX3JL and Luc Engelmann LX1IQ
- Copyright (c) 2022 Doug McLain AD8DP and Thomas A. Early N7TAE
- Copyright (c) 2024 Thomas A. Early N7TAE
- Copyright (c) 2025-2026 JC Merg DL4JC (URF363 fork extensions)
