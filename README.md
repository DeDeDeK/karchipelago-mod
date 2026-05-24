# karchipelago-mod

This is the mod portion of the [Kirby Air Ride APWorld (KARchipelago)](https://github.com/DeDeDeK/KARchipelago) for [Archipelago](https://archipelago.gg/). 

This repository is based off of a [**template project**](https://github.com/UnclePunch/hoshi-mod) for building a Kirby Air Ride mod using the [Hoshi](https://github.com/UnclePunch/hoshi) modding framework.

This hosts the code for interfacing with the game via code hooks and other methods of accessing the game provided by hoshi. The goal is to provide the APWorld client with the needed game state to function. 

# Build Instructions

This project is written in **C** and uses `make` to manage the build process. It outputs `.bin` (code) and `.dat` (asset) files and can be built on **Windows**, **macOS**, and **Linux**.

---

## Prerequisites

### 1. **[devkitPPC](https://devkitpro.org/wiki/Getting_Started)**

This contains the required PowerPC compiler. Install devkitPro and place (or symlink) it at `externals/devkitpro/` so the Makefile can find it automatically. Alternatively, you can set the `DEVKITPPC` environment variable to point to your installation.

### 2. **[uv](https://docs.astral.sh/uv/getting-started/installation/)**

This project uses [uv](https://docs.astral.sh/uv/) to manage Python dependencies (`pyelftools`, `pyisotools`). Python scripts are invoked via `uv run`, so no manual dependency installation is needed — `uv` handles it automatically.

### 3. **[trash-cli](https://github.com/andreafrancia/trash-cli)** (Linux only, optional)

The `make clean` target uses `trash-put` to safely remove Dolphin Riivolution and memory card files instead of permanently deleting them.

### 3. **Original NTSC Kirby Air Ride ISO**

Place your unmodified Kirby Air Ride .iso file in the root directory of this repo and name it "kar.iso".

---

## Building the Project

Navigate to the root folder of the project and run:

```bash
make package
```

This will:

* Compile all mod's source files.
* Pack each mod into a hoshi compatible `.bin` file using the Python script.
* Output a Riivolution mod under `out/Riivolution/`.

To copy `out/Riivolution/*` into Dolphin's `Load/Riivolution/` directory, run `make deploy`. Override the destination with `DOLPHIN_RIIVOLUTION_DIR=...` if Dolphin lives elsewhere on your system.

To produce an XDelta patch against `kar.iso` for a one-off ISO build, run `make patch` instead.

---

## License

KARchipelago is distributed under the **GNU General Public License v3** —
see [`LICENSE`](LICENSE).

The mod links against [hoshi](https://github.com/UnclePunch/hoshi) (also
GPLv3, vendored as a submodule under `externals/hoshi/`), which is why
the project as a whole is GPLv3.

Several Python helpers under `scripts/hsd/` are ports of code from
[HSDLib](https://github.com/Ploaj/HSDLib) (MIT, © 2021 Ploaj). HSDLib's
MIT license text is preserved verbatim in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and each ported file
carries an attribution header.