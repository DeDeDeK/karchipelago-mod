#!/bin/bash
# Build the devkitPPC toolchain into externals/devkitpro, where the Makefile
# expects it (DEVKITPRO ?= $(CURDIR)/externals/devkitpro).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE_NAME="devkitpro-builder"
OUTPUT_DIR="$ROOT/externals/devkitpro"

if [ -e "$OUTPUT_DIR" ]; then
    echo "Error: $OUTPUT_DIR already exists. Remove it first." >&2
    exit 1
fi

echo "Building DevKitPro image..."
podman build -f "$SCRIPT_DIR/Dockerfile" -t "$IMAGE_NAME" "$SCRIPT_DIR"

CONTAINER_ID=$(podman create "$IMAGE_NAME")
trap 'podman rm "$CONTAINER_ID" >/dev/null' EXIT

echo "Copying /opt/devkitpro to $OUTPUT_DIR..."
mkdir -p "$(dirname "$OUTPUT_DIR")"
podman cp "$CONTAINER_ID:/opt/devkitpro" "$OUTPUT_DIR"

echo "Done. devkitPPC installed to $OUTPUT_DIR"
