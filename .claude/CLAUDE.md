# CLAUDE.md

## Project Overview

KARchipelago is a collection of mods for Kirby Air Ride (GameCube, GKYE01), built on top of **hoshi**, a GameCube modding framework (in `externals/hoshi/`). The build produces a single Riivolution package containing every mod found under `mods/` (the Makefile auto-discovers them).

The target platform is PowerPC (GameCube), cross-compiled with devkitPPC.

### Mods in this repo

Each mod's public API header (if it has one) lives in `mods/<mod>/include/<mod>_api.h`, on the global include path so other mods import it via `Hoshi_ImportMod`.

- **`mods/archipelago/`** - Main mod (primary focus). Integrates Kirby Air Ride with the Archipelago randomizer: gating, checklist rewards, DeathLink/EnergyLink/TrapLink. Exposes `ArchipelagoAPI`.
- **`mods/archipelago_debug/`** - Debug menu. Imports `ArchipelagoAPI` to manipulate AP state for testing. Separate so it can be excluded from non-dev builds.
- **`mods/custom_events/`** - WIP. Registers new City Trial event kinds beyond `EVKIND_NUM` (Waddle Dee Swarm, Gravity/Scale Change, Gourmet Race). Exposes `CustomEventsAPI`. See `docs/custom-events.md`.
- **`mods/custom_weather/`** - WIP. Appends 9 custom sky/lighting presets to City Trial's preset array at runtime (`WeatherKind` in `custom_weather.h`). Hoshi menu toggles which appear.
- **`mods/custom_ai/`** - WIP scaffold. Two domains, each with a preset selector: enemy AI (`enemy_ai.c`) and CPU rider AI (`cpu_ai.c`). Menus + tuning tables only; the apply hooks are TODO. See `docs/enemy-ai-system.md`.
- **`mods/custom_items/`** - Adds City Trial item kinds from drop-in HSD `.dat` archives (FST `items/` scan, `customItem` descriptor, per-round `itData[]`/spawn-weight splice). Exposes `CustomItemsAPI` (incl. a pickup handler). Inert with no `.dat`s. See `docs/custom-items.md`.
- **`mods/custom_checklist/`** - Adds mod-owned checklist tabs alongside the vanilla 3, as synthetic clear-checker modes the engine renders for free. Mod supplies a check table, theme color, optional art. Exposes `CustomChecklistAPI`. Untouched with no tabs. See `docs/custom-checklist.md`.
- **`mods/hypernova/`** - WIP. City Trial super-inhale power-up: 2× Kirby + cone-vacuum of items/breakables + rainbow recolor. Per-player activation; granted by the `custom_items` "Miracle Fruit". Exposes `HypernovaAPI`. See `docs/hypernova.md`.
- **`mods/textbox/`** - On-screen notifications: queued, color-segmented, typewriter reveal. Used for AP grants/losses, deathlink/traplink. Exposes `TextBoxAPI` (`Enqueue*` + color helpers). See `docs/textbox.md`.

## Build

```bash
make deploy
```

`make deploy` runs `make package` (compile, link against hoshi, pack `.bin`s, copy assets, build `out/Riivolution/`) then copies it into Dolphin's `Load/Riivolution/`. It's the standard dev command; `make package` alone builds without touching Dolphin (use it to verify a compile). Run `make clean` first only when needed (linker script / header / build-config changes) - incremental rebuilds are the norm.

**Never run bare `make`** - always `make deploy` (or `make package`). The default target does not produce the deployable Riivolution mod and leaves the build incomplete.

**A successful `make deploy` (or `make package`) is sufficient verification.** The build fails on any compile or link error, so if it completes you do **not** need to grep its output for "error" or "warning". Pre-existing warnings in hoshi/devkitPPC headers are noise - don't re-run the build to inspect them.

**Source files are auto-discovered.** The Makefile globs all `*.c`/`*.s` under each `mods/*/src/`, so adding a source file (or a whole new mod folder with a `src/`) needs no manual registration - don't grep the Makefile for it. One exception: a new mod's **public** `include/` dir must be added to the Makefile's `INCLUDES` list (those are explicit, not globbed).

