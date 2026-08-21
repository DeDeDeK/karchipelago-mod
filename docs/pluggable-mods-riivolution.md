# Pluggable Mods via Riivolution Options

Design note for a package layout that is not built yet. The shipping build produces a single
all-or-nothing Riivolution package: one "Enabled" choice that applies every mod at once. This
doc captures how to turn each hoshi mod into an independently toggleable Riivolution option,
and the constraints to plan around.

## Why This Is Feasible

hoshi does not hard-code which mods exist. At boot it calls
`FST_ForEachInFolder("/mods", ".bin", ...)` twice (`externals/hoshi/src/hoshi.c`) - once with
`Mods_CountFile` to size the loader table, then again with `Mods_LoadGlobal`, which reads each
file (`Mods_LoadFile` -> `DVDFastOpen` / `File_LoadOffsetSync`), relocates it and installs it.
A mod is loaded iff its `.bin` is present in `/mods` at scan time; an absent `.bin` is simply
skipped. There is no manifest and no fixed list - presence in the FST is the entire contract.

Riivolution file-replacement patches add files to the in-memory FST that the game reads. So
gating a mod's `.bin` behind a Riivolution `<option>`/`<choice>` *is* the on/off switch:
enabling the option adds the `.bin` to the FST, the boot scan finds it, and the mod loads. No
changes to the loader are required.

The build-time counterpart already exists. The Makefile's `INCLUDE_MODS` lists the mod folders
to compile and pack, and it has **no default** - a build that omits it silently produces a
package with no mods in it (`make package INCLUDE_MODS=archipelago,textbox`; names that do not
match a folder under `mods/` are dropped without a warning). It gates each mod's `assets/` copy
as well as its code. Sources are globbed from each included mod's `src/`, so adding a file - or
a whole new mod folder - needs no registration; the one manual step for a new mod is adding its
public `include/` dir to the Makefile's `INCLUDES` list, since those are enumerated explicitly.
A per-mod Riivolution option is the runtime version of the same choice: ship every `.bin` and
let the player pick in Dolphin's Riivolution menu. The current candidates are `ap_star`,
`archipelago`, `archipelago_debug`, `custom_ai`, `custom_checklist`, `custom_events`,
`custom_items`, `custom_machines`, `custom_weather`, `hypernova` and `textbox`.

## How the Current Package Is Structured

