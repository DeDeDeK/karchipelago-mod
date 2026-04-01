#!/bin/bash
# Search mem1.raw for all 4-byte-aligned occurrences of a 32-bit value.
#
# Usage:
#   ./scripts/findptr.sh <value> [region_start] [region_end]
#
# Examples:
#   ./scripts/findptr.sh 0x80412AB0                           # search all of mem1
#   ./scripts/findptr.sh 0x80412AB0 0x80000000 0x80600000     # search a region
#   ./scripts/findptr.sh 0x80412AB0 0x80530000 0x80540000     # narrow search
#
# Prints each match as:  <game_address>  <context>
# where context shows the surrounding words for quick vtable/table identification.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MEM1="$SCRIPT_DIR/mem1.raw"
MAP="$SCRIPT_DIR/../externals/hoshi/GKYE01.map"
MEM1_BASE=0x80000000

if [ $# -lt 1 ] || [ $# -gt 3 ]; then
    echo "Usage: $0 <value> [region_start] [region_end]"
    echo ""
    echo "Search mem1.raw for all 4-byte-aligned occurrences of a 32-bit value."
    echo ""
    echo "Examples:"
    echo "  $0 0x80412AB0                           # search all of mem1"
    echo "  $0 0x80412AB0 0x80000000 0x80600000     # search a region"
    exit 1
fi

VALUE=$(printf '%08x' $(( $1 )) )

MEM1_SIZE=$(stat -c%s "$MEM1")

if [ $# -ge 2 ]; then
    REGION_START=$(( $2 ))
else
    REGION_START=$(( MEM1_BASE ))
fi

if [ $# -ge 3 ]; then
    REGION_END=$(( $3 ))
else
    REGION_END=$(( MEM1_BASE + MEM1_SIZE ))
fi

FILE_START=$(( REGION_START - MEM1_BASE ))
FILE_END=$(( REGION_END - MEM1_BASE ))

if [ $FILE_START -lt 0 ] || [ $FILE_END -gt $MEM1_SIZE ]; then
    echo "Error: region out of bounds for mem1.raw (size $MEM1_SIZE)" >&2
    exit 1
fi

SEARCH_LEN=$(( FILE_END - FILE_START ))

# Extract the search region and find all matches
# Use xxd to convert to hex, then search for the value at 4-byte-aligned positions
BYTE1="${VALUE:0:2}"
BYTE2="${VALUE:2:2}"
BYTE3="${VALUE:4:2}"
BYTE4="${VALUE:6:2}"

echo "Searching for 0x$VALUE in 0x$(printf '%08x' $REGION_START)..0x$(printf '%08x' $REGION_END)" >&2

# Use Python for efficient binary search
python3 -c "
import struct, sys, os

value = bytes.fromhex('$VALUE')
mem1_base = $MEM1_BASE
file_start = $FILE_START
file_end = $FILE_END
map_path = '$MAP'

# Load symbol map for context
symbols = []
try:
    with open(map_path, 'r') as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 5:
                try:
                    addr = int(parts[0], 16)
                    size = int(parts[1], 16)
                    name = parts[4]
                    symbols.append((addr, size, name))
                except ValueError:
                    pass
    symbols.sort()
except:
    pass

def find_symbol(addr):
    lo, hi = 0, len(symbols) - 1
    result = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if symbols[mid][0] <= addr:
            result = mid
            lo = mid + 1
        else:
            hi = mid - 1
    if result is not None:
        sym_addr, sym_size, sym_name = symbols[result]
        if addr < sym_addr + sym_size:
            offset = addr - sym_addr
            if offset == 0:
                return sym_name
            return f'{sym_name}+0x{offset:x}'
    return None

with open('$MEM1', 'rb') as f:
    f.seek(file_start)
    data = f.read(file_end - file_start)

count = 0
for i in range(0, len(data) - 3, 4):
    if data[i:i+4] == value:
        game_addr = mem1_base + file_start + i
        sym = find_symbol(game_addr)
        loc = f'0x{game_addr:08x}'
        if sym:
            loc += f'  ({sym})'

        # Show context: 2 words before and 2 words after
        ctx_parts = []
        for j in range(-8, 12, 4):
            ci = i + j
            if 0 <= ci <= len(data) - 4:
                w = struct.unpack('>I', data[ci:ci+4])[0]
                if j == 0:
                    ctx_parts.append(f'[{w:08X}]')
                else:
                    ctx_parts.append(f'{w:08X}')
            else:
                ctx_parts.append('........')
        ctx = ' '.join(ctx_parts)

        print(f'{loc}  {ctx}')
        count += 1

print(f'Found {count} match(es).', file=sys.stderr)
"
