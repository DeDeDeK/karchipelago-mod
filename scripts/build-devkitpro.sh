#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKERFILE="$SCRIPT_DIR/DevKitPro.dockerfile"
IMAGE_NAME="devkitpro-builder"
OUTPUT_DIR="$SCRIPT_DIR/devkitpro"

echo "Building DevKitPro Docker image..."
podman build -f "$DOCKERFILE" -t "$IMAGE_NAME" "$SCRIPT_DIR"

echo "Creating temporary container..."
CONTAINER_ID=$(podman create "$IMAGE_NAME")

echo "Copying /opt/devkitpro from container to $OUTPUT_DIR..."
podman cp "$CONTAINER_ID:/opt/devkitpro" "$OUTPUT_DIR"

echo "Cleaning up container..."
podman rm "$CONTAINER_ID"

echo "Done! DevKitPro installed to $OUTPUT_DIR"
echo "You may want to set these environment variables:"
echo "  export DEVKITPRO=$OUTPUT_DIR"
echo "  export DEVKITARM=$OUTPUT_DIR/devkitARM"
echo "  export DEVKITPPC=$OUTPUT_DIR/devkitPPC"
