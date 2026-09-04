# CLAUDE.md

## Project Overview

KARchipelago is a collection of mods for Kirby Air Ride (GameCube, GKYE01), built on top of **hoshi**, a GameCube modding framework (in `externals/hoshi/`). The build produces a single Riivolution package containing the mods named by `INCLUDE_MODS`.

The target platform is PowerPC (GameCube), cross-compiled with devkitPPC.

## Project Structure

- `mods/` - one subdirectory per mod: sources in `src/`, and a public API header at `include/<mod>_api.h` if the mod exports one (other mods import it via `Hoshi_ImportMod`). `INCLUDE_MODS` chooses which are built. A mod's `assets/` is the **disc staging folder** - its contents are copied into the FST root keeping their paths under `assets/`, so only things the game loads belong there.
- `art/` - source assets the authoring scripts read: PNGs for `scripts/authoring/` and `scripts/hsd/`, WAVs for `scripts/audio/`. Kept out of `mods/*/assets/` because nothing on disc reads them and staging them just pads the package.
- `externals/hoshi/` - the hoshi modding framework (submodule): headers, linker script, symbol map (`GKYE01.map`), framework source.
- `docs/` - per-system reference docs and data files; one doc per system, found by filename.
- `scripts/` - `kar.py` (the RE tool over `mem1.raw` + `GKYE01.map` + `link.ld`), plus `ghidra/` (type pipeline), `hsd/` (the general `.dat` toolchain - anything that works on any archive), `authoring/` (per-mod asset authors: one script per shipped `.dat`, writing to a fixed path under `mods/<mod>/assets/`), `audio/` (machine sound / SSM toolchain), `devkitpro/` (toolchain build), `utility/` (ISO / DOL / memcard-tile helpers).
- `out/` - build output; `out/Riivolution/` is the deployable package. Do not hand-edit.
- `iso/` - extracted contents of `kar.iso` - original game assets for inspection.

## Build

```bash
make deploy INCLUDE_MODS=archipelago,custom_machines
```

`make deploy` runs `make package` (compile, link against hoshi, pack `.bin`s, copy assets, build `out/Riivolution/`) then copies it into Dolphin's `Load/Riivolution/` (`DOLPHIN_RIIVOLUTION_DIR`, overridable). It's the standard dev command; `make package` alone builds without touching Dolphin (use it to verify a compile). Run `make clean` first only when needed (linker script / header / build-config changes) - incremental rebuilds are the norm.

**`INCLUDE_MODS` decides what gets built, and there is no default** - without it the build silently produces a package with no mods in it. Names are comma- or space-separated, must match folder names under `mods/`, and anything else is ignored without a warning. It gates each mod's `assets/` copy as well as its code, so build the same set you intend to run. When working on one mod, pass just that one; ask which set to build when it matters and the user has not said.

**Never run bare `make`** - always `make deploy` (or `make package`). The default target does not produce the deployable Riivolution mod and leaves the build incomplete.

**A successful `make deploy` (or `make package`) is sufficient verification.** The build fails on any compile or link error, so if it completes you do **not** need to grep its output for "error" or "warning". Pre-existing warnings in hoshi/devkitPPC headers are noise - don't re-run the build to inspect them.

**Source files are auto-discovered.** The Makefile globs all `*.c`/`*.s` under each included mod's `src/`, so adding a source file (or a whole new mod folder with a `src/`) needs no manual registration - don't grep the Makefile for it. One exception: a new mod's **public** `include/` dir must be added to the Makefile's `INCLUDES` list (those are explicit, not globbed).

## Important Files - Do NOT Read Directly

- **`externals/hoshi/GKYE01.map`** - Symbol map file (~20k lines). Never read the full file. Search for specific symbols with grep when needed.
- **`scripts/mem1.raw`** - Dolphin MEM1 dump (24MB) of the **vanilla** game at the **main menu** - no hoshi, no gameplay state. Authoritative for code and rodata; `.data`/`.sdata` carry main-menu runtime state, so treat those as a snapshot rather than the static layout. Gitignored - `kar.py` says so if it is absent. Never read it directly - go through `scripts/kar.py`.
- **`*.iso`** - large binaries. Never read directly; `scripts/utility/iso.py` extracts and patches them.
- **`iso/`** - Extracted ISO contents. Individual `.dat`, `.dol`, etc. files can be read or hex-dumped as needed, but avoid reading the directory wholesale.

