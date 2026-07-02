#!/usr/bin/env bash
# Import hoshi's struct/enum/typedef definitions into the Ghidra program's
# DataTypeManager (phase 1 of the type-import workflow; phase 2 = apply_protos.py).
#
# Ghidra's C parser aborts on function bodies, but the hoshi headers mix type
# declarations with inline/static function bodies. So we mirror the headers with
# bodies stripped (strip_bodies.py), shadow the system/detour includes with empty
# stubs (stubs/), and feed the parser a master header (hoshi_all.h) via the
# ghidra-cli bridge script ParseHoshiHeaders.java.
#
# Changes are in-memory only until you run `ghidra analyze` (this script reminds
# you at the end). Re-running into an already-populated DTM risks `.conflict`
# types -- run against a clean/baseline program.
#
# Linux only (uses /proc to find the bridge's working directory).
#
# Usage: scripts/ghidra/import_types.sh [--project NAME]
set -euo pipefail

PROJECT=kar-decomp
while [ $# -gt 0 ]; do
  case "$1" in
    --project) PROJECT="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
HOSHI_INCLUDE="$REPO/externals/hoshi/include"
CFG_DIR="$HOME/.config/ghidra-cli"
SCRIPTS_DIR="$CFG_DIR/scripts"

[ -d "$HOSHI_INCLUDE" ] || { echo "missing $HOSHI_INCLUDE" >&2; exit 1; }

STRIPPED="$(mktemp -d)"
REPORT="$(mktemp)"
trap 'rm -rf "$STRIPPED"' EXIT

echo "[1/6] stripping function bodies -> $STRIPPED"
( cd "$REPO" && uv run python "$HERE/strip_bodies.py" "$HOSHI_INCLUDE" "$STRIPPED" )

echo "[2/6] writing parser config -> $CFG_DIR/parse_hoshi.cfg"
mkdir -p "$SCRIPTS_DIR"
cat > "$CFG_DIR/parse_hoshi.cfg" <<EOF
report=$REPORT
includes=$HERE/stubs,$STRIPPED
files=$HERE/hoshi_all.h
EOF

echo "[3/6] installing ParseHoshiHeaders.java into ghidra-cli scripts dir"
cp "$HERE/ParseHoshiHeaders.java" "$SCRIPTS_DIR/ParseHoshiHeaders.java"

echo "[4/6] ensuring bridge is running"
ghidra start --project "$PROJECT" >/dev/null 2>&1 || true

# The bridge validates the script path with new File(path).exists() relative to
# its own CWD, but runScript resolves by bare filename against source dirs. So we
# drop a copy at the bridge CWD and invoke by bare name.
PIDFILE="$(ls "$HOME/.local/share/ghidra-cli/"bridge-*.pid 2>/dev/null | head -1 || true)"
if [ -n "$PIDFILE" ] && [ -r "$PIDFILE" ]; then
  BPID="$(cat "$PIDFILE")"
else
  BPID="$(pgrep -f GhidraCliBridge | head -1 || true)"
fi
[ -n "$BPID" ] || { echo "could not find bridge PID" >&2; exit 1; }
BCWD="$(readlink "/proc/$BPID/cwd")"
echo "[5/6] running parser (bridge pid=$BPID cwd=$BCWD)"
cp "$HERE/ParseHoshiHeaders.java" "$BCWD/ParseHoshiHeaders.java"
trap 'rm -rf "$STRIPPED"; rm -f "$BCWD/ParseHoshiHeaders.java"' EXIT
ghidra script run ParseHoshiHeaders.java --project "$PROJECT" || true
rm -f "$BCWD/ParseHoshiHeaders.java"

echo "[6/6] parse report:"
sed -n '1,40p' "$REPORT" 2>/dev/null || echo "(no report written)"

cat <<EOF

Types are now in the in-memory program. To persist to disk and then apply the
documented function signatures:

    ghidra analyze --project $PROJECT                       # save types
    uv run python $HERE/apply_protos.py --project $PROJECT  # apply signatures
    ghidra analyze --project $PROJECT                       # save signatures
EOF
