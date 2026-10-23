# Mod Loading

How hoshi gets into the game, finds the mods on the disc and installs them, and how mods reach each
other's APIs. It ends with a plan, not built yet, for shipping each mod as its own Riivolution
option. A modder needs this to know when their `OnBoot` runs relative to other mods, what an
import can and cannot see, and what "the mod is installed" means.

## Boot Chain

The Riivolution package carries two patches. The code patch writes `payload.bin` to 0x805f6390 and a
few hooks that send game execution into it; `files` maps the package folder onto the disc root.
The payload loads `hoshi.bin` from the disc, relocates it and calls hoshi's `OnFileLoad`. Without
the code patch the game boots vanilla and nothing ever looks at the mods.

`OnFileLoad` first swaps `HSD_MemAlloc` (and the archive loader) for a persistent bump allocator,
so everything allocated during boot - hoshi's own tables and every mod's `OnBoot` allocations -
lives for the whole session. It installs hoshi's subsystems (scene table relocation, preload,
exports, the screen camera, the save file), then installs the mods, then patches in the lifecycle
callback hooks, builds the settings menu, and restores the normal allocator.

## Installing Mods

hoshi has no list of mods. It calls `FST_ForEachInFolder("/mods", ".bin", ...)` twice: once to
count the files and size its table, once to install each. A mod is installed if and only if its
`.bin` is in the disc's `/mods` folder at boot.

For each file, `Mods_LoadGlobal`:

1. Reads it into the persistent heap and checks its header `version` against hoshi's
   `HOSHI_VERSION`. A mismatch prints which side needs updating and asserts: every `.bin` is
   built against one hoshi version.
2. Relocates it and frees the relocation table.
3. Takes the mod's `ModDesc`, records it, and runs `OnBoot` with the mod marked as the one being
   installed.
4. Counts the mod as installed only after `OnBoot` returns.

Mods install in the order the folder lists them, which is alphabetical by file name. After the
last one hoshi grows each of the game's preload heaps by the size of the files mods registered for
it with `Hoshi_AddPreloadGameFile`.

## ModDesc

| Field | Meaning |
|---|---|
| `name` | Identity: the import key and, hashed, the save-block key |
| `author` | Shown in the boot log |
| `version.major` / `.minor` | Checked by importers; a mod that exports an API sets it to that API's version |
| `save_size` | Bytes of the mod's own save struct; 0 for none |
| `save_ptr` | Written by hoshi: the mod's save struct |
| `affects_gameplay` | Includes the mod's save in hoshi's save backup and restore |
| `option_desc` | Root of the mod's settings-menu tree, or NULL |
| `OnBoot` ... `OnTopRideLoadEnd` | Lifecycle callbacks, each NULL or a function hoshi calls at a fixed point |

## Exports and Imports

A mod publishes an API by calling `Hoshi_ExportMod(ptr)` in its `OnBoot`; hoshi stores the pointer
against the mod being installed. Another mod resolves it with
`Hoshi_ImportMod(name, major, minor)`, which searches the mods installed so far by `ModDesc.name`:

- Found, but the importer asked for an older major than the installed mod's: hoshi prints both
  versions and asserts.
- Found: the exported pointer.
- Not found: NULL, with a warning. The minor version is not checked.

Because a mod counts as installed only once its `OnBoot` returns, an import made from `OnBoot`
sees only mods earlier in the folder. `OnSaveLoaded` runs after every mod's `OnBoot`, so imports
belong there, and a NULL there means the mod is not in the build. The consumer decides whether
that is survivable: either it degrades (skips the feature, or points the API at a stub whose calls
do nothing) or it checks for NULL at each use.

## Plan: One Riivolution Option per Mod

The package today is all-or-nothing: one option, `KARchipelago`, whose single `Enabled` choice
applies the code patch and a `files` patch that maps the whole package folder onto the disc root:

```xml
<patch id="files">
    <folder external="/KARchipelago" recursive="true" />
    <folder external="/KARchipelago" disc="/" create="true" recursive="true" />
</patch>
```

Since presence in `/mods` is the whole install contract, putting each `.bin` behind its own
Riivolution option makes each mod a toggle in Dolphin's Riivolution menu, with no loader change.
The `files` patch splits into a mandatory core patch and one patch per mod:

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

`external` paths resolve under Dolphin's `Load/Riivolution/`; `disc` paths are relative to the
game's file-table root. `create="true"` is required because none of these files exist on the
original disc.

The same shape supports splitting mods into separate repositories. Dolphin loads every XML in
`Load/Riivolution/`, and several may target the same game, so a core package ships the loader and
hoshi's files and each mod repository ships a small XML that adds only its own `.bin` in `/mods`
and assets. No two packages write the same disc file, so they cannot conflict. A standalone mod
still needs hoshi's headers, linker script and symbol map to build, plus the public API header of
every mod it imports; those headers are the real coupling between repositories.

Constraints to plan around:

- **Core is not optional.** Without the code patch and `payload.bin` nothing scans `/mods`; the core
  option has to be presented as required.
- **Dependencies are partly guarded.** An import never faults on its own, but each consumer must
  survive a NULL import, and an option can bundle a hard dependency, as above.
- **Assets travel with their mod.** Every mod's assets are staged into one shared disc tree, so
  per-mod patches have to list files one by one unless each mod gets its own folder on the disc.
- **Save data survives toggling.** A mod's save block is found by a hash of its name, and the
  block of a mod that is not installed stays in the file untouched.
- **The settings menu follows the installed set.** It is rebuilt every boot from the installed
  mods' `option_desc`, and each mod's saved values are keyed to that mod.
