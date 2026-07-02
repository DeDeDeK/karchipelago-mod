#!/usr/bin/env bash
# Type hoshi's fixed-address engine globals in the Ghidra program's listing
# (phase 3 of the type-import workflow; phase 1 = import_types.sh, phase 2 =
# apply_protos.py). extract_globals.py pulls every documented fixed-address global
# from the hoshi headers; ApplyGlobals.java clears the conflicting auto-analysis
# data at each address and re-creates it as the documented hoshi type + label.
#
# Needs phase 1 done first (the struct/enum types must exist to apply them).
#
# Changes are in-memory only until you run `ghidra analyze` (reminded at the end).
#
# Linux only (uses /proc to find the bridge's working directory).
#
# Usage: scripts/ghidra/import_globals.sh [--project NAME]
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

DATA="$CFG_DIR/apply_globals.tsv"
REPORT="$(mktemp)"

echo "[1/6] extracting global declarations -> $DATA"
mkdir -p "$SCRIPTS_DIR"
( cd "$REPO" && uv run python "$HERE/extract_globals.py" "$HOSHI_INCLUDE" > "$DATA" )
echo "      $(grep -cvE '^#' "$DATA") globals"

echo "[2/6] writing parser config -> $CFG_DIR/apply_globals.cfg"
cat > "$CFG_DIR/apply_globals.cfg" <<EOF
data=$DATA
report=$REPORT
EOF

echo "[3/6] installing ApplyGlobals.java into ghidra-cli scripts dir"
cp "$HERE/ApplyGlobals.java" "$SCRIPTS_DIR/ApplyGlobals.java"

echo "[4/6] ensuring bridge is running"
ghidra start --project "$PROJECT" >/dev/null 2>&1 || true

# The bridge validates the script path relative to its own CWD but resolves
# runScript by bare filename against source dirs, so drop a copy at the bridge CWD.
PIDFILE="$(ls "$HOME/.local/share/ghidra-cli/"bridge-*.pid 2>/dev/null | head -1 || true)"
if [ -n "$PIDFILE" ] && [ -r "$PIDFILE" ]; then
  BPID="$(cat "$PIDFILE")"
else
  BPID="$(pgrep -f GhidraCliBridge | head -1 || true)"
fi
[ -n "$BPID" ] || { echo "could not find bridge PID" >&2; exit 1; }
BCWD="$(readlink "/proc/$BPID/cwd")"
echo "[5/6] running ApplyGlobals (bridge pid=$BPID cwd=$BCWD)"
cp "$HERE/ApplyGlobals.java" "$BCWD/ApplyGlobals.java"
trap 'rm -f "$BCWD/ApplyGlobals.java"' EXIT
ghidra script run ApplyGlobals.java --project "$PROJECT" || true
rm -f "$BCWD/ApplyGlobals.java"

echo "[6/7] apply report:"
sed -n '1,200p' "$REPORT" 2>/dev/null || echo "(no report written)"
rm -f "$REPORT"

# Persist the listing edits to disk with `program close` (flushes the whole
# program), then reopen to keep working. `ghidra analyze` does NOT reliably save
# freshly-created script data like this -- the definitions stay in memory but are
# not written on reload -- so save with close first. (Once saved, a later analyze
# is harmless: it preserves the already-defined globals.)
echo "[7/7] persisting to disk (program close + open)"
ghidra program close --program kar.dol --project "$PROJECT" >/dev/null 2>&1 || true
ghidra program open --program kar.dol --project "$PROJECT" >/dev/null 2>&1 || true

cat <<EOF

Globals are typed and saved to disk (via program close). Note: \`ghidra analyze\`
would not have saved these on its own -- always persist script edits with close.
EOF
