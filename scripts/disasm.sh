#!/bin/bash
# Disassemble a function from mem1.raw.
#
# Usage:
#   ./scripts/disasm.sh <address> [length]     - by address (length auto-looked up if omitted)
#   ./scripts/disasm.sh <symbol_name>          - by symbol name from GKYE01.map
#
# Examples:
#   ./scripts/disasm.sh 0x80007AF0 0x120
#   ./scripts/disasm.sh 0x80007AF0              # auto-lookup size from map
#   ./scripts/disasm.sh Gm_GetGameData          # lookup address and size from map

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MEM1="$SCRIPT_DIR/mem1.raw"
MAP="$SCRIPT_DIR/../externals/hoshi/GKYE01.map"

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "Usage: $0 <address|symbol_name> [length]"
    echo "Examples:"
    echo "  $0 0x80007AF0 0x120"
    echo "  $0 0x80007AF0          # auto-lookup size from map"
    echo "  $0 Gm_GetGameData      # lookup address and size from map"
    exit 1
fi

ARG1=$1

# Determine if the first argument is an address (starts with 0x/0X or is all hex digits) or a symbol name
if [[ "$ARG1" =~ ^0[xX][0-9a-fA-F]+$ ]] || [[ "$ARG1" =~ ^[0-9a-fA-F]{8}$ ]]; then
    # Argument is an address
    ADDR=$ARG1
    if [ $# -eq 2 ]; then
        LEN=$2
    else
        # Look up size from map by address
        ADDR_LOWER=$(echo "$ADDR" | sed 's/0[xX]//' | tr 'A-F' 'a-f')
        MAP_LINE=$(grep -i "^${ADDR_LOWER} " "$MAP" | head -1)
        if [ -z "$MAP_LINE" ]; then
            echo "Error: address $ADDR not found in map. Provide length manually." >&2
            exit 1
        fi
        LEN="0x$(echo "$MAP_LINE" | awk '{print $2}')"
        NAME=$(echo "$MAP_LINE" | awk '{print $5}')
        echo "Found: $NAME (size $LEN)" >&2
    fi
else
    # Argument is a symbol name
    MAP_LINE=$(grep -E " ${ARG1}\s*$" "$MAP" | head -1)
    if [ -z "$MAP_LINE" ]; then
        echo "Error: symbol '$ARG1' not found in map." >&2
        exit 1
    fi
    ADDR="0x$(echo "$MAP_LINE" | awk '{print $1}')"
    LEN="0x$(echo "$MAP_LINE" | awk '{print $2}')"
    echo "Found: $ARG1 at $ADDR (size $LEN)" >&2

    # Allow length override even with symbol name
    if [ $# -eq 2 ]; then
        LEN=$2
    fi
fi

OFFSET=$(( $ADDR - 0x80000000 ))
TMP=$(mktemp /tmp/disasm.XXXXXX.bin)
trap "rm -f $TMP" EXIT

dd if="$MEM1" iflag=skip_bytes skip=$OFFSET bs=$(( LEN )) count=1 of="$TMP" 2>/dev/null
$SCRIPT_DIR/../externals/devkitpro/devkitPPC/bin/powerpc-eabi-objdump -D -b binary -m powerpc -EB --adjust-vma=$ADDR "$TMP"
