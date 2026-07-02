# Ghidra type/signature import from hoshi headers

Tooling to push hoshi's C type definitions, documented function signatures, and
fixed-address globals into the Ghidra `kar.dol` project so decompiles resolve
struct fields, enum values, typed params, and named globals instead of raw
`undefined*` pointer math.

Runs through the `ghidra-cli` bridge (see the `ghidra-cli` skill). The bridge does
not auto-save; flush changes to disk with **`ghidra program close --program
kar.dol`** (then `ghidra program open --program kar.dol` to keep working).
`ghidra analyze` does **not** reliably save freshly-created script data (phase 3's
`createData` globals stay in memory but aren't written on reload) — so save with
`program close`, not analyze. This isn't analyze destroying data: once the globals
are saved, a later `analyze` preserves them (verified analyze → close → restart).

## Three phases

Run them in order — each builds on the last (signatures and globals apply the
types from phase 1). Flush to disk with `program close` after each (see above);
phase 3's `import_globals.sh` does the close+open for you.

### Phase 1 — types (structs / enums / unions / typedefs)

```bash
scripts/ghidra/import_types.sh --project kar-decomp
ghidra program close --program kar.dol --project kar-decomp   # flush to disk
ghidra program open  --program kar.dol --project kar-decomp   # reopen to continue
```

Ghidra's C parser aborts on function bodies, but the hoshi headers mix type
declarations with `static inline` bodies. `import_types.sh`:

1. `strip_bodies.py` mirrors `externals/hoshi/include` into a temp dir with every
   function body and file-scope initializer replaced by `;` (structs/enums/
   prototypes kept). The parser searches its include path in order for both
   `"..."` and `<...>`, so the mirror shadows the originals.
2. `stubs/` supplies empty headers (`math.h stdio.h string.h stdarg.h inline.h
   ip.h`) that shadow the system include and the inline/networking detours we
   skip.
3. `hoshi_all.h` `#define`s away `_Static_assert`/`static`/`inline` and includes
   the game headers in dependency order.
4. `ParseHoshiHeaders.java` (a ghidra-cli bridge script) calls Ghidra's
   `CParserUtils.parseHeaderFiles` on that master header into the program's
   DataTypeManager.

### Phase 2 — function signatures

```bash
uv run python scripts/ghidra/apply_protos.py --project kar-decomp   # add --dry-run to preview
ghidra program close --program kar.dol --project kar-decomp   # flush to disk
ghidra program open  --program kar.dol --project kar-decomp   # reopen to continue
```

`apply_protos.py` reads every hoshi prototype carrying a `// 0xADDR` comment
(`extract_protos.py`) and sets that function's signature in Ghidra by address.
It also sets the return type of global getters (e.g. `gmGetGlobalP` →
`GameData *`), which Ghidra propagates to every caller for free. It keeps
Ghidra's name when Ghidra already has a better one (e.g. `HSD_`-prefixed) and
fixes CParser syntax quirks (pointer returns, array params, `const`).

### Phase 3 — fixed-address globals

```bash
scripts/ghidra/import_globals.sh --project kar-decomp   # applies + saves (close+open)
```

hoshi pins engine globals to their runtime addresses (`static PlayerData
*stc_playerdata = (PlayerData *)0x8055a9f0;`, `#define stc_enemy_param_table
(*(void **)0x805dd878)`, r13-relative `(CityItemMgr **)(0x805dd0e0 + 0x7EC)`,
etc.). `extract_globals.py` pulls all of them (~118); `ApplyGlobals.java` clears
the conflicting data auto-analysis laid down at each address and re-creates it as
the documented hoshi type with a primary label. This turns
`*(int*)(DAT_805dd8cc + 0x…)` in the decompiler into
`stc_city_item_mgr->live_item_count`.

Needs phase 1 done first (the struct types must exist to apply them). A handful of
addresses are skipped by design: MMIO (`gx_pipe` at `0xcc008000`) and a few data
tables Ghidra mis-analyzed as code inside `.text1` (the script won't clobber a
function). Array extents aren't encoded in the decls, so indexed globals get a
single element unless listed in `ARRAY_SIZES` in `extract_globals.py`
(`stc_playerdata` → `PlayerData[5]`).

## Files

| file | role |
|------|------|
| `import_types.sh` | phase-1 driver (Linux; uses `/proc` to find the bridge CWD) |
| `strip_bodies.py` | mirror headers with function bodies removed |
| `stubs/` | empty headers shadowing system/detour includes |
| `hoshi_all.h` | master header the parser consumes |
| `ParseHoshiHeaders.java` | bridge script that runs the C parser (installed into `~/.config/ghidra-cli/scripts/`) |
| `extract_protos.py` | pull `// 0xADDR`-documented prototypes from the headers (also a module) |
| `apply_protos.py` | phase-2 driver: apply those prototypes + getter return-types |
| `import_globals.sh` | phase-3 driver (Linux; uses `/proc` to find the bridge CWD) |
| `extract_globals.py` | pull fixed-address global-data decls from the headers (also a module) |
| `ApplyGlobals.java` | bridge script: clear + retype + label each global address |

## Notes / caveats

- **Re-running is not idempotent for types.** Parsing into an already-populated
  DataTypeManager can create `.conflict` duplicates. Re-import phase 1 only
  against a clean/baseline program.
- Phase 2 *is* safe to re-run — `set-signature` overwrites in place.
- Phase 3 *is* safe to re-run — each address is cleared before it's retyped.
- If hoshi adds a new top-level header whose types you want, add its `#include`
  to `hoshi_all.h`.
- Types are imported but **not auto-applied** to existing functions; a function
  benefits only once its params/vars are typed (which is exactly what phase 2
  does for the documented ones). For undocumented functions, type vars ad hoc
  with `ghidra function set-var-type` / `set-signature`.
