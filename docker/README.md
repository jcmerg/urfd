# urfd Docker Deployment

Docker deployment for the urfd reflector.

The tcd transcoder runs natively on a separate host — Docker adds too much USB latency for reliable DVSI hardware access.

## Quick Start

```bash
# Build and start
docker/build.sh start

# Or manually
docker compose -f docker-compose.yml build
docker compose -f docker-compose.yml up -d
```

## Configuration

Mount your config directory to `/usr/local/etc/urfd`. Must contain at least `urfd.ini`. See the main [urfd documentation](../docs/) for details.

## Files

| File | Purpose |
|---|---|
| `Dockerfile` | Multi-stage build (Ubuntu 24.04) |
| `docker-compose.yml` | Production compose file |
| `build.sh` | Build + restart helper |
| `supervisord.conf` | Process manager (urfd, nginx, php-fpm) |
| `nginx-dashboard.conf` | Dashboard web server config |

## Synology NAS

The Synology Container Manager cannot build images locally. Build on a Linux machine and export:

```bash
docker build -t urfd:latest -f docker/Dockerfile .
docker save urfd:latest | gzip > urfd-image.tar.gz
```

Import on the NAS via Container Manager > Image > Import, or via SSH:

```bash
docker load < urfd-image.tar.gz
```

Adjust volume paths for NAS (e.g. `/volume1/docker/urfd/config:/usr/local/etc/urfd`).
