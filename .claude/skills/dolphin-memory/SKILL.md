---
name: dolphin-memory
description: >
    Use the dolphin-memory skill for live game memory inspection while Dolphin is running with the game loaded. Activate when the user wants to:
    - Read or write game memory in real time (vs the stale `scripts/mem1.raw` snapshot)
    - Watch / poll a value as it changes during gameplay
    - Dump a struct region from the live process (RiderData, GameData, etc.)
    - Poke a value to test behavior without rebuilding
    - Resolve a `GKYE01.map` symbol to its current runtime value
    Requires Dolphin to be running with active emulation. Pairs with manual debugging (breakpoints, frame advance) - does not replace it.
---

# dolphin-memory Skill

Live read/write access to GameCube memory through a running Dolphin instance, via the `dolphin-memory-engine` Python package ([repo](https://github.com/henriquegemignani/py-dolphin-memory-engine)). Acts like a real-time `mem1.raw` you can also write to.

## Prerequisites

1. **Dolphin must already be running** with Kirby Air Ride loaded and emulation started. The user runs Dolphin manually; this skill does not launch it.
2. The package is invoked via `uv run` (per the project rule of always using `uv run python` instead of bare `python`). For one-offs:
   ```bash
   uv run --with dolphin-memory-engine python -c '...'
   ```
   For repeatable scripts, put a PEP 723 header at the top of the script so `uv run scripts/foo.py` resolves dependencies automatically:
   ```python
   # /// script
   # dependencies = ["dolphin-memory-engine"]
   # ///
   import dolphin_memory_engine as dme
   ```

## Hooking

```python
import dolphin_memory_engine as dme

dme.hook()                              # attach to Dolphin process
assert dme.is_hooked(), dme.get_status()
# ... do work ...
dme.un_hook()                           # detach (optional; process exit detaches)
```

`get_status()` returns a `DolphinStatus` enum: `hooked`, `notRunning` (no Dolphin process), `noEmu` (Dolphin running but no game loaded), `unHooked`.

`assert_hooked()` raises `RuntimeError("not hooked")` if not currently attached. The read/write functions call this internally, so an unhooked operation raises rather than silently failing.

If `hook()` succeeds but `is_hooked()` is still false, surface the status explicitly to the user - usually means Dolphin is running but no game is loaded (`noEmu`).

## Complete API Reference

All addresses are **console addresses** (the GameCube virtual address space, e.g. `0x80312A40`). The library translates to the Dolphin process offset internally.

### Connection
| Function | Purpose |
|---|---|
| `hook()` | Attach to the running Dolphin process. |
| `un_hook()` | Detach. |
| `is_hooked() -> bool` | Currently attached? |
| `assert_hooked()` | Raises `RuntimeError("not hooked")` if not attached. |
| `get_status() -> DolphinStatus` | Enum: `hooked`, `notRunning`, `noEmu`, `unHooked`. |

### Reading
| Function | Returns | Notes |
|---|---|---|
| `read_byte(addr) -> int` | `uint8` | |
| `read_word(addr) -> int` | `uint32` | byte-swapped to host order |
| `read_float(addr) -> float` | `float32` | byte-swapped |
| `read_double(addr) -> float` | `float64` | byte-swapped |
| `read_bytes(addr, size) -> bytes` | raw bytes | **NOT byte-swapped** - exact GameCube big-endian memory |

### Writing
| Function | Notes |
|---|---|
| `write_byte(addr, value)` | |
| `write_word(addr, value)` | byte-swapped to GC order |
| `write_float(addr, value)` | byte-swapped |
| `write_double(addr, value)` | byte-swapped |
| `write_bytes(addr, bytes)` | **NOT byte-swapped** - caller supplies big-endian buffer |

### Pointer Chains
```python
addr = dme.follow_pointers(0x804D7714, [0x10, 0x20])
```
Reads the 32-bit pointer at `0x804D7714`, dereferences, adds `0x10`, reads pointer there, dereferences, adds `0x20`. Returns the final address. Raises `RuntimeError` if any intermediate pointer is invalid (`isValidConsoleAddress` check).

### MemWatch Class
A persistent watch entry that caches a buffer and re-reads on demand. Useful for repeated polling of one address.
```python
watch = dme.MemWatch(label="kirby_hp", console_address=0x803F1234, is_pointer=False)
watch.read_memory_from_ram()           # refresh from RAM; returns True on success
print(watch.get_value())               # uint32
watch.add_offset(0x4)                  # for pointer-chain watches
watch.write_memory_from_string("42")   # writes value parsed from string
```

`MemWatch` only stores 4-byte (word) values. For other sizes use the standalone `read_*` / `write_*` functions.

## ⚠️ No `read_u16` / `read_short`

The package only exposes byte / word(32) / float / double scalars. For 16-bit values (or any non-standard width) use `read_bytes` + manual decode:

```python
# u16 (big-endian on GC):
val = int.from_bytes(dme.read_bytes(addr, 2), "big")

# s16:
import struct
(val,) = struct.unpack(">h", dme.read_bytes(addr, 2))
```

## Resolving Symbols from `GKYE01.map`

The map format is `<addr> <size> <addr2> <flag> <name>` (5 columns, addr/size in hex without `0x`). Symbols can be functions or data globals - for r13-relative SDA globals, the absolute address is what's in the map.

Lookup helper:

```python
import re
from pathlib import Path

MAP = Path("externals/hoshi/GKYE01.map")

def resolve(symbol: str) -> tuple[int, int]:
    """Return (address, size) for a symbol from the hoshi map. Raises KeyError if not found."""
    pattern = re.compile(
        rf"^([0-9a-f]{{8}})\s+([0-9a-f]+)\s+\S+\s+\S+\s+{re.escape(symbol)}\s*$",
        re.MULTILINE,
    )
    m = pattern.search(MAP.read_text())
    if not m:
        raise KeyError(symbol)
    return int(m.group(1), 16), int(m.group(2), 16)

addr, size = resolve("Gm_GetGameData")
```

For ad-hoc lookups from the shell: `grep -E " <symbol>\s*\$" externals/hoshi/GKYE01.map`.

## Recipes

### Read a value by symbol

```python
import dolphin_memory_engine as dme

dme.hook()
gamedata_addr, _ = resolve("gGameData")
hp = dme.read_word(gamedata_addr + 0x40)   # offset pulled from hoshi headers
print(f"hp = {hp}")
```

### Dump a struct region

```python
import struct

def dump_riderdata(base: int):
    raw = dme.read_bytes(base, 0x800)       # raw big-endian
    hp     = struct.unpack_from(">I", raw, 0x40)[0]
    speed  = struct.unpack_from(">f", raw, 0x44)[0]
    state  = struct.unpack_from(">H", raw, 0x48)[0]
    print(f"hp={hp} speed={speed:.2f} state=0x{state:04x}")
```

`struct` format chars on big-endian (`>`): `B`/`b` = u8/s8, `H`/`h` = u16/s16, `I`/`i` = u32/s32, `Q`/`q` = u64/s64, `f` = float, `d` = double.

When dumping a region that you'll later compare against `mem1.raw`, remember `mem1.raw` is the **vanilla menu snapshot** - heap layout will differ. Map-resolved globals are stable; runtime allocations are not.

### Watch loop (poll for changes)

```python
import time

dme.hook()
addr = 0x803F1234
prev = None
while True:
    val = dme.read_word(addr)
    if val != prev:
        print(f"{addr:#x} = {val} ({val:#x})")
        prev = val
    time.sleep(0.05)                          # 20 Hz
```

Keep poll intervals at ≥16 ms (60 Hz) - anything faster duplicates reads of the same frame and burns CPU. Use `try/except KeyboardInterrupt` so the user can stop the loop cleanly.

### Poke a value (write test)

```python
dme.hook()
dme.write_word(0x803F1234, 999)               # force max HP for damage testing
```

Writes can fire user-set Dolphin watchpoints unexpectedly. Always confirm with the user before issuing writes during an active debugging session, and mention writes in the end-of-turn summary.

### Pointer-chain dereference

For values behind allocator-managed pointers (move each run/round):

```python
# Walk: read pointer at 0x804D7714, +0x40, read pointer, +0x10
final = dme.follow_pointers(0x804D7714, [0x40, 0x10])
val = dme.read_word(final)
```

The semantics of `follow_pointers` are: for each offset, dereference the *current* address as a pointer, then add the offset. The final return is **the address you should read**, not yet dereferenced.

## Common Pitfalls

- **Forgetting to hook**: every read/write calls `assert_hooked` and raises `RuntimeError("not hooked")`. Call `dme.hook()` first.
- **Dolphin not running / no game loaded**: `get_status()` returns `notRunning` or `noEmu`. Surface this clearly to the user - don't pretend to read.
- **Using `read_bytes` and expecting host-endian ints**: `read_bytes` is raw GC big-endian. Use `struct.unpack(">...")` or `int.from_bytes(..., "big")`.
- **Confusing `mem1.raw` with live state**: `scripts/mem1.raw` is the *vanilla menu snapshot*. If the user asks "what is X right now", use this skill, not the dump. Use `mem1.raw` for static analysis (vtables, code patterns) only.
- **Hard-coded heap addresses across runs**: addresses for allocator-backed objects can shift between sessions. Re-resolve from map symbols or pointer chains rather than baking ephemeral addresses into scripts.
- **Tight polling**: `time.sleep(0.05)` (20 Hz) is plenty for human-visible changes; tighter loops just heat the CPU.

## Pairing with Manual Debugging

The user controls Dolphin: breakpoints, frame advance, save/load states, watchpoints. This skill complements that workflow:

- While the game is **paused at a breakpoint**, you can inspect arbitrary memory without disturbing state.
- While the game is **running**, you can poll values to capture transient behavior.
- **Writes are intrusive** - they can trip user-set watchpoints, corrupt a repro the user is capturing, or desync the game from a save state. Confirm before writing during an active session.

If the user is iterating on a fix, prefer reads + suggesting code changes over writing memory to mask a bug.