## Running Python Scripts

**Always use `uv run python` instead of bare `python` (or `python3`)** when invoking Python scripts in this repo (e.g. anything under `scripts/`). This ensures the project's managed environment and dependencies are used. Example: `uv run python scripts/hsd/explore.py ls iso/files/GrSpace2Model.dat` - never `python scripts/hsd/explore.py ...`.

## Project Structure

- `externals/hoshi/` - Hoshi modding framework (submodule). Contains headers (`include/`), linker script (`packtool/link.ld`), symbol map (`GKYE01.map`), and framework source (`src/`).
- `mods/<mod>/src/` - each mod's C/assembly sources (auto-discovered); public API headers in `mods/<mod>/include/`. See the Mods list above.
- `docs/` - Documentation and reference data (checklist mappings, protocol docs). See "Documentation in `docs/`" below for the full index.
- `scripts/` - Build helpers (`iso.py`, `dol.py`, toolchain image), RE tools (`disasm.sh`, `findptr.sh`, `mem1.raw`), the Ghidra type-import pipeline (`scripts/ghidra/` - `import_types.sh`/`apply_protos.py`/`import_globals.sh` push hoshi structs, prototypes, and globals into the project; see its `README.md`), and the HSD `.dat` toolchain (`scripts/hsd/` - `explore.py`, the backdrop carvers; see the `dat-explore` skill and `docs/sky-backdrop-system.md`).
- `out/` - Build output directory. `out/files/` is the staged ISO files-overlay (mod `.bin`s + assets + hoshi outputs); `out/Riivolution/` is the deployable Riivolution package; `out/patch.xdelta` is produced by `make patch`. Do not hand-edit. Run `make deploy` to copy `out/Riivolution/*` into Dolphin's Load dir.
- `iso/` - Extracted contents of `kar.iso` (game filesystem) - `files/` (game data, audio, textures) and `sys/` (`main.dol`, `boot.bin`). Use for inspecting original game assets.

## Important Files - Do NOT Read Directly

Never attempt to read ISO files, memory dumps, or other large binary/text files directly. Always use appropriate tools or scripts.

- **`externals/hoshi/GKYE01.map`** - Symbol map file (~20k lines). Never read the full file. Search for specific symbols with grep when needed.
- **`scripts/mem1.raw`** - Dolphin memory dump (~24MB) captured at the **main menu** (no gameplay/mod runtime state). Taken with hoshi loaded, so hoshi-rebuilt regions (e.g. the scene-desc table at `0x80495058`) are modded, not vanilla - when RE'ing a region hoshi patches/rebuilds, **verify against the source headers (`externals/hoshi/include/`), not this snapshot**. Bulk vanilla code/rodata/vtables are intact. Never read directly; use `scripts/disasm.sh` (disassembly) and `scripts/findptr.sh` (pointer search).
- **`*.iso`** - ISO files are large binaries. Never read directly; use the provided scripts in `scripts/` or appropriate tools.
- **`iso/`** - Extracted ISO contents. Individual `.dat`, `.dol`, etc. files can be read or hex-dumped as needed, but avoid reading the directory wholesale.

## Documentation in `docs/`

Current-state reference material - engine/mod facts and design rationale, not the RE process or verification steps (see the comments/docs convention under Code Conventions). One file per system; read the relevant one before working on that system.