## Reverse Engineering Workflow

When reverse engineering game functions and discovering their purpose, **always** update the following files with findings before finishing:

1. **`externals/hoshi/include/`** - Add/update function declarations and data structure definitions in the appropriate header files (typically `game.h`). Include the address in a comment.
2. **`externals/hoshi/packtool/link.ld` + `GKYE01.map`** - `uv run python scripts/kar.py rename 0xADDR Name` does both: it renames the `zz_XXXXXXXX_` map symbol and adds the `link.ld` entry that lets mod code call the function. Data globals (r13-relative/SDA addresses) do not belong in `link.ld` - declare them as `static` pointer casts in headers instead (see `event.h` or `game.h` for examples). When you are done, `uv run python scripts/kar.py check` lists any prototype still missing from either file.
3. **Ghidra:** Once the headers carry the finding, push it with **`uv run python scripts/ghidra/sync.py`** - it parses hoshi's structs/enums into Ghidra, applies every prototype documented with a `// 0xADDR` comment, types and labels every global pinned to a literal address (`static TYPE *name = (TYPE*)0xADDR;`), and saves. Name a phase (`types`, `protos`, `globals`) to run just that one, `--dry-run` to preview; all phases are safe to re-run. Anything the headers can't express (local var types, a rename you want before the header exists) goes through the `ghidra-cli` skill directly - but persist those yourself with `ghidra program close --program kar.dol` (then `ghidra program open --program kar.dol` to keep working), since the bridge never auto-saves and `ghidra analyze` does **not** reliably write freshly-created data back to disk.

## Tooling

- **Python:** run repo scripts with `uv run python`, never bare `python`/`python3` - uses the project's managed environment.
- **Static game image:** `uv run python scripts/kar.py <cmd>` answers anything that comes from `mem1.raw`, `GKYE01.map`, or `link.ld`. Reach for it instead of `xxd`/`objdump` on the dump or hand-grepping the map; most subcommands take a symbol name or a `0xADDR`, and `--help` has the flags.
  - `sym` - symbol lookup by address, name, substring, or range; `[ld hdr]` flags say whether `link.ld` and a hoshi header already carry it.
  - `disasm` - length comes from the map when omitted; call targets, `lis` pairs, and r13/r2 accesses are annotated with what they resolve to.
  - `read` - `-f hex|words|floats|halfs|string`; `words` names any value pointing at a known symbol.
  - `find` - 4-byte-aligned 32-bit search (vtables, pointer tables).
  - `decomp` - Ghidra decompilation as plain C; `ghidra decompile` itself only emits JSON.
  - `rename` - names an unnamed symbol in `GKYE01.map` and `link.ld` at once.
  - `check` - hoshi prototypes missing from `link.ld` or the map, and `link.ld` names whose map row is still `zz_`.
- **Skills:** `dolphin-memory` (live memory while Dolphin runs), `dat-explore` (HSD `.dat` archives), `ghidra-cli` (decompilation, xrefs, types).
- **Ghidra constraints:** use the pre-configured project/program only - never `ghidra setup`/`config` or load another binary. Inline `ghidra script python`/`script java` are unavailable in bridge mode, and `script run` needs the copy-and-report plumbing in `scripts/ghidra/sync.py` - drive new `.java` scripts through that rather than rebuilding it.

## Comments

Keep comments short and minimal - state only what the reader needs, prefer no comment to an obvious one, and match the surrounding code's comment density.

- **ASCII only.** No typographic ligatures or Unicode glyphs anywhere in source - no arrows, dashes, ellipses, or math symbols. Write `->`, `-`, `...`, `~`, `>=`. This applies to string literals too (`OSReport` output is an ASCII console).
- **No decorations.** No banners, box art, or `====`/`----` rules, and no trailing dressing - not even on a one-line comment. Just `//` or `/* */` and the text.
- **Minimal by default.** A comment earns its place by carrying information the code cannot. Delete restatements of the code, rationale for decisions no reader will question, and background that belongs in `docs/`. Struct-member comments are fine but stay to one short line.
- **No section markers.** Don't use comments to label regions of a file or function (`// --- setup ---`, `// helpers below`); let the code's structure do that.
- **Current-state only.** Say what the code does now and why it is shaped that way. No "how we got here" narration - no RE process, dated/"verified live" notes, past failed/"naive" attempts, or "used to be X" corrections.
- **No references to other files.** A comment must stand on its own. Don't point at `docs/*.md` (no `see docs/foo.md`, section names, or `docs/*.csv`) or any other file - the target drifts independently and the pointer goes stale silently. State the fact inline.
- **Large explanations belong in docs.** If a comment outgrows a few lines, move the prose to the relevant `docs/*.md` and leave only the essential fact in the code (grounded in a function name + address where relevant).

