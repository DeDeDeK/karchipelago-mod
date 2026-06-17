# Pluggable Mods via Riivolution Options

> **Status: design note, not yet implemented.** The shipping build still produces a single all-or-nothing Riivolution package (one "Enabled" choice that applies every mod at once). This doc captures how to turn each hoshi mod into an independently toggleable Riivolution option, and the constraints to plan around. Nothing here is wired up yet.

## Why this is feasible

hoshi does **not** hard-code which mods exist. At boot it discovers mods by scanning the disc filesystem for `.bin` files under `/mods`:

```c
// externals/hoshi/src/hoshi.c (~280, ~292)
FST_ForEachInFolder("/mods", ".bin", 0, (void (*)(int, void *))Mods_CountFile, &mod_num);
...
FST_ForEachInFolder("/mods", ".bin", 0, (void (*)(int, void *))Mods_LoadGlobal, 0);
```

Each `.bin` found is read (`Mods_LoadFile` → `DVDFastOpen` / `File_LoadOffsetSync`) and installed (`Mods_LoadGlobal`). The consequence: **a mod is loaded iff its `.bin` is present in `/mods` at scan time; an absent `.bin` is simply skipped.** There is no manifest and no fixed list — presence in the FST is the entire contract.

Riivolution file-replacement patches add files to the in-memory FST that the game reads. So gating a mod's `.bin` behind a Riivolution `<option>/<choice>` *is* the on/off switch: enabling the option adds the `.bin` to the FST, the boot scan finds it, and the mod loads. No changes to the loader are required.

This also means the existing build-time `EXCLUDE_MODS` knob (which mod `.bin`s get built) has a natural runtime counterpart: ship all the `.bin`s and let the player choose per mod in Dolphin's Riivolution menu.

## How the current package is structured

The generated XML (`out/Riivolution/riivolution/KARchipelago.xml`, copied from `externals/hoshi/dol/out/Riivolution/...`) puts everything under one choice:

- **`code_patch`** — `<memory valuefile=".../payload.bin" offset="0x805f6390"/>` plus the memory hooks that redirect game execution into the loader. This is the framework bootstrap and is **mandatory**: without it the game boots vanilla and nothing scans `/mods`.
- **`files`** — a single recursive folder patch that maps the entire `/KARchipelago` payload onto the disc at once: `hoshi.bin`, `MnSettings.dat`, `MxDb.dat`, every `mods/*.bin`, and all assets.

```xml
<patch id="files">
    <folder external="/KARchipelago" recursive="true" />
    <folder external="/KARchipelago" disc="/" create="true" recursive="true" />
</patch>
```

`external` paths resolve under `Load/Riivolution/` (so `/KARchipelago/...` is `Load/Riivolution/KARchipelago/...`, where `make deploy` stages them). `disc` paths are relative to the game's FST root — `/KARchipelago/mods/archipelago.bin` lands at `/mods/archipelago.bin`, `/KARchipelago/ap-icon.png` at `/ap-icon.png`, etc.

To make mods pluggable, split that one `files` patch into a mandatory **core** patch plus one patch per mod, each behind its own option.

## Target XML shape

```xml
<wiidisc version="1">
  <id game="GKY"/>
  <options>
    <section name="KARchipelago">
      <option name="Core (required)">
        <choice name="Enabled"><patch id="core"/></choice>
      </option>
      <option name="Archipelago">
        <choice name="Enabled">
          <patch id="mod-archipelago"/>
          <patch id="mod-textbox"/>   <!-- bundled dependency; see Caveats -->
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
    <file external="/KARchipelago/ap-icon.png"          disc="/ap-icon.png"          create="true"/>
  </patch>
  <patch id="mod-textbox">
    <file external="/KARchipelago/mods/textbox.bin" disc="/mods/textbox.bin" create="true"/>
  </patch>
  <patch id="mod-custom_ai">
    <file external="/KARchipelago/mods/custom_ai.bin" disc="/mods/custom_ai.bin" create="true"/>
  </patch>
</wiidisc>
```