The generated XML (`out/Riivolution/riivolution/KARchipelago.xml`, copied verbatim from
`externals/hoshi/dol/out/Riivolution/` by the Makefile's `riivolution` target) exposes one
section (`Mod`) with one option (`KARchipelago`) whose single `Enabled` choice applies both
patches:

- **`code_patch`** - `<memory valuefile="/KARchipelago/payload.bin" offset="0x805f6390"/>` plus
  the memory hooks that redirect game execution into the loader. This is the framework
  bootstrap and is **mandatory**: without it the game boots vanilla and nothing scans `/mods`.
- **`files`** - a single recursive folder patch that maps the entire `/KARchipelago` payload
  onto the disc at once: `hoshi.bin`, `MnSettings.dat`, `MxDb.dat`, every `mods/*.bin`, and all
  assets.

```xml
<patch id="files">
    <folder external="/KARchipelago" recursive="true" />
    <folder external="/KARchipelago" disc="/" create="true" recursive="true" />
</patch>
```

`external` paths resolve under `Load/Riivolution/` (so `/KARchipelago/...` is
`Load/Riivolution/KARchipelago/...`, where `make deploy` stages them). `disc` paths are
relative to the game's FST root - `/KARchipelago/mods/archipelago.bin` lands at
`/mods/archipelago.bin`, `/KARchipelago/ApIcon.dat` at `/ApIcon.dat`, etc.

To make mods pluggable, split that one `files` patch into a mandatory **core** patch plus one
patch per mod, each behind its own option.

## Target XML Shape

```xml
<options>
  <section name="KARchipelago">
    <option name="Core (required)">
      <choice name="Enabled"><patch id="core"/></choice>
    </option>
    <option name="Archipelago">
      <choice name="Enabled">
        <patch id="mod-archipelago"/>
        <patch id="mod-textbox"/>   <!-- bundled dependency -->
      </choice>
    </option>
    <option name="Custom AI">
      <choice name="Enabled"><patch id="mod-custom_ai"/></choice>
    </option>
  </section>
</options>

<patch id="core">
  <memory valuefile="/KARchipelago/payload.bin" offset="0x805f6390"/>
  <!-- ... the remaining code_patch <memory> entries, verbatim ... -->
  <file external="/KARchipelago/hoshi.bin"      disc="/hoshi.bin"      create="true"/>
  <file external="/KARchipelago/MnSettings.dat" disc="/MnSettings.dat" create="true"/>
  <file external="/KARchipelago/MxDb.dat"       disc="/MxDb.dat"       create="true"/>
</patch>

<patch id="mod-archipelago">
  <file external="/KARchipelago/mods/archipelago.bin" disc="/mods/archipelago.bin" create="true"/>
  <file external="/KARchipelago/ApIcon.dat"           disc="/ApIcon.dat"           create="true"/>
</patch>
```

`create="true"` is required because `/mods/*.bin` (and the other added files) do not exist on
the original disc - Riivolution creates the FST entry. This mirrors the `create="true"` already
on the current folder patch.

## Separate-Repos Distribution Model

The end goal (splitting non-AP mods into their own repos) maps cleanly onto Riivolution because
Dolphin loads **every** `*.xml` in `Load/Riivolution/`, and multiple XMLs may target the same
`<id game="GKY"/>` - each shows up as its own toggle in the Riivolution config UI.

- **Core repo** ships `KARchipelago-core.xml` (the `code_patch` memory hooks + `hoshi.bin` /
  `MnSettings.dat` / `MxDb.dat`) and its payload folder. Required for anything to load.
- **Each mod repo** ships a small XML that only adds `/mods/<mod>.bin` (+ that mod's assets)
  plus its built `.bin`. Drop-in, independently toggled.

No cross-XML conflicts arise because each mod only *adds* a distinct `/mods/<name>.bin`; nobody
overwrites the same disc file.

Build-side coupling for a standalone mod repo: it still needs hoshi (headers, `Lib/`,
`packtool/link.ld`, `GKYE01.map`) to compile and pack its `.bin`, plus the public API header of
any mod it imports (e.g. a standalone `archipelago` repo needs `textbox_api.h`). Those shared
API headers are the real coupling surface between split repos.

## Constraints

**Core is non-optional.** The `code_patch` memory hooks + `payload.bin` are the loader itself.
Present it as a clearly-required option (or fold it into a base the player must enable).
Disabling it = vanilla game, no `/mods` scan.

**Inter-mod dependencies are real and only partly guarded.** Mods publish and consume APIs via
`Hoshi_ExportMod` / `Hoshi_ImportMod`, matched on `ModDesc.name`; `_Hoshi_ImportMod`
(`externals/hoshi/src/export.c`) returns `0` when the requested mod is absent, so an import
never faults on its own. Whether that is survivable depends on the consumer:

- `archipelago` dereferences the textbox API at ~90 call sites with no NULL check at any of
  them - only at the import in `OnSaveLoaded`, which logs a warning and continues. "Archipelago
  on, Textbox off" crashes on the first notification.
- Its other imports degrade cleanly: `custom_machines` (`AP_ResolveCustomMachines` in
  `mods/archipelago/src/main.c`) leaves machines ungated, and `ap_star`
  (`GateApStar_Resolve` in `mods/archipelago/src/gate_ap_star.c`) simply installs no assemble
  handler. `archipelago_debug` guards all three of its imports the same way.

Two ways to handle textbox: bundle it into the Archipelago choice (as in the XML above; ships
today with no code change), or NULL-guard `tb_api` at its uses so the dependency becomes
genuinely optional.

**Assets travel with their mod, not core.** `ApIcon.dat` belongs to the `archipelago` patch,
`ApStarShot.dat` to `ap_star`. Since each mod's `assets/` folder already stages its own files,
a per-mod patch can map one `<folder>` instead of enumerating files - but note the staging
flattens every mod's `assets/` into the same disc root, so the folder mapping has to be built
per file unless the on-disc layout is given per-mod subdirectories too.

**Boot order and deferred imports are already handled.** Mods boot in alphabetical order, so
`archipelago` boots before `textbox` and defers its `Hoshi_ImportMod` to `OnSaveLoaded` - the
first point past every mod's `OnBoot`, and therefore the first point where a failed import
really means the mod is absent. Splitting into options does not change ordering, but any new
exporter/consumer pair must keep imports out of `OnBoot`.

**Save data survives toggling.** Each mod's blob is located in the card save by a 32-bit hash of
`ModDesc.name` (`KARPlusSave_Alloc` / `KARPlusSave_CheckModDataExists` in
`externals/hoshi/src/save.c`), not by position, and a mod that is not loaded leaves its metadata
entry untouched. Turning a mod off and back on across sessions keeps its data. Data *offsets*
are still packed end-to-end, so a mod whose save grows between versions forces
`KARPlusSave_VerifySize` to memmove every later mod's region up; that path is what breaks first
if the total exceeds `SAVE_SIZE`.

**Settings menu is rebuilt per boot.** hoshi assembles the settings menu at runtime from each
active mod's `OptionDesc` and sorts it (`externals/hoshi/src/settings.c`), so the option list
changes shape whenever the active set changes. Menu-save slots are per mod, so entries follow
their mod, but cursor/index persistence across a changed roster is worth checking on hardware.
