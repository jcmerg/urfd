#!/bin/bash
set -e

OPT_DIR="/opt/urfd"
URFD_SRC="$OPT_DIR/src"

echo "=== Building urfd Docker image ==="
echo "Source: $URFD_SRC"
echo "Config: $OPT_DIR"

docker build \
    -t urfd:latest \
    -f "$OPT_DIR/Dockerfile" \
    --build-context optfiles="$OPT_DIR" \
    "$URFD_SRC"

echo ""
echo "=== Build complete ==="

# Restart container if running
if docker ps -q -f name=urfd | grep -q .; then
    echo "=== Restarting urfd container ==="
    cd "$OPT_DIR"
    docker compose down
    docker compose up -d
    echo "=== Container restarted ==="
elif [ "$1" = "start" ]; then
    echo "=== Starting urfd container ==="
    cd "$OPT_DIR"
    docker compose up -d
    echo "=== Container started ==="
else
    echo ""
    echo "Container not running. Run '$0 start' or 'cd $OPT_DIR && docker compose up -d' to start."
fi

echo ""
docker ps -f name=urfd