## Docs

`docs/` holds per-system reference material - one file per engine/mod system, plus `checklist-mappings.csv`. Find the relevant doc by filename and read it before working on that system. Each doc stands on its own:

- **Self-contained, no cross-references.** One file fully covers its system; a reader shouldn't need to open another doc. Don't point from one `docs/*.md` to another (no "see `foo.md`", section names, "sister doc") - restate the fact instead. Docs are discovered by filename, not by links.
- **Current-state only**, same rule as comments: what the code/engine does *now* and why, never how we got here. Ground facts in the code (function name + address). Forward-looking notes for WIP features are fine - just drop the "RE gap / needs live confirm" framing.

## Code Style

- C, targeting PowerPC 750 (GameCube CPU). No `-std=` flag is passed, so the toolchain default applies (C23 on the current devkitPPC GCC); `_Static_assert` and designated initializers are used freely.
- Compiled `-O1 -ffreestanding -fno-exceptions`, no libc beyond what hoshi provides.
- The r13 register (SDA base) is `0x805DD0E0`; game globals in this range are accessed as r13-relative offsets. The r2 register (SDA2 base) is `0x805E6700`; read-only small data (float constants, etc.) are accessed as r2-relative offsets.
- **Brace style: Allman.** Opening brace on its own line, matching hoshi style. The one exception in practice is a one-line wrapper or menu callback kept on a single line (`settings_menu.c`, `machine_registry.c`).
- All headers use `#ifndef`/`#define`/`#endif` include guards; no `#pragma once` anywhere. New hoshi game headers use `KAR_H_<NAME>` (roughly half still carry the legacy `MEX_H_<NAME>` from m-ex; `externals/hoshi/include/hoshi/*.h` use `HOSHI<NAME>_H`). Mod headers - including the public API headers on the global include path - use plain name-based guards.
- Game memory addresses and structures are defined in hoshi headers under `externals/hoshi/include/`. Don't redeclare them in mod code - include the appropriate hoshi header instead.

**Naming follows the object hierarchy.** Name functions and structs for the level they operate at - a function that modifies `MachineData.stats` uses the `Machine_` prefix, not a generic `Stats_`.

| Level | Struct | Prefix | Scope |
|-------|--------|--------|-------|
| Game | `GameData` | `Gm_` | Global game state, mode settings, scene management |
| Player | `PlayerData` | `Ply_` | Per-player state (stats, HP, controller slot) |
| Rider | `RiderData` | `Rider_` | The character (Kirby, etc.) riding a machine |
| Machine | `MachineData` | `Machine_` | The vehicle/star being ridden |

### Debug Output (OSReport)

- Every `OSReport` call must have a `[Component]` prefix naming the subsystem the reader cares about, e.g. `[GateMachines]`, `[DeathLink]`, `[MachineAudio]`. In a mod whose files are independent subsystems (`archipelago`, `custom_machines`, `custom_weather`) that is the source file; in a small single-subsystem mod (`custom_ai`, `custom_items`, `hypernova`, `textbox`, `archipelago_debug`) it is the mod. Prefixes must be unique across the whole package - `[Main]` belongs to `archipelago/src/main.c` alone.
- Bitmasks are printed in full binary using `MaskBits(val, bits)` from `inline.h`, not hex. Example: `OSReport("[GateBoxes] Box %d unlocked (mask = %s)\n", kind, MaskBits(mask, 8));` The rotating buffer holds 32 bits, so clamp wider counts.
- Keep output succinct: one consolidated "Hooks installed" line per component at boot, not one per hook. No trailing periods, no ellipses, past tense for things that happened.
- Avoid per-frame or per-tick logging. Log state changes, decisions, and errors - not ongoing activity. A message on a path the engine retries (per spawn attempt, per scene while a failure persists) must latch so it prints once.
- Report per-player work as one line with a count, not one line per player.
- Menu toggle changes are logged via `on_change` callbacks on `OptionDesc`.
