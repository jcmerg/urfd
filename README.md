# URF363 - Universal Digital Voice Reflector

Fork of [urfd](https://github.com/nostar/urfd) with extended features for the URF363 reflector project.

**Version 3.2.1-dht** | Dashboard v2.6.0

## What's New in This Fork

### MMDVMClient Connector
Connect to any DMR master server (e.g. BrandMeister) via the MMDVM protocol. Maps talkgroups to reflector modules with per-timeslot support. Multiple TGs can share a module: the first becomes the primary (TX/RX), additional TGs are secondary (inbound RX only). Dynamic TGs can be added at runtime via the Admin interface.

```ini
[MMDVMClient]
Enable = true
MasterAddress = master.example.com  # MMDVM master server IP or DNS hostname
MasterPort = 62031
# LocalPort = 12345                 # Fixed local UDP source port (omit = OS-assigned)
DmrId = 1234567                     # Your registered DMR/hotspot ID
Password = changeme                 # MMDVM hotspot security password
Callsign = YOURCALL                 # Callsign (used for DMR-ID lookup on kerchunk)
Latitude = 0.0
Longitude = 0.0
Location = My City
Description = My URF Reflector
URL = https://example.com
RxFreq = 430412500
TxFreq = 439812500
# FallbackDmrId = 1234567           # DMR ID for unknown callers (0 or omit = drop)
# BlockProtocols = SvxReflector,YSF # Block protocols bidirectionally
#
# BrandMeister API (optional): sync static TGs, add/remove via API instead of kerchunk
# Get your API key from BrandMeister SelfCare -> Profile -> Security -> API Keys
# BrandMeisterApiKey = eyJ0eXAiOiJKV1Qi...
#
# TG mapping: TG<number> = <Module>[,TS<1|2>]
# First TG per module = primary (TX/RX). Additional = secondary (RX only).
# Default timeslot is TS2.
TG91 = A,TS2                        # Worldwide -> Module A (primary)
# TG262 = A,TS2                     # Germany -> Module A (secondary, RX only)
# TG26363 = B,TS1                   # Local -> Module B (primary)
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
Host = svxreflector.example.com    # DNS hostname or IP
Port = 5300                         # TCP and UDP port (same)
Callsign = YOURCALL-HS             # Node callsign (as registered on server)
Password = changeme                 # Authentication password
# BlockProtocols = MMDVMClient,USRP # Block protocols bidirectionally
# RxGain = 0                        # Incoming audio gain in dB (-40 to +40, default 0)
# TxGain = 0                        # Outgoing audio gain in dB (-40 to +40, default 0)
#
# TG mapping: TG<number> = <Module>
# Modules must be transcoded. Multiple TGs per module supported.
# TG26363 = S
```

**BlockProtocols** (all client protocols): Supported on MMDVMClient, SvxReflector, DCSClient, DExtraClient, DPlusClient, and YSFClient. Prevents audio from the listed source protocols from being routed through this connector. Each client protocol also always blocks self-routing (e.g. DCSClient never routes DCSClient-originated audio back out). Available protocol names: `MMDVM`, `MMDVMClient`, `SvxReflector`, `DExtra`, `DExtraClient`, `DPlus`, `DPlusClient`, `DCS`, `DCSClient`, `DMRPlus`, `YSF`, `YSFClient`, `M17`, `NXDN`, `P25`, `USRP`, `URF`, `XLXPeer`, `G3`. Comma-separated.

**RxGain / TxGain**: Static gain applied to SVX audio independently from USRP gain (which is configured in tcd.ini). RxGain is applied after OPUS decode before the transcoder, TxGain after the transcoder before OPUS encode. AGC in tcd still runs on SVX audio after RxGain. Default is 0 dB for both.

### SVXServer (Beta)
Accept incoming connections from SvxLink nodes. Acts as a SvxReflector server (protocol V2, HMAC-SHA1 auth, OPUS audio). Requires transcoded modules. Users authenticate with callsign + password. TG mapping works like SvxReflector client — clients select TGs via `MsgSelectTG`. Users and TGs can be managed at runtime via the Admin interface.

```ini
[SVXServer]
Enable = true
Port = 5300                         # TCP and UDP port (same)
# BlockProtocols = MMDVMClient,USRP # Block protocols bidirectionally
# RxGain = 0                        # Incoming audio gain in dB (-40 to +40)
# TxGain = 0                        # Outgoing audio gain in dB (-40 to +40)
#
# TG mapping: TG<number> = <Module>
# Modules must be transcoded. Multiple TGs per module supported.
# TG26363 = A
#
# User authentication: <Callsign> = <Password>
# Users can also be added/removed at runtime via the Admin interface.
# N0CALL-HS = changeme
```

**User management**: Users can be added/removed at runtime via the Admin interface or configured statically in `urfd.ini`. Each user authenticates with callsign + password using HMAC-SHA1.

**TG mapping**: Same semantics as SvxReflector client — modules must be transcoded. Multiple TGs per module supported. Dynamic TGs can be added via the Admin interface with configurable TTL.

**Admin commands**:
```json
{"cmd": "svxserver_user_add", "token": "...", "callsign": "N0CALL-HS", "password": "secret"}
{"cmd": "svxserver_user_remove", "token": "...", "callsign": "N0CALL-HS"}
{"cmd": "svxserver_user_list", "token": "..."}
```

### MMDVM Server
Accept incoming connections from MMDVM hotspots and repeaters directly (no BrandMeister needed). Implements the HomeBrew DMR protocol (RPTL→RPTK→RPTC→RPTO handshake). Works without transcoder for pure DMR-to-DMR operation; transcoded modules needed only for cross-mode bridging. TG mapping works like MMDVMClient — TGs map to modules with timeslot support.

```ini
[MMDVM]
Enable = true
Port = 62030
# FallbackDmrId = 0                # DMR ID for unknown callers (0 or omit = drop)
# BlockProtocols = SVXServer,USRP  # Block protocols bidirectionally
#
# TG mapping: TG<number> = <Module>[,TS<1|2>]
# First TG per module = primary (TX/RX). Additional = secondary (RX only).
# Default timeslot is TS2.
TG9 = A,TS2                        # Common hotspot default
# TG26363 = F,TS2                  # Local TG -> Module F
#
# User authentication: <BASE-DMRID> or <CALLSIGN> = <password>
# If no users are configured, authentication is disabled (open access).
# One entry covers all 9-digit extensions (e.g. 2631353 covers 263135300-99).
# Users can also be added/removed at runtime via the Admin interface.
# 2631353 = changeme               # 7-digit base DMR ID
# DL4JC = changeme                 # Callsign (resolved to DMR ID at startup)
```

**Extended DMR IDs**: BrandMeister-style hotspots append a 2-digit suffix to the 7-digit base DMR ID (e.g. `263135301` = base `2631353` + suffix `01`). urfd strips the suffix automatically — one user entry covers all extensions.

**User authentication**: Users can authenticate with a Base DMR-ID (7 digits) or callsign. Callsigns are resolved to DMR IDs at startup via the DMR ID database. If no users are configured, access is open (no password required). Users can be managed at runtime via the Admin interface.

**TG mapping**: Same semantics as MMDVMClient — first TG per module is primary (TX+RX), additional TGs are secondary (RX only). Dynamic TGs can be added via the Admin interface with configurable TTL.

**Hotspot config**: Point the hotspot directly at the reflector IP on port 62030. In WPSD/Pi-Star, use "MMDVM_BRIDGE" or single-network mode. DMRGateway multi-network mode routes traffic to BrandMeister by default and must be disabled.

**Admin commands**:
```json
{"cmd": "mmdvm_user_add", "token": "...", "dmrid": 2631353, "password": "secret"}
{"cmd": "mmdvm_user_add", "token": "...", "callsign": "DL4JC", "password": "secret"}
{"cmd": "mmdvm_user_remove", "token": "...", "dmrid": 2631353}
{"cmd": "mmdvm_user_list", "token": "..."}
{"cmd": "mmdvm_tg_add", "token": "...", "tg": 26363, "module": "F", "ts": 2, "ttl": 900}
{"cmd": "mmdvm_tg_remove", "token": "...", "tg": 26363}
{"cmd": "mmdvm_tg_list", "token": "..."}
{"cmd": "mmdvm_peer_list", "token": "..."}
```

### D-Star Client Connectors (DCS, DExtra, DPlus)
Connect to external D-Star reflectors as a client node. Maps remote reflector modules to local reflector modules. All three D-Star linking protocols are supported with their respective wire formats. Mappings can be configured statically in `urfd.ini` or added dynamically at runtime via the Admin interface.

```ini
[DCSClient]
Enable = true
Callsign = DL4JC                         # Must be whitelisted on target reflector
Map1 = dcs002.xreflector.net,30051,A,S   # DCS002 module A -> local module S
Map2 = dcs009.xreflector.net,30051,B,F   # DCS009 module B -> local module F
# BlockProtocols = MMDVMClient,YSF       # Optional: block audio from these protocols

[DExtraClient]
Enable = true
Callsign = DL4JC
Map1 = xrf757.openquad.net,30001,A,A     # XRF757 module A -> local module A

[DPlusClient]
Enable = true
Callsign = DL4JC                         # Must be registered at dstargateway.org
Map1 = ref001.dstargateway.org,20001,C,A # REF001 module C -> local module A
```

**Map format**: `host,port,remote_module,local_module` — each mapping creates one connection to the remote reflector on the specified module and routes audio to the local module.

**Callsign requirements**: DPlus reflectors require registration at [dstargateway.org](https://regist.dstargateway.org/). DExtra and DCS reflectors are typically open.

**Dynamic mappings**: Use the Admin dashboard "D-Star Client Mappings" section or the socket API:
```json
{"cmd": "dcs_map_add", "token": "...", "host": "dcs002.xreflector.net", "port": 30051, "remote_module": "A", "local_module": "S"}
{"cmd": "dcs_map_remove", "token": "...", "local_module": "S"}
{"cmd": "dcs_map_list", "token": "..."}
{"cmd": "dextra_map_add", "token": "...", "host": "xrf757.openquad.net", "port": 30001, "remote_module": "A", "local_module": "A"}
{"cmd": "dplus_map_add", "token": "...", "host": "ref001.dstargateway.org", "port": 20001, "remote_module": "A", "local_module": "Z"}
{"cmd": "reconnect", "token": "...", "protocol": "dcsclient"}
```

### YSF Client Connector
Connect to external YSF reflectors as a client node. Supports optional DG-ID for room selection on DG-ID-aware reflectors. Maps each connection to a local reflector module.

```ini
[YSFClient]
Enable = true
Callsign = DL4JC
Map1 = 90.187.72.177,42099,A             # YSF reflector -> local module A
Map2 = 85.215.138.68,42000,F,15          # YSF reflector -> local module F, DG-ID 15
# BlockProtocols = MMDVMClient            # Optional
```

**Map format**: `host,port,local_module[,dgid]` — DG-ID is optional (0 = disabled). When set, the DG-ID is included in the FICH header of all outbound voice packets.

**YSF Radio field**: Outbound voice packets carry the reflector callsign (e.g. `URF363`) in the YSF radio/description field (CSD1 bytes 0-9). This is displayed as the "Radio" column on YSF dashboards like ysf-deutschland.de.

**Dynamic mappings**:
```json
{"cmd": "ysf_map_add", "token": "...", "host": "90.187.72.177", "port": 42099, "local_module": "A", "dgid": 0}
{"cmd": "ysf_map_remove", "token": "...", "local_module": "A"}
{"cmd": "ysf_map_list", "token": "..."}
```

### NXDN RAN-Based Module Routing
Select reflector modules via NXDN RAN (Radio Access Number). RAN 1-26 maps directly to Module A-Z. RAN 0 maps to the AutoLinkModule (configurable fallback). Set the TX RAN on your NXDN radio to choose the target module — the client switches dynamically per transmission, like YSF DG-ID.

```ini
[NXDN]
Enable = true
Port = 41400
AutoLinkModule = S             # Module for RAN 0 (comment out to disable)
ReflectorID = 26363
# FallbackNxdnId = 8970        # NXDN ID for unknown callers (0 or omit = drop)
```

**RAN-to-Module mapping** (fixed, no configuration needed):
| RAN | Module | RAN | Module | RAN | Module |
|-----|--------|-----|--------|-----|--------|
| 1   | A      | 10  | J      | 19  | S      |
| 2   | B      | 11  | K      | 20  | T      |
| ...  | ...   | ... | ...    | 26  | Z      |

**NXDN ID Resolution**: Incoming NXDN IDs are resolved to callsigns via the NXDN ID database. If not found, the DMR ID database is used as fallback (many operators reuse their DMR ID as NXDN ID). This enables cross-protocol callsign display and D-Star slow data name lookup. Unknown IDs are dropped unless FallbackNxdnId is configured.

**D-Star Slow Data**: NXDN sources show `via NXDN RAN<n>` in the D-Star text field. Operator names are looked up from both DMR and NXDN databases.

**Dashboard**: The NXDN RAN column is shown in the module overview when NXDN is enabled. DMR+ and YSF DG-ID columns are hidden when their respective protocols are disabled.

### Dynamic Talkgroup Timer Behavior

Both MMDVM and SVX dynamic TGs have a 15-minute inactivity TTL. The timer is refreshed on any transmission start (DvHeader) on the module, regardless of which protocol originated the traffic. This means cross-protocol activity keeps all timers alive consistently.

**BrandMeister note**: BM maintains its own TG subscription timer independently. When the BM subscription expires, urfd's local mapping will also expire if there is no further activity. Use the **kerchunk** admin command to manually extend the BM-side subscription without needing a radio.

**SVX note**: When all dynamic SVX TGs expire, urfd sends `SELECT_TG(0)` to the SVX server to unsubscribe cleanly and stop receiving traffic for expired TGs.

### Admin Interface
Runtime management via a JSON-based TCP socket, accessible through the dashboard web panel. Runs in its own thread and does not block audio processing. Each client connection is handled in a separate detached thread with a 5-second idle timeout, so slow or stalled clients cannot block the admin socket.

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
- **D-Star Client Mappings**: Add/remove DCS, DExtra, DPlus reflector connections dynamically with module-to-module mapping. Protocol selector for DCS/DExtra/DPlus with auto-port defaults.
- **YSF Client Mappings**: Add/remove YSF reflector connections with optional DG-ID.
- **MMDVM Server Management**: Add/remove/list users (by DMR-ID or callsign) and TG mappings. View connected hotspot nodes with DMR-ID, frequencies, location, and software info.
- **Protocol Reconnect**: Force reconnect of MMDVM, SVX, DCSClient, DExtraClient, DPlusClient, or YSFClient connections.
- **Transcoder Statistics**: Connection status, active codec, packet counts, round-trip time per transcoded module.
- **Live Log Viewer**: Last 200 log lines with timestamps, auto-refreshing every 10 seconds.
- **Clear Last Heard**: Clears the last heard users list from the dashboard.
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
{"cmd": "dcs_map_add", "token": "...", "host": "dcs002.xreflector.net", "port": 30051, "remote_module": "A", "local_module": "S"}
{"cmd": "dcs_map_remove", "token": "...", "local_module": "S"}
{"cmd": "dcs_map_list", "token": "..."}
{"cmd": "ysf_map_add", "token": "...", "host": "90.187.72.177", "port": 42099, "local_module": "A", "dgid": 15}
{"cmd": "ysf_map_list", "token": "..."}
{"cmd": "svxserver_user_add", "token": "...", "callsign": "N0CALL-HS", "password": "secret"}
{"cmd": "svxserver_user_remove", "token": "...", "callsign": "N0CALL-HS"}
{"cmd": "svxserver_user_list", "token": "..."}
{"cmd": "mmdvm_user_add", "token": "...", "dmrid": 2631353, "password": "secret"}
{"cmd": "mmdvm_user_add", "token": "...", "callsign": "DL4JC", "password": "secret"}
{"cmd": "mmdvm_user_remove", "token": "...", "dmrid": 2631353}
{"cmd": "mmdvm_user_list", "token": "..."}
{"cmd": "mmdvm_tg_add", "token": "...", "tg": 26363, "module": "F", "ts": 2, "ttl": 900}
{"cmd": "mmdvm_tg_remove", "token": "...", "tg": 26363}
{"cmd": "mmdvm_tg_list", "token": "..."}
{"cmd": "mmdvm_peer_list", "token": "..."}
{"cmd": "clear_users", "token": "..."}
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
- **Module configuration**: description, linked node count, transcoded status, DMR+ TG ID, YSF DG-ID, NXDN RAN
- **Per-module mappings**: autolinks (YSF, NXDN, P25), TG mappings (MMDVMClient, SvxReflector), D-Star client mappings (DCSClient, DExtraClient, DPlusClient with connection status), YSF client mappings (YSFClient with DG-ID)
- **Enabled protocols**: name and port for each active protocol
- **Per-station protocol**: which protocol a user was heard on (DCS, MMDVMClient, YSF, etc.)
- **Dynamic TG indicators**: `<Dynamic>true</Dynamic>` and `<Remaining>` seconds for runtime-added TGs

Module names are configured once in `urfd.ini` and automatically available in the dashboard.

The JSON report also includes a `DynamicTGs` array with protocol, TG, module, and remaining seconds for all active dynamic mappings.

### Dashboard v2.6.0
Complete redesign with dark mode theme.

**New pages:**
- **Active Users** - Connected nodes per module in card layout
- **Overview Modules** - Module table with DMR+ IDs, YSF DG-IDs, NXDN RANs, mappings (static + dynamic), transcoder status, connected nodes. Protocol columns hidden when disabled.
- **Enabled Protocols** - Server-side protocols with ports and type classification (client protocols like MMDVMClient, SvxReflector, D-Star/YSF clients are hidden as they connect outbound)

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
Transcoded streams (DMR, YSF, SVX, M17, P25, USRP, client protocols -> D-Star) now include proper D-Star slow data:
- **Header**: Caller callsign (MY), reflector callsign (RPT1/RPT2)
- **Text message**: Source protocol and TG/DG-ID/RAN/module info
- **Operator name**: If the caller's callsign is found in the DMR or NXDN ID database, the operator's name is shown alternating with the protocol/TG info (~5 seconds each)

Examples of text messages per protocol:

| Source | Slow Data Text |
|--------|---------------|
| MMDVMClient | `via DMR TG26363` |
| SvxReflector | `via SVX TG317424` |
| YSF (server) | `via YSF DG28` |
| NXDN | `via NXDN RAN6` |
| DCSClient | `via DCS002 A` |
| DExtraClient | `via XRF757 A` |
| DPlusClient | `via REF001 C` |
| YSFClient | `via YSF DG15` |
| M17, P25, USRP, DMR+ | `via M17`, `via P25`, etc. |

D-Star client protocols extract the remote reflector name from the hostname (e.g. `dcs002.xreflector.net` → `DCS002`) and append the remote module letter. YSFClient appends the DG-ID if configured (otherwise just `via YSF`).

Header and text message alternate every superframe (~420ms). When an operator name is available, the text message cycles between protocol/TG info and name every ~5 seconds:

```
[Header: DL4JC > URF363 S] → [via DCS002 A]   → [Header] → [via DCS002 A]   → ...
                    (after ~5s)
[Header: DL4JC > URF363 S] → [Jens-Christian]  → [Header] → [Jens-Christian]  → ...
                    (after ~5s, back to via/info)
```

Name lookup works for all protocols — any callsign with a DMR ID database entry will have its name displayed on D-Star radios.

## Introduction

The URF Multi-protocol Gateway Reflector Server, **urfd**, is part of the software system for a Digital Voice Network. It supports D-Star (DPlus, DCS, DExtra, G3), DMR (MMDVM, DMR+, MMDVMClient), M17, YSF, P25, NXDN, USRP (AllStar), SvxReflector (SvxLink FM client) and SVXServer (SvxLink FM server).

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

## Administration Tools

### radmin

Interactive and non-interactive administration tool. Auto-detects Docker vs systemd deployment.

```bash
# Interactive menu
./radmin

# Single commands
./radmin status          # Show deployment status + git log
./radmin restart         # Restart container or service
./radmin stop            # Stop container or service
./radmin start           # Start container or service
./radmin logs            # Follow logs (Ctrl-C to exit)
./radmin reload          # Send SIGHUP (hot-reload config)
./radmin check           # Run inicheck (quiet mode)
./radmin build           # Run build.sh (Docker) or make (bare metal)

# With custom ini path
./radmin /path/to/urfd.ini
```

Detection order: Docker container `urfd` running → Docker container stopped → systemd service → not installed.

### inicheck

Validates `urfd.ini` syntax and cross-references (e.g. TG modules must exist in `[Modules]`, transcoded protocols need a transcoder, Echo module must be a configured module).

```bash
cd reflector
make inicheck
./inicheck -q urfd.ini   # Errors and warnings only
./inicheck -n urfd.ini   # Also print public config keys
./inicheck -v urfd.ini   # Print all keys (including internal)
```

Exit code 0 = OK, 1 = errors found. Warnings (missing optional fields) don't cause a non-zero exit.

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
| 5300/tcp+udp | SvxReflector | SvxLink (outgoing client) |
| 5300/tcp+udp | SVXServer | SvxLink (incoming server) |
| 62030/udp | MMDVM | DMR MMDVM |

## YSF

URF acts as a YSF Master providing Wires-X rooms (one per module). YSF users connect via hotspot software (Pi-Star/WPSD) which discovers URF reflectors through the XLX API. No registration at ysfreflector.de is needed. If `EnableDGID = true`, users can switch modules via DG-ID values (10=A, 11=B, ... 35=Z).

## Changes from upstream

### Protocol Connectors
- **MMDVMClient**: Full MMDVM protocol client for BrandMeister/DMR+ masters — multi-TG per module (primary TX/RX + secondary RX-only), per-timeslot support, fallback DMR ID, options string generation
- **BrandMeister API**: Optional REST API v2 integration — startup sync preserves dynamic TGs (only removes TGs not in config or runtime map), API-based TG add/remove instead of kerchunk, configurable via `BrandMeisterApiKey`
- **SvxReflector**: TCP/UDP client for SvxLink FM servers — OPUS codec, bidirectional audio bridging, configurable RX/TX gain, SELECT_TG protocol, auto-disconnect on TCP send failure with immediate reconnect
- **SVXServer** (beta): SvxReflector V2 server — accept incoming SvxLink node connections with HMAC-SHA1 auth, OPUS audio, TG-based module routing, per-user authentication, runtime user/TG management via admin API
- **DCS interlinking**: Native DCS reflector-to-reflector peering with PeerCallsign override and protocol field in interlink file
- **D-Star client protocols**: DCSClient, DExtraClient, DPlusClient — connect to external D-Star reflectors as a client node with module-to-module mapping, dynamic add/remove via admin API, protocol blocking, dashboard integration
- **YSF client protocol**: YSFClient — connect to external YSF reflectors with optional DG-ID, dynamic mapping via admin API, reflector callsign in YSF radio field

### Runtime Management
- **Admin interface**: JSON-over-TCP socket + web dashboard — dynamic TG management, protocol reconnect, transcoder stats, live log viewer, runtime protocol blocking, per-client threading with 5s idle timeout
- **Runtime protocol blocking**: Bidirectional block/unblock of any protocol pair via admin dashboard — thread-safe with shared_mutex, clickable labels to remove, reset-to-config-default button, not persisted (resets on restart)
- **Dynamic TG management**: Add/remove TGs at runtime with configurable TTL, cross-protocol timer refresh, BM API or kerchunk activation, SELECT_TG(0) cleanup on SVX expiry
- **SIGHUP config reload**: Hot-reload TG mappings, whitelist/blacklist/interlink, transcoder modules without dropping sessions — thread-safe via sigwait, exception-safe with rollback

### Audio & Transcoding
- **NXDN RAN routing**: Module selection via RAN (Radio Access Number) — RAN 1-26 = Module A-Z, RAN 0 = AutoLinkModule. Client switches module per transmission (like YSF DG-ID). NXDN ID resolution via NXDN DB with DMR DB fallback. FallbackNxdnId for unknown callers (drop if unset). Fixed uninitialized `m_uiNXDNid` in CCallsign default constructor.
- **D-Star slow data**: Transcoded streams include caller callsign, protocol/TG/RAN/module info, operator name from DMR and NXDN ID databases — rotating every ~5 seconds. TG display uses actual source TG (not primary/static) for correct multi-TG display. Client protocols show remote reflector name + module (e.g. `via DCS002 A`), YSFClient shows DG-ID
- **SVX/USRP codec separation**: Independent codec paths (`ECodecType::svx` vs `ECodecType::usrp`) with separate gain control — SVX gain in urfd.ini, USRP gain in tcd.ini
- **MMDVM late-entry**: Resolves DMR ID from active stream callsign (prefers cached ID from source protocol over DB lookup) — enables mid-stream block removal
- **Self-echo prevention**: MMDVMClient blocks self-routing back to BrandMeister

### Dashboard & Output
- **Protocol naming**: Consistent UI names — `MMDVM` = direct hotspot connections, `MMDVMClient` = BrandMeister/master server connection (previously both showed as "MMDVM" in different contexts)
- **Dashboard v2.6.0**: Dark mode redesign, admin panel, module overview with TG mappings and NXDN RAN, protocol-specific columns hidden when disabled, protocol list, QuadNet Live proxy, reflector list with search/pagination
- **Extended XML/JSON**: Module mappings (static + dynamic with TTL), reflector metadata, protocol list, per-user protocol info, dynamic TG array
- **Protocol blocking display**: Active blocks shown as clickable labels in admin panel

### Infrastructure
- **Echo module**: Built-in parrot with enable/disable flag
- **M17 LSTN support**: Listen-only M17 clients for monitoring services
- **Transcoder resilience**: TCP keepalive, non-blocking poll, queue drain on disconnect, automatic reconnect after network outages
- **Database HTTP retry**: Retry with backoff on failed initial load (prevents empty databases after transient errors)
- **Service optimizations**: Realtime scheduling and voice CPU priority
- **Thread-safe logging**: Atomic cout via ostringstream
- **Thread-safe DB lookups**: DMR/NXDN ID database lookups in CodecStream hold proper mutex locks during access, preventing race conditions with background DB refresh
- **P25 cleanup**: Removed dead `P25_MODULE_ID` hardcode, P25 now uses `AutoLinkModule` config consistently. Guard debug dumps.
- **YSF fixes**: WiresX packet check moved before voice parser (CONN_REQ data frames were consumed as voice). CONN_REQ accepts room IDs 4001-4026 and direct index 1-26 (was forced to 0). DX_RESP status `'3'` (YSFGateway compat, was undocumented `'5'`). ALL_RESP byte `0x26` (was `0x29`). Fixed `YSFPayload::getSource()` null pointer (checked wrong member). Note: WiresX room switching requires direct YSF clients; MMDVMHost intercepts CONN_REQ locally. DG-ID is the primary module selection method.
- **DCS server fix**: Ignore "EEEE" status packets (35 bytes) from ircDDBGateway clients instead of logging as unknown. Accept 9-byte keepalive variant.
- **Cherry-picked from [dbehnke/urfd](https://github.com/dbehnke/urfd)**: Callsign sanitization, YSF radio ID collision fix, transcoder module ID enforcement, DG-ID module selection

## Copyright

- Copyright (c) 2016 Jean-Luc Deltombe LX3JL and Luc Engelmann LX1IQ
- Copyright (c) 2022 Doug McLain AD8DP and Thomas A. Early N7TAE
- Copyright (c) 2024 Thomas A. Early N7TAE
- Copyright (c) 2025-2026 Jens-Christian Merg DL4JC (URF363 fork extensions)
