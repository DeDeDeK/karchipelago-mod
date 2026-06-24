# CLAUDE.md

## Project Overview

KARchipelago is a collection of mods for Kirby Air Ride (GameCube, GKYE01), built on top of **hoshi**, a GameCube modding framework (in `externals/hoshi/`). The build produces a single Riivolution package containing every mod found under `mods/` (the Makefile auto-discovers them).

The target platform is PowerPC (GameCube), cross-compiled with devkitPPC.

### Mods in this repo

- **`mods/archipelago/`** - Main mod. Integrates Kirby Air Ride with the Archipelago multiworld randomizer: gating systems, checklist rewards, DeathLink/EnergyLink/TrapLink, etc. This is the primary focus of the project. Exposes an `ArchipelagoAPI` consumed by `archipelago_debug`.
- **`mods/archipelago_debug/`** - Debug menu mod. Imports `ArchipelagoAPI` via `Hoshi_ImportMod` to manipulate AP state for testing (toggle gates, force grants, etc.). Kept separate so the debug surface can be excluded from non-dev builds.
- **`mods/custom_events/`** - WIP. Custom City Trial event framework that registers new event kinds beyond vanilla `EVKIND_NUM` (Waddle Dee Swarm, Gravity Change, Scale Change, Gourmet Race; dormant Cannon scaffolding in `cannon_event.c`). Exposes a `CustomEventsAPI` (header in `mods/custom_events/include/custom_events_api.h`) consumed by the archipelago mod for event gating and triggering. See `docs/custom-events.md`.
- **`mods/custom_weather/`** - WIP. Adds custom sky/lighting presets to City Trial (extends the stage's preset array at runtime with 9 new entries appended after the 17 vanilla presets - see `WeatherKind` in `custom_weather.h`). Exposes a hoshi settings menu to toggle which presets can appear.
- **`mods/custom_ai/`** - WIP scaffold. Custom AI mod covering two domains, each with its own preset selector (and a "Random" entry) in the hoshi settings menu: **enemy AI** (`enemy_ai.c` - Air Ride enemies: Aggressive, Item Hoarder, Coward, Erratic + Default) and **CPU rider AI** (`cpu_ai.c` - the AI opponents, e.g. City Trial's CPU players: Aggressive, Hoarder, Cautious, Reckless + Default). Currently only the menus and the per-domain tuning tables exist; the enemy-spawn / CPU-rider hooks that apply the presets are still TODO. `custom_ai.c` is the shared boot + random-roll helper. See `docs/enemy-ai-system.md` for the enemy AI internals.
- **`mods/custom_items/`** - Framework for adding new City Trial item kinds from drop-in HSD `.dat` archives. Scans the FST `items/` folder at boot (`FST_ForEachInFolder`), validates each archive's `customItem` public descriptor, and per round splices a new `ItemKind` into the engine's `itData[]` plus the box/sky and event-source spawn-weight tables (lifting the kind ceiling and clamping the instance's behavior kind to a vanilla `base_kind`). Models can be any item model carved from `Item.dat`, including the multi-texture/skinned legendary pieces. Exposes a `CustomItemsAPI` (header in `mods/custom_items/include/custom_items_api.h`) including a pickup handler other mods register to react to a custom item being collected. A build with no `.dat`s in `items/` installs no hooks and is inert. See `docs/custom-items.md`.
- **`mods/hypernova/`** - WIP. City Trial super-inhale power-up: while active, a powered-up Kirby grows ~2× and a held trigger vacuums items (collected via the vanilla pickup) and breakable yakumono (pulled in, shrunk, then broken through the game's own break path via a synthesized collision) out of a cone in front of the rider, plus a rainbow body/whirlwind recolor. Activation is **per player** (`Activate` powers up all humans, `ActivatePlayer` one slot); the `custom_items` "Miracle Fruit" grants it to its collector via a pickup handler. Inert until triggered, so a default build leaves vanilla play untouched. Exposes a `HypernovaAPI` (header in `mods/hypernova/include/hypernova_api.h`). See `docs/hypernova.md`.
- **`mods/textbox/`** - On-screen notification system. Queued, color-segmented messages with typewriter reveal, used by the archipelago mod for AP grants/losses, deathlink/traplink notifications, etc. Exposes a `TextBoxAPI` (header in `mods/textbox/include/textbox_api.h`) with `Enqueue`/`EnqueueSegments`/`EnqueueColoredNoun*` and a palette of color helpers (`PatchColor`, `MachineColor`, `ItemColor`, etc.).

## Build

```bash
make deploy
```

This runs `make package` (compile mod sources, link against hoshi, pack `.bin` files, copy assets, build the Riivolution payload under `out/Riivolution/`) and then copies `out/Riivolution/*` into Dolphin's `Load/Riivolution/` directory. Run `make clean` first only when needed (e.g. after modifying linker script, headers, or build configuration) - incremental rebuilds are the norm.

`make package` builds everything but does not touch Dolphin; use it when you only want to verify the build compiles. `make deploy` is the standard development command.

**Never run bare `make`** - always use `make deploy` (or `make package`). The default target does not produce the deployable Riivolution mod and will leave the build in an incomplete state.

**A successful `make deploy` (or `make package`) is sufficient verification.** The build fails on any compile or link error, so if it completes you do **not** need to grep its output for "error" or "warning". Pre-existing warnings in hoshi/devkitPPC headers are noise - don't re-run the build to inspect them.

**Source files are auto-discovered.** The Makefile globs every mod folder under `mods/` and recursively finds all `*.c` / `*.s` files in each mod's `src/` subdirectory. Adding a new `.c` file to `mods/<mod>/src/` (or anywhere beneath it) is enough - there is no manual file list to update. Same for adding a whole new mod: drop a folder with a `src/` subdir under `mods/` and it will be picked up. You do **not** need to grep the Makefile or check for explicit registration when adding source files. (The one exception: a new mod whose **public** header other mods `#include` needs its `include/` dir added to the Makefile's `INCLUDES` list - those are listed explicitly, not globbed.)

## Running Python Scripts

**Always use `uv run python` instead of bare `python` (or `python3`)** when invoking Python scripts in this repo (e.g. anything under `scripts/`). This ensures the project's managed environment and dependencies are used. Example: `uv run python scripts/hsd/explore.py ls iso/files/GrSpace2Model.dat` - never `python scripts/hsd/explore.py ...`.

## Project Structure

- `externals/hoshi/` - Hoshi modding framework (submodule). Contains headers (`include/`), linker script (`packtool/link.ld`), symbol map (`GKYE01.map`), and framework source (`src/`).
- `mods/archipelago/src/` - Archipelago mod C source code (the main mod). Public API header in `mods/archipelago/include/archipelago_api.h` (on the compiler include path).
- `mods/archipelago_debug/src/` - Debug menu mod C source code. Consumes `archipelago_api.h` via `Hoshi_ImportMod`.
- `mods/custom_events/src/` - Custom events mod C source code. Public API header in `mods/custom_events/include/custom_events_api.h` (added to compiler include path so other mods can `#include "custom_events_api.h"`).
- `mods/custom_weather/src/` - Custom weather mod C source code.
- `mods/custom_ai/src/` - Custom enemy AI mod C source code. Self-contained (no public API header); preset table and menu only so far.
- `mods/custom_items/src/` - Custom items mod C source code. Public API header in `mods/custom_items/include/custom_items_api.h` (added to compiler include path so other mods can `#include "custom_items_api.h"`).
- `mods/hypernova/src/` - Hypernova mod C source code. Public API header in `mods/hypernova/include/hypernova_api.h` (on the compiler include path).
- `mods/textbox/src/` - Textbox notification mod C source code. Public API header in `mods/textbox/include/textbox_api.h` (on the compiler include path).
- `docs/` - Documentation and reference data (checklist mappings, protocol docs). See "Documentation in `docs/`" below for the full index.
- `scripts/` - Build helpers (`iso.py`, `dol.py`, `build-devkitpro.sh` + `DevKitPro.dockerfile` for the toolchain image), reverse-engineering tools (`disasm.sh`, `findptr.sh`, `mem1.raw` memory dump), and the HSD `.dat` toolchain under `scripts/hsd/` - the `explore.py` CLI (ls / tree / find), the `carve_backdrop.py` + `carve_all_backdrops.py` backdrop-asset extractors, plus `probe_backdrops.py`, `verify_carved.py`, and `dump_lights.py`. See the `dat-explore` skill for the explorer and `docs/sky-backdrop-system.md` for the carving pipeline.
- `out/` - Build output directory. `out/files/` is the staged ISO files-overlay (mod `.bin`s + assets + hoshi outputs); `out/Riivolution/` is the deployable Riivolution package; `out/patch.xdelta` is produced by `make patch`. Do not hand-edit. Run `make deploy` to copy `out/Riivolution/*` into Dolphin's Load dir.
- `iso/` - Extracted contents of `kar.iso` (game filesystem) - `files/` (game data, audio, textures) and `sys/` (`main.dol`, `boot.bin`). Use for inspecting original game assets.

## Important Files - Do NOT Read Directly

Never attempt to read ISO files, memory dumps, or other large binary/text files directly. Always use appropriate tools or scripts.

- **`externals/hoshi/GKYE01.map`** - Symbol map file (~20k lines). Never read the full file. Search for specific symbols with grep when needed.
- **`scripts/mem1.raw`** - Dolphin memory dump (~24MB binary) captured at the **main menu**. It does not contain mod-specific memory locations or runtime state from gameplay. **Caveat: this snapshot appears to have been taken with hoshi loaded, not on a pure-vanilla game** - hoshi-rebuilt regions (e.g. the scene-desc table at `0x80495058` is all-`0xFF`, hoshi's `Scenes_CopyVanilla` memset-init) are modded in the dump. The bulk (vanilla code, static `.rodata` tables, vtables) is intact and verifies correctly, but when reverse-engineering a region hoshi patches/rebuilds, **verify against the source headers (`externals/hoshi/include/`), not this snapshot**. Never read this file directly. Use `scripts/disasm.sh` for targeted disassembly and `scripts/findptr.sh` for pointer searches.
- **`*.iso`** - ISO files are large binaries. Never read directly; use the provided scripts in `scripts/` or appropriate tools.
- **`iso/`** - Extracted ISO contents. Individual `.dat`, `.dol`, etc. files can be read or hex-dumped as needed, but avoid reading the directory wholesale.

## Documentation in `docs/`

Current-state reference material - engine/mod facts and design rationale, not the RE process or verification steps (see the comments/docs convention under Code Conventions).

- `checklist-mappings.csv` - Combined checklist mappings for all three modes (Air Ride, City Trial, Top Ride). Contains memory addresses, checkbox descriptions, and rewards. Primary reference for checklist clear kind indices and item IDs.
- `checklist-stat-tracking.md` - The `plclearcheckerlib` stat-measurement layer beneath the clear bits: the per-player stat struct (`Ply_GetItemCollectArray`), the verified item-collect subsystem (array at `+0x4c8`, increment/decrement, `Ply_GetItemCollectNum`/`Ply_GetItemCollectTotal`/`Ply_GetBoxCollectTotal`), and how `CityTrial_CheckForNewUnlocks` accumulates totals and fires the "pick up N items" / "break N boxes" / "get 10 of patch X" cells. Sister doc to `clearchecker-system.md`.
- `city-trial-event-system.md` - Full documentation of the City Trial event system: state machine, event function table, param/BGM/sky tables, reserve queue, selection logic.
- `clearchecker-system.md` - Documentation on the clear checker system.
- `client-game-protocol.md` - Protocol between the AP client and the game.
- `collision-system.md` - Map collision system (mpColl): CollData/mpCollInfo structs, item coll_kind dispatch, raycast/ground functions, Item_InitDesc parameters. (Entity-vs-entity HitColl is in `hurtdata-system.md`.)
- `cpu-ai-system.md` - CPU rider AI (the AI opponents in CT/AR/TR): the per-frame perceive→decide→process→emit pipeline, the virtual pad (`CpuData` at RiderData+0x778), the state-machine dispatch + command-stream, the `cAIPad`/`CommonDesireData`/`FormationPos` desire subsystem, and the tweak (`cpu_level`) vs replace (pad injection) strategies. The rider-opponent counterpart to `enemy-ai-system.md`.
- `css-system.md` - Character select screen system (all 3 modes), icon rendering pipeline, CharacterKind/MachineKind mapping, availability logic.
- `custom-events.md` - Custom events framework: registration, state machine wrappers, SIS text, weighted selection.
- `custom-hud.md` - Custom HUD element creation: GObj setup, JObj hierarchy, GX link rendering.
- `custom-items.md` - Custom items framework: drop-in `.dat` discovery (FST scan), the `customItem` descriptor contract, the per-round `itData[]`/spawn-weight engine splice (TODO), and the exported `CustomItemsAPI`. Builds on the vanilla item system (`item.h`).
- `deathlink.md` - DeathLink implementation details.
- `effects-system.md` - Model-effect system (the GObj+JObj visual-effect layer): `Effect_SpawnSync`, the decimal effect-ID encoding (`group*10000+entry`), `EffectModel_CreateGObj`, the effect manager (`gEffectMgr` 0x8055D7A0) + bank registry (`efGlobal` 0x8058C208), `EfCommon`/`EfPtcl*` bank loading, and the HSD_TExp/TEV color path with the in-place recolor recipe (`_HSD_TObjTev.constant`). Sister doc to `particle-system.md`.
- `enemy-ai-system.md` - Enemy AI: state machine, movement, targeting, per-type callbacks, EnemyData struct, custom AI integration.
- `enemy-spawn-system.md` - Enemy spawn system: actor IDs, spawn manager, GOBJProc priorities, EventActorDesc.
- `energylink.md` - EnergyLink: protocol fields, generation/spend flow, baseline gating, Auto-Charge, received-patch feedback handling, Top Ride tracking.
- `event-gravity-change.md` - Gravity Change custom event implementation.
- `event-scale-change.md` - Scale Change custom event implementation.
- `event-source-drops.md` - `event_source_drop[]` table reference: struct, field-to-source mapping, drop pipeline (`City_SpawnMiscItems` → `CityItem_GetEventItem`), source enum, yaku-break object families, and enumerated City Trial drop weights.
- `event-waddle-dee-swarm.md` - Waddle Dee Swarm custom event: standalone chase AI, the spline-snap workaround, func2/3/4 override, detection-range bypass, spawn/fade lifecycle.
- `gate-abilities.md` - Copy ability gating implementation.
- `gate-boxes.md` - Item box gating implementation.
- `gate-colors.md` - Color gating implementation.
- `gate-events.md` - Event gating implementation.
- `gate-items.md` - Item gating implementation.
- `gate-machines.md` - Machine/vehicle gating implementation.
- `gate-patches.md` - Patch gating implementation.
- `gate-stages.md` - Air Ride and Top Ride stage gating implementation.
- `gate-stadiums.md` - Stadium gating implementation: unlock check, selection replacement, history buffer fix.
- `gourmet-race-event.md` - Gourmet Race custom event: food spawning, scoring, HUD.
- `hurtdata-system.md` - HurtData/HitColl damage system: structs, per-frame pipeline, damage/knockback calculation, vulnerability states, enemy damage mechanics, how to apply damage from custom code.
- `hypernova.md` - Hypernova mod (City Trial power-up): reusing the vanilla inhale's presentation (animation/VFX/SFX) while the suction stays custom (the vanilla suction is EventActor-only), the custom cone-scan vacuum (items collected via the p_link-13 list; breakable yakumono pulled/shrunk/broken via a synthesized collision), the rainbow body + whirlwind recolor, and 2× scale via Big Kirby reuse.
- `kirby-model-scale.md` - Big/Small Kirby cosmetic filler: the per-object `model_scale` fields (RiderData+0x348, TopRideKirby+0x524) the engine applies to the model every frame, and the all-mode scaling implementation.
- `machine-charge-system.md` - Machine charge/boost system.
- `memcard-save-system.md` - Memory card / save system: the vanilla game save (`GCP_MemCard`) and hoshi's separate mod-data save (`externals/hoshi/src/save.c`), plus the card-manager tile (banner/icon/comment) format via `CARDStat`.
- `meteor-actor.md` - Meteor event actor (0x4E): state machine, standalone spawn, patches.
- `particle-system.md` - HSD point-particle pool (exhaust/sparkle layer): the 148-byte `Particle` struct, the 32 render-group bank array (0x8058cce8), `psRenderParticles`/`psDispParticles` render path, the 256-particle + generator free-lists, `ptclGen`/`PtclDesc` emission, and the per-particle color recolor lever. Sister doc to `effects-system.md`.
- `patch-cap.md` - Progressive patch cap system: replacement hooks, consumer coverage, how to raise the ceiling.
- `patch-drop-system.md` - Patch drop pipeline (`Rider_DropPatches` producer, `Rider_TickDropPatches` consumer): drop_mode 0/1/2 semantics, RiderData queue fields, stat→ItemKind table, used by `Patch_DropTrap`.
- `permanent-patches.md` - Permanent patch system: round-start re-application, all-up consolidation.
- `pluggable-mods-riivolution.md` - Design note (not yet implemented): making each hoshi mod an independently toggleable Riivolution option. The `/mods` FST scan as the on/off contract, target XML shape, separate-repos distribution model, and dependency/save caveats.
- `projectile-system.md` - Projectile system documentation.
- `scene-system.md` - Game scene/mode transition system.
- `sis-text-system.md` - SIS text system: commands, lookup tables, custom event text extension.
- `sky-backdrop-system.md` - Custom backdrop system for City Trial: ModelSection layout, the `3D_CreateStageModel` override hook, the carved donor asset toolchain (`scripts/hsd/carve_backdrop.py` + friends), and the gr_kind / stage filename table. Sister doc to sky-lighting-system.md.
- `sky-lighting-system.md` - Sky preset and lighting transition system.
- `spawn-rate.md` - Spawn Rate Up item: CT timer + cap scaling, TR probability scaling, scale cap rationale.
- `textbox.md` - Textbox notification system: multi-segment colored-noun rendering, typewriter seeding, scene-change rebuild/persistence, pre-first-scene canvas guard, corner-anchored stacking, Top Ride re-render.
- `topride-item-system.md` - Top Ride item system: item bitmask, ability-themed items, gating hooks.
- `topride-kirby-states.md` - Top Ride Kirby state system: `state_handler` polymorphic slot at `+0x7C`, transition pipeline, and the full state set (Explode/Crush/Press/etc.) for invocation from mod code (deathlink, traplink, custom traps).
- `topride-system.md` - Top Ride mode: KirbyMgr, charge component, cpu_level encoding.
- `traplink-send.md` - TrapLink send/receive implementation.
- `yakumono-system.md` - Yakumono (game stage objects) system documentation.

## Reverse Engineering Workflow

When reverse engineering game functions and discovering their purpose, **always** update the following files with findings before finishing:

1. **`externals/hoshi/include/`** - Add/update function declarations and data structure definitions in the appropriate header files (typically `game.h`). Include the address in a comment.
2. **`externals/hoshi/packtool/link.ld`** - Add symbol addresses for newly identified functions so they can be called from mod code. Data globals (r13-relative/SDA addresses) do not go here - declare them as `static` pointer casts in headers instead (see `event.h` or `game.h` for examples).
3. **`externals/hoshi/GKYE01.map`** - Rename unnamed symbols (`zz_XXXXXXXX_`) at their addresses to the discovered names.
4. **Ghidra:** Use the `ghidra-cli` skill to rename the corresponding functions in the Ghidra project to keep it in sync.

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
- Hoshi framework headers (`externals/hoshi/include/`) use traditional `#ifndef`/`#define`/`#endif` include guards. Newer headers use a `KAR_H_<NAME>` guard macro; a number of older ones still use the legacy `MEX_H_<NAME>` prefix (carried over from the m-ex framework) - prefer `KAR_H_*` for new headers. Mod headers (anywhere under `mods/*/src/` or `mods/*/include/`) also use `#ifndef`/`#define`/`#endif` guards. The mod public API headers (`archipelago_api.h`, `custom_events_api.h`, `textbox_api.h`, `hypernova_api.h`) sit on the global include path and are consumed by other mods, but use the same plain name-based `#ifndef` guards.
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