- `checklist-mappings.csv` - Clear-kind indices, checkbox descriptions, rewards for all 3 modes (primary checklist reference).
- `checklist-grid-geometry.md` - Checklist 12×10 cell grid: procedural build, cell-GObj array, which functions hardcode the layout.
- `checklist-stat-tracking.md` - Stat layer beneath the clear bits: item/box collect counts, unlock accumulation.
- `clearchecker-system.md` - Clear checker system. (Sisters: the two checklist docs above + `custom-checklist.md`.)
- `custom-checklist.md` - Mod-owned-tab framework: synthetic-mode plumbing, registration/descriptor contract, theme recolor, art swap.
- `city-trial-event-system.md` - CT event system: state machine, function/param/BGM/sky tables, reserve queue, selection.
- `custom-events.md` - Custom events framework: registration, state-machine wrappers, SIS text, weighted selection.
- `event-gravity-change.md` / `event-scale-change.md` / `event-waddle-dee-swarm.md` / `gourmet-race-event.md` - Individual custom events.
- `event-source-drops.md` - `event_source_drop[]` table: struct, drop pipeline, source enum, CT drop weights.
- `meteor-actor.md` - Meteor event actor (0x4E): state machine, standalone spawn, patches.
- `enemy-ai-system.md` - Enemy AI: state machine, movement, targeting, per-type callbacks, custom AI integration.
- `cpu-ai-system.md` - CPU rider opponents (CT/AR/TR): perceive→decide→emit, virtual pad, tweak vs replace. (Sister: `enemy-ai-system.md`.)
- `enemy-spawn-system.md` - Enemy spawn: actor IDs, spawn manager, GOBJProc priorities, EventActorDesc.
- `collision-system.md` - Map collision (mpColl): CollData structs, coll_kind dispatch, raycasts. (Entity damage: `hurtdata-system.md`.)
- `hurtdata-system.md` - HurtData/HitColl damage: structs, pipeline, damage/knockback, applying damage from mod code.
- `effects-system.md` - Model-effect layer (GObj+JObj): `Effect_SpawnSync`, effect-ID encoding, TEV recolor. (Sister: `particle-system.md`.)
- `particle-system.md` - HSD point-particle pool: `Particle` struct, render path, free-lists, per-particle recolor.
- `copy-ability-system.md` - Copy ability lifecycle: grant, per-frame tick, teardown, force-drop from mod code.
- `custom-items.md` - Custom items framework: `.dat` FST discovery, `customItem` descriptor, `itData[]`/spawn-weight splice.
- `custom-hud.md` - Custom HUD elements: GObj/JObj setup, GX link rendering.
- `css-system.md` - Character select (all 3 modes): icon pipeline, Character/MachineKind mapping, availability.
- `machine-charge-system.md` - Machine charge/boost system.
- `projectile-system.md` - Projectile system.
- `yakumono-system.md` - Yakumono (stage objects) system.
- `scene-system.md` - Scene/mode transition system.
- `sis-text-system.md` - SIS text: commands, lookup tables, custom-event text extension.
- `sky-lighting-system.md` - Sky preset + lighting transition system.
- `sky-backdrop-system.md` - Custom CT backdrops: ModelSection, `3D_CreateStageModel` hook, carve toolchain. (Sister: `sky-lighting-system.md`.)
- `memcard-save-system.md` - Save system: vanilla `GCP_MemCard` + hoshi mod-data save + card tile format.
- `kirby-model-scale.md` - Big/Small Kirby scaling: per-object `model_scale` fields, all-mode implementation.
- `hypernova.md` - Hypernova power-up: vanilla-inhale presentation reuse, custom cone vacuum, recolor, 2× scale.
- `textbox.md` - Textbox notifications: colored-noun rendering, typewriter, scene-change persistence.
- `gate-*.md` - AP gating, one per resource: abilities, boxes, colors, events, items, machines, patches, stages, stadiums.
- `patch-cap.md` - Progressive patch cap: replacement hooks, consumer coverage, raising the ceiling.
- `patch-drop-system.md` - Patch drop pipeline: producer/consumer, drop_mode 0/1/2, stat→ItemKind table.
- `permanent-patches.md` - Permanent patches: round-start re-application, all-up consolidation.
- `spawn-rate.md` - Spawn Rate Up item: CT timer/cap scaling, TR probability scaling.
- `topride-system.md` - Top Ride mode: KirbyMgr, charge component, cpu_level encoding.
- `topride-item-system.md` - Top Ride items: bitmask, ability-themed items, gating hooks.
- `topride-kirby-states.md` - Top Ride Kirby states: `state_handler` slot, transitions, full state set for mod invocation.
- `client-game-protocol.md` / `deathlink.md` / `energylink.md` / `traplink-send.md` - AP client protocol + link systems.
- `pluggable-mods-riivolution.md` - Design note (not implemented): per-mod Riivolution toggles.

