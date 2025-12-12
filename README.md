# karchipelago-mod

This is the mod portion of the [Kirby Air Ride APWorld (KARchipelago)](https://github.com/DeDeDeK/KARchipelago) for [Archipelago](https://archipelago.gg/). 

This repository is based off of a [**template project**](https://github.com/UnclePunch/hoshi-mod) for building a Kirby Air Ride mod using the [Hoshi](https://github.com/UnclePunch/hoshi) modding framework.

This hosts the code for interfacing with the game via code hooks and other methods of accessing the game provided by hoshi. The goal is to provide the APWorld client with the needed game state to function. 

# Build Instructions

This project is written in **C** and uses `make` to manage the build process. It outputs `.bin` (code) and `.dat` (asset) files and can be built on **Windows**, **macOS**, and **Linux**.

---

## Prerequisites

### 1. **[devkitPPC](https://devkitpro.org/wiki/Getting_Started)** 

This contains the required PowerPC compiler. After installing, make sure the DEVKITPPC environment variable is set. You can verify this by typing `echo %DEVKITPPC%` in the Windows command prompt.

### 2. **Python 3**

This project depends on a script written in Python to pack the compiled `.o` file into a `.bin` file to be used by the game. Make sure Python 3 is installed and available in your PATH.

* **Windows**: Download from [python.org](https://www.python.org/downloads/) and check the "Add to PATH" option during installation.
* **Linux/macOS**: Use your package manager (e.g., `sudo apt install python3` or `brew install python`).

#### Optional
- [pyisotools](https://pypi.org/project/pyisotools/)
- xdelta

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
* Output a Riivolution folder for the mod.
* Output an XDelta patch that can be used to create an .iso for the mod.

To automatically install the files to your iso filesystem after building the project, run:
```bash
make all install INSTALL_DIR="Path\To\Root\Folder"
```
where `"Path\To\Root\Folder"` points to your extracted ISO filesystem containing the `files` and `sys` folder.