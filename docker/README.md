# urfd Docker Deployment

Docker deployment for the urfd reflector, with optional integrated tcd transcoder.

## Deployment Options

### Option 1: Combined (urfd + tcd on the same host)

Both services run on one machine. tcd waits for urfd to be healthy before starting. Set `ServerAddress = 127.0.0.1` in `tcd.ini`.

```bash
# Linux host
docker compose -f docker-compose.combined.yml build
docker compose -f docker-compose.combined.yml up -d

# Synology NAS (pre-built images)
docker compose -f docker-compose.combined.nas.yml up -d
```

### Option 2: Separate hosts (urfd and tcd on different machines)

**On the urfd host:**

```bash
# Linux
docker compose -f docker-compose.urfd-only.yml build
docker compose -f docker-compose.urfd-only.yml up -d

# Synology NAS
docker compose -f docker-compose.urfd-only.nas.yml up -d
```

**On the tcd host:**

Set `ServerAddress` in `tcd.ini` to the IP of the urfd host.

```bash
# Linux
docker compose -f docker-compose.tcd-only.yml build
docker compose -f docker-compose.tcd-only.yml up -d

# Synology NAS
docker compose -f docker-compose.tcd-only.nas.yml up -d
```

## Compose Files

| File | urfd | tcd | Build | Target |
|---|---|---|---|---|
| `docker-compose.combined.yml` | yes | yes | yes | Linux host |
| `docker-compose.combined.nas.yml` | yes | yes | no | Synology NAS |
| `docker-compose.urfd-only.yml` | yes | - | yes | Linux host |
| `docker-compose.urfd-only.nas.yml` | yes | - | no | Synology NAS |
| `docker-compose.tcd-only.yml` | - | yes | yes | Linux host |
| `docker-compose.tcd-only.nas.yml` | - | yes | no | Synology NAS |

## Building images for NAS deployment

The Synology Container Manager cannot build images locally. Build on a Linux machine and export:

```bash
# Build both images
docker compose -f docker-compose.combined.yml build

# Export
docker save urfd:latest | gzip > urfd-image.tar.gz
docker save tcd:latest | gzip > tcd-image.tar.gz
```

Import on the NAS via Container Manager > Image > Import, or via SSH:

```bash
docker load < urfd-image.tar.gz
docker load < tcd-image.tar.gz
```

## Configuration

### urfd

Mount your config directory to `/usr/local/etc/urfd`. Must contain at least `urfd.ini`. See the main [urfd documentation](../docs/) for details.

### tcd

Mount `tcd.ini` to `/etc/tcd/tcd.ini`. The `Modules` setting **must match** the `[Transcoder] Modules` in `urfd.ini`.

| Scenario | ServerAddress in tcd.ini |
|---|---|
| Combined (same host) | `127.0.0.1` |
| Separate hosts | IP of the urfd host |

## USB Devices (tcd)

The tcd container automatically detaches AMBE DVSI devices from the `ftdi_sio` kernel driver at startup. Other FTDI USB devices (serial converters) are not affected — no need to unload kernel modules.

Supported hardware:
- AMBE3003 (USB-3003 / USB-3012): up to 3 transcoded modules
- AMBE3000 (ThumbDV / DVstick-30): 1 transcoded module

The tcd container uses the [md380_vocoder_dynarmic](https://github.com/jcmerg/md380_vocoder_dynarmic) software vocoder for AMBE2, so only one DVSI hardware device is needed.

See [docker-tcd/README.md](../docker-tcd/README.md) for tcd-specific details.
