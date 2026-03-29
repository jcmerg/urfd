# tcd Docker Container

Docker container for the tcd hybrid transcoder using the md380 software vocoder (via [dynarmic](https://github.com/jcmerg/md380_vocoder_dynarmic)) for AMBE2 on x86_64. Only one DVSI hardware device (AMBE3003 or AMBE3000) is needed — the second vocoder runs in software.

## Features

- Multi-stage build: md380_vocoder_dynarmic, imbe_vocoder, libftd2xx, tcd
- Software AMBE2 vocoder via dynarmic (ARM JIT on x86_64) — no second DVSI chip needed
- Selective USB unbinding: only AMBE devices are detached from `ftdi_sio`, other FTDI devices (USB serial converters) keep working
- AMBE3003: up to 3 transcoded modules / AMBE3000: 1 module

## Quick Start (Linux host)

```bash
cd docker

# Create your config from the example
cp tcd.ini.example tcd.ini
# Edit tcd.ini: set Modules and ServerAddress to match your urfd
vi tcd.ini

# Build and start
docker compose build
docker compose up -d

# Check logs
docker compose logs -f
```

## Synology NAS Deployment

The Synology Container Manager cannot build images locally. Build the image on another machine and export it, then import on the NAS.

### 1. Build and export the image (on a Linux machine)

```bash
cd docker
docker compose build
docker save tcd:latest | gzip > tcd-image.tar.gz
```

### 2. Import the image on the NAS

Copy `tcd-image.tar.gz` to the NAS (e.g. via File Station or SCP), then via SSH:

```bash
docker load < /path/to/tcd-image.tar.gz
```

Or use Container Manager > Image > Import.

### 3. Create the config

```bash
sudo mkdir -p /volume1/docker/tcd
sudo cp tcd.ini.example /volume1/docker/tcd/tcd.ini
sudo vi /volume1/docker/tcd/tcd.ini
```

Edit `Modules` and `ServerAddress` to match your urfd configuration.

### 4. Start the container

In Container Manager, create a new project and use `docker-compose.nas.yml` as the compose file. Or via SSH:

```bash
docker compose -f docker-compose.nas.yml up -d
```

### 5. Check logs

```bash
docker compose -f docker-compose.nas.yml logs -f
```

## Configuration

The `tcd.ini` must match the `[Transcoder]` section of your `urfd.ini`:

| Parameter | Description |
|---|---|
| `Port` | TCP port (must match urfd, default: 10100) |
| `ServerAddress` | IP of the urfd reflector |
| `Modules` | Transcoded modules (must match urfd, e.g. `A` or `ADM`) |
| `DStarGainIn/Out` | D-Star vocoder gain in dB |
| `DmrYsfGainIn/Out` | DMR/YSF vocoder gain in dB |
| `UsrpTxGain/RxGain` | PCM gain in dB (applies to USRP and SvxReflector) |

## CPU Priority

The compose files set elevated CPU priority for realtime vocoding:

- `cpu_shares: 1024` — double the default priority (512), ensures tcd gets preferred CPU time under load
- `ulimits.rtprio: 99` — allows realtime scheduling inside the container
- `ulimits.nice: -20` — allows highest nice priority

This is important on shared systems like a NAS where DSM and other services compete for CPU.

## USB Device Handling

The container **automatically** detaches AMBE DVSI devices from the Linux `ftdi_sio` kernel driver at startup via `entrypoint.sh`. No manual steps required. Other FTDI devices (USB serial converters etc.) remain unaffected and keep working as `/dev/ttyUSBx`.

There is **no need to unload kernel modules** (`rmmod ftdi_sio`) — only the specific AMBE device is unbound.

Supported DVSI devices:
- ThumbDV / DVstick-30 / USB-3000 (AMBE3000, 1 channel)
- USB-3003 / USB-3012 / DF2ET-3003 (AMBE3003, 3 channels)

### Optional: udev rule (not required)

If you prefer to unbind the AMBE device at the host level (e.g. automatically on plug-in, before the container starts), you can optionally install a udev rule. This is **not needed** for normal operation — the container handles it.

```bash
sudo cp 99-ambe-unbind.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

## Files

| File | Description |
|---|---|
| `Dockerfile` | Multi-stage build (md380_vocoder, imbe_vocoder, libftd2xx, tcd) |
| `docker-compose.yml` | Default compose with build support (Linux host) |
| `docker-compose.nas.yml` | Synology NAS compose (pre-built image, absolute paths) |
| `tcd.ini.example` | Example configuration |
| `entrypoint.sh` | Startup script with selective AMBE USB unbinding |
| `99-ambe-unbind.rules` | Optional udev rule for host-level USB unbinding |
