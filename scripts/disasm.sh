#!/bin/bash
# Disassemble a function from mem1.raw by GameCube address and length.
# Usage: ./scripts/disasm.sh <address> <length>
# Example: ./scripts/disasm.sh 0x80007AF0 0x120

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <address> <length>"
    echo "Example: $0 0x80007AF0 0x120"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MEM1="$SCRIPT_DIR/../docs/mem1.raw"
ADDR=$1
LEN=$2

OFFSET=$(( $ADDR - 0x80000000 ))
TMP=$(mktemp /tmp/disasm.XXXXXX.bin)
trap "rm -f $TMP" EXIT

dd if="$MEM1" iflag=skip_bytes skip=$OFFSET bs=$(( LEN )) count=1 of="$TMP" 2>/dev/null
$SCRIPT_DIR/../externals/devkitpro/devkitPPC/bin/powerpc-eabi-objdump -D -b binary -m powerpc -EB --adjust-vma=$ADDR "$TMP"