## Reverse Engineering Workflow

When reverse engineering game functions and discovering their purpose, **always** update the following files with findings before finishing:

1. **`externals/hoshi/include/`** - Add/update function declarations and data structure definitions in the appropriate header files (typically `game.h`). Include the address in a comment.
2. **`externals/hoshi/packtool/link.ld`** - Add symbol addresses for newly identified functions so they can be called from mod code. Data globals (r13-relative/SDA addresses) do not go here - declare them as `static` pointer casts in headers instead (see `event.h` or `game.h` for examples).
3. **`externals/hoshi/GKYE01.map`** - Rename unnamed symbols (`zz_XXXXXXXX_`) at their addresses to the discovered names.
4. **Ghidra:** Use the `ghidra-cli` skill to keep the Ghidra project in sync - rename the function to the discovered name, set its signature (`function set-signature`) using the hoshi types, and type any relevant local vars (`function set-var-type`). If you added or changed a **struct/enum** in the hoshi headers, re-import types with `scripts/ghidra/import_types.sh`; if you added or changed a documented prototype (a header decl with a `// 0xADDR` comment), re-run `scripts/ghidra/apply_protos.py` to push the new signatures; if you added or changed a **fixed-address global** (a `static TYPE *name = (TYPE*)0xADDR;` / cast-macro / r13-relative decl), re-run `scripts/ghidra/import_globals.sh` to type and label it. See `scripts/ghidra/README.md`. The bridge does not auto-save; persist edits to disk with `ghidra program close --program kar.dol` (then `ghidra program open --program kar.dol` to keep working). `ghidra analyze` does **not** reliably save freshly-created script data (e.g. the global `createData` definitions), so use `program close` (`import_globals.sh` does the close+open for you). Running `analyze` later is safe - it preserves already-saved globals.

### Tooling

- **Disassembly:** **Always** use `scripts/disasm.sh` for disassembling code from the memory dump. Never use `xxd` or raw `objdump` for disassembly - `disasm.sh` handles address conversion, objdump flags, and map lookups automatically. Accepts symbol names, addresses, or addresses with explicit length:
  ```bash
  ./scripts/disasm.sh Gm_GetHSDUpdate           # by symbol name (auto address + size)
  ./scripts/disasm.sh 0x80007AF0                 # by address (auto size from map)
  ./scripts/disasm.sh 0x80007AF0 0x120           # by address with explicit length
  ```
- **Hex inspection:** Use `xxd` **only** for inspecting raw *data* regions in `scripts/mem1.raw` (struct layouts, raw byte values, non-code memory). Never use `xxd` for code - use `disasm.sh` instead. The memory dump is at `scripts/mem1.raw` (same location `disasm.sh` reads from).
  ```bash
  xxd -s $((ADDRESS - 0x80000000)) -l <length> scripts/mem1.raw
  ```
- **Pointer search:** Use `scripts/findptr.sh` to find all 4-byte-aligned occurrences of a 32-bit value in `scripts/mem1.raw`. Essential for locating vtables, function pointer tables, and global pointers to a struct. Supports optional region bounds to narrow the search.
  ```bash
  ./scripts/findptr.sh 0x80412AB0                           # search all of mem1
  ./scripts/findptr.sh 0x80412AB0 0x80000000 0x80600000     # search a region
  ```
- **Symbol lookup:** Grep `externals/hoshi/GKYE01.map` for addresses or symbol names. The map format includes address, size, and name per line.
- **Live memory inspection:** When the user wants to read or write game memory in real time (vs the stale `scripts/mem1.raw` snapshot) - watch values change, dump runtime structs, poke values for testing - use the `dolphin-memory` skill. Requires Dolphin to be running with the game loaded. Pairs with manual debugging (breakpoints, frame advance) rather than replacing it.
- **Ghidra:** Use the `ghidra-cli` skill for deeper analysis: decompilation to C pseudocode, cross-references, call graphs, function listing, and symbol/type management. A default project and binary are already configured - **never** specify or attempt to load a different project or binary. **Never** run `ghidra setup`, `ghidra config set`, or any command that manages the Ghidra installation or bridge configuration. The bridge install directory, default project, and default program are pre-configured by the user. Only start/stop/restart the bridge; never reconfigure it.
  - **Ghidra script execution does not work through the bridge.** Any `ghidra-cli` subcommand that runs a Ghidra script (`.java`/`.py` headless analyzer scripts, the `runScript` family, etc.) will fail. Stick to bridge-native operations: decompilation, cross-references, function listing, symbol/type queries, renames. If a task genuinely needs a script, surface that to the user rather than attempting it.

## Code Conventions

- C99, targeting PowerPC 750 (GameCube CPU). The r13 register (SDA base) is `0x805DD0E0`; game globals in this range are accessed as r13-relative offsets. The r2 register (SDA2 base) is `0x805E6700`; read-only small data (float constants, etc.) are accessed as r2-relative offsets.
- Compiled with `-O1`, no exceptions, no RTTI, freestanding environment.
- **Brace style: Allman.** Opening brace on its own line for all function definitions, matching hoshi style. Apply this to all mods in this repo.
- All headers use `#ifndef`/`#define`/`#endif` include guards. For new hoshi headers prefer a `KAR_H_<NAME>` guard macro (many older ones still carry the legacy `MEX_H_<NAME>` prefix from the m-ex framework). Mod headers - including the public API headers on the global include path - use plain name-based guards.
- Game memory addresses and structures are defined in hoshi headers under `externals/hoshi/include/`. Don't redeclare them in mod code - include the appropriate hoshi header instead.
- **Never reference `docs/` files in code comments** (no `see docs/foo.md`, no doc section names, no `docs/*.csv`). Comments must stand on their own - state the relevant fact inline instead of pointing at a doc. The `docs/` tree is reference material that drifts independently of the code; a comment pointer becomes stale silently.
- **Comments and docs are current-state, not a lab notebook.** Both code comments and `docs/*.md` describe what the code/engine does *now* and why it is shaped that way. Do **not** narrate the reverse-engineering process or how a conclusion was reached, and do **not** include dated or "verified live"/Dolphin-verification notes, past failed/"naive" attempts, or "old docs said X" corrections. State the fact and ground it in the code (function name + address). Forward-looking design notes for WIP/unimplemented features are fine - just drop the "RE gap / needs live confirm" framing. When you learn something by live verification, record the resulting *fact*, not the act of verifying it.

### Debug Output (OSReport)

- Every `OSReport` call must have a `[Component]` prefix matching the source file, e.g. `[GateMachines]`, `[DeathLink]`, `[Main]`.
- Bitmasks are printed in full binary using `MaskBits(val, bits)` from `mask_fmt.h`, not hex. Example: `OSReport("[GateBoxes] Box %d unlocked (mask = %s)\n", kind, MaskBits(mask, 8));`
- Keep output succinct: one consolidated "Hooks installed" line per component at boot, not one per hook.
- Avoid per-frame or per-tick logging. Log state changes, decisions, and errors - not ongoing activity.
- Menu toggle changes are logged via `on_change` callbacks on `OptionDesc` (see `main.c`).

### Naming Hierarchy

The game has a clear object hierarchy. Functions and data structures must always be named to indicate the level they operate at:

| Level | Struct | Function Prefix | Description |
|-------|--------|-----------------|-------------|
| Game | `GameData` | `Gm_` | Global game state, mode settings, scene management |
| Player | `PlayerData` | `Ply_` | Per-player state (stats, HP, controller slot) |
| Rider | `RiderData` | `Rider_` | The character (Kirby, etc.) riding a machine |
| Machine | `MachineData` | `Machine_` | The vehicle/star being ridden |

When discovering, naming, or renaming functions and structs during reverse engineering, always determine which level they operate on and use the corresponding prefix. For example, a function that modifies `MachineData.stats` should use the `Machine_` prefix, not a generic `Stats_` prefix.
