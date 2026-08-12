<img src="art/karchipelago-logo.png" alt="karchipelago logo" width="400"/>

# karchipelago-mod

This is the mod portion of the [Kirby Air Ride APWorld (KARchipelago)](https://github.com/DeDeDeK/KARchipelago) for [Archipelago](https://archipelago.gg/).

This repository hosts the native GameCube mod that interfaces with Kirby Air Ride (NTSC, `GKYE01`) through code hooks and other facilities provided by the [Hoshi](https://github.com/UnclePunch/hoshi) modding framework. Its job is to expose the game state the APWorld client needs to drive a multiworld, and to apply the game-side effects (item gating, rewards, DeathLink/EnergyLink/TrapLink, etc.).

The repo is based on the [hoshi-mod template project](https://github.com/UnclePunch/hoshi-mod).

## Getting the mod

- **Latest mod release:** https://github.com/DeDeDeK/karchipelago-mod/releases/latest
- **APWorld (Archipelago integration):** https://github.com/DeDeDeK/KARchipelago
- **Setup guide (full player walkthrough):** https://github.com/DeDeDeK/KARchipelago/blob/main/worlds/kirby_air_ride/docs/setup_en.md

## Installing the mod (Riivolution)

See the [setup guide](https://github.com/DeDeDeK/KARchipelago/blob/main/worlds/kirby_air_ride/docs/setup_en.md) for the complete walkthrough, including installing the APWorld and connecting the client.

## Build Instructions

This project is written in **C** and uses `make`. It outputs `.bin` (code) and `.dat` (asset) files and can be built on **Windows**, **macOS**, and **Linux**.

### Prerequisites

1. **[devkitPPC](https://devkitpro.org/wiki/Getting_Started)** - the PowerPC cross-compiler. Install devkitPro and place (or symlink) it at `externals/devkitpro/` so the Makefile finds it automatically, or set the `DEVKITPPC` environment variable to point at your installation.
2. **[uv](https://docs.astral.sh/uv/getting-started/installation/)** - manages the Python build dependencies (`pyelftools`, `pyisotools`). Scripts are invoked via `uv run`, so no manual dependency install is needed.
3. **Original NTSC Kirby Air Ride ISO** - place your unmodified disc image in the repo root, named `kar.iso`. (The build extracts the original DOL from it.)
4. **[trash-cli](https://github.com/andreafrancia/trash-cli)** *(Linux only, optional)* - `make clean` uses `trash-put` to move Dolphin Riivolution/memory-card files to the trash instead of deleting them.

### Building

From the repo root:

```bash
make package
```

This will:

- Compile every mod's source files,
- Link each mod against hoshi and pack it into a hoshi-compatible `.bin`,
- Copy in assets and the hoshi payload, and
- Produce the deployable Riivolution package under `out/Riivolution/`.

Source files are auto-discovered: the Makefile globs every folder under `mods/` and recursively finds all `*.c` / `*.s` under each mod's `src/`. Adding a file (or a whole new mod folder with a `src/` subdir) needs no manifest edits.

### Deploying to Dolphin

```bash
make deploy
```

`make deploy` runs `make package` and then copies `out/Riivolution/*` into Dolphin's `Load/Riivolution/` directory. Override the destination if Dolphin lives elsewhere:

```bash
make deploy DOLPHIN_RIIVOLUTION_DIR=/path/to/Load/Riivolution
```

### Selecting mods

Nothing is built by default. Choose which mod folders to build with `INCLUDE_MODS` (comma- or space-separated mod folder names):

```bash
# default - builds nothing
INCLUDE_MODS ?=
```

Set it on the command line:

```bash
make package INCLUDE_MODS=archipelago,textbox  # build just these two
make package INCLUDE_MODS=archipelago,textbox,hypernova,custom_items,custom_checklist,custom_ai
```

Names not present under `mods/` are ignored.

### One-off ISO patch

To produce an XDelta patch against `kar.iso` instead of a Riivolution package:

```bash
make patch
```

The patch is written to `out/patch.xdelta`.


## Credits

- Swiggity - karchipelago logo design
- Taco - KAR Deluxe logo + font design

---

## License

karchipelago-mod is distributed under the **GNU General Public License v3** - see [`LICENSE`](LICENSE).

The mod links against [hoshi](https://github.com/UnclePunch/hoshi) (also GPLv3, vendored as a submodule under `externals/hoshi/`)

Several Python helpers under `scripts/hsd/` are ports of code from [HSDLib](https://github.com/Ploaj/HSDLib) (MIT, © 2021 Ploaj). HSDLib's MIT license text is preserved verbatim in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and each ported file carries an attribution header.