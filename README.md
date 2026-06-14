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

## What the mod provides to the AP client

The mod and the Python AP client (shipped with the [APWorld](https://github.com/DeDeDeK/KARchipelago)) communicate through a shared memory block. On boot the mod allocates an `APData` struct and publishes a pointer to it at the static address `0x805d52d4`; the client reads/writes that struct with `dolphin-memory-engine`. Through this interface the mod:

- **Reports location checks** - checklist clears across all three modes (Air Ride, City Trial, Top Ride) are surfaced to the client as AP locations.
- **Receives and applies items** - the client delivers item IDs; the mod unlocks the corresponding gated content (machines, abilities, colors, stadiums, events, items/patches/boxes, courses) or applies filler/trap/goal effects in-game.
- **Bridges the link protocols** - DeathLink, EnergyLink, and TrapLink each have mailbox fields the two sides exchange.
- **Exchanges slot options, goal state, and a handshake** - the client writes the slot's options and location layout, the mod signals when it's ready and reports goal completion.

The client owns the multiworld networking; the mod owns everything that happens inside the game. The canonical field layout is the `APData` struct in `mods/archipelago/src/main.h`; the protocol is documented in `docs/client-game-protocol.md`.

## Mods in this repository

The build packages every mod found under `mods/` into a single Riivolution package (see [Excluding mods](#excluding-mods) below).

**Shipped by default:**

- **`archipelago`** - The main mod. All Archipelago integration: gating systems, checklist rewards, DeathLink/EnergyLink/TrapLink, goals, and the shared-memory interface above. Exposes an `ArchipelagoAPI`.
- **`textbox`** - On-screen notification system (queued, color-segmented messages) used by the archipelago mod for grant/loss, DeathLink, and TrapLink notices. Exposes a `TextBoxAPI`.

**Work in progress (excluded from the default build):**

- **`custom_events`** - Custom City Trial event framework (e.g. Waddle Dee Swarm, Gravity Change, Scale Change, Gourmet Race). Not yet wired into the archipelago mod.
- **`custom_weather`** - Adds custom sky/lighting presets to City Trial.

A standalone **custom stadiums** mod is also planned; for now, stadium unlocking/gating lives inside the `archipelago` mod rather than as a separate module.

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

### Excluding mods

By default the WIP mods are left out of the build. This is controlled by `EXCLUDE_MODS` (comma- or space-separated mod folder names):

```bash
# default - drops the WIP mods
EXCLUDE_MODS ?= custom_events,custom_weather
```

Override it on the command line:

```bash
make package EXCLUDE_MODS=                      # build everything, including WIP mods
make package EXCLUDE_MODS=custom_events,textbox # drop additional mods
```

### One-off ISO patch

To produce an XDelta patch against `kar.iso` instead of a Riivolution package:

```bash
make patch
```

The patch is written to `out/patch.xdelta`.

---

## License

karchipelago-mod is distributed under the **GNU General Public License v3** - see [`LICENSE`](LICENSE).

The mod links against [hoshi](https://github.com/UnclePunch/hoshi) (also GPLv3, vendored as a submodule under `externals/hoshi/`)

Several Python helpers under `scripts/hsd/` are ports of code from [HSDLib](https://github.com/Ploaj/HSDLib) (MIT, © 2021 Ploaj). HSDLib's MIT license text is preserved verbatim in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and each ported file carries an attribution header.