`create="true"` is required because `/mods/*.bin` (and the other added files) do not exist on the original disc — Riivolution creates the FST entry. This mirrors the `create="true"` already on the current folder patch.

## Separate-repos distribution model

The end goal (splitting non-AP mods into their own repos) maps cleanly onto Riivolution because Dolphin loads **every** `*.xml` in `Load/Riivolution/`, and multiple XMLs may target the same `<id game="GKY"/>` — each shows up as its own toggle in the Riivolution config UI.

- **Core repo** ships `KARchipelago-core.xml` (the `code_patch` memory hooks + `hoshi.bin` / `MnSettings.dat` / `MxDb.dat`) and its payload folder. Required for anything to load.
- **Each mod repo** ships a small XML that only adds `/mods/<mod>.bin` (+ that mod's assets) plus its built `.bin`. Drop-in, independently toggled.

No cross-XML conflicts arise because each mod only *adds* a distinct `/mods/<name>.bin`; nobody overwrites the same disc file.

Build-side coupling for a standalone mod repo: it still needs hoshi (headers, `Lib/`, `packtool/link.ld`, `GKYE01.map`) to compile and pack its `.bin`, plus the public API header of any mod it imports (e.g. a standalone `archipelago` repo needs `textbox_api.h`). Those shared API headers are the real coupling surface between split repos.

## Caveats

1. **Core is non-optional.** The `code_patch` memory hooks + `payload.bin` are the loader itself. Present it as a clearly-required option (or fold it into a base the player must enable). Disabling it = vanilla game, no `/mods` scan.

2. **Inter-mod dependencies are real and currently unguarded.** Mods publish/consume APIs via `Hoshi_ExportMod` / `Hoshi_ImportMod` (matched by `ModDesc.name`). `_Hoshi_ImportMod` returns `0` (NULL) gracefully when the requested mod is absent — but consumers do not all check the result:
   - `archipelago` dereferences the textbox API (`tb_api->...`) in ~87 places with **zero** NULL guards. "Archipelago on, Textbox off" therefore crashes on the first notification.
   - `archipelago_debug` imports `ArchipelagoAPI` + `CustomEventsAPI` the same way.

   Two ways to handle it:
   - **Bundle the dependency** into the dependent's choice (the `mod-textbox` patch listed under the Archipelago option above). Simplest; ships today with no code change.
   - **Harden the consumer** to NULL-guard `tb_api` / `ce_api` so the dependency becomes genuinely optional. More work, but then the mods are truly independent plugins.

3. **Assets travel with their mod, not core.** `ap-icon.png` belongs to the `archipelago` patch. Giving each mod its own asset subfolder lets the per-mod patch map a single `<folder>` instead of enumerating files.

4. **Boot order / deferred imports already handled.** Mods boot in alphabetical order. `archipelago` boots before `textbox`, so it defers its `Hoshi_ImportMod` to `OnSaveLoaded` (by which point every mod has exported its API) rather than calling it in `OnBoot`. Splitting into options does not change ordering, but any new exporter/consumer pair must keep imports out of `OnBoot`.

## Open questions

- **Per-mod save data across toggles.** Each mod registers its own save blob via its `GlobalMod`/`ModDesc`. If the save layout is keyed by mod name, toggling a mod off then on across sessions is safe; if it is positional, disabling one mod could shift another mod's data. Not yet confirmed — check `externals/hoshi/src/save.*` (e.g. `KARPlusSave_GetModSaveData`, `Mods_SetDefaultSaveData`, `Mods_OnLoadSaveData`) before relying on free toggling.
- **Settings-menu stability.** hoshi builds the settings menu at runtime from each active mod's `OptionDesc` (`externals/hoshi/src/settings.c`). Confirm that menu cursor/index persistence behaves when the set of active mods changes between boots.
