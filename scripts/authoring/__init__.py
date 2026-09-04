# SPDX-License-Identifier: GPL-3.0-only
"""Per-mod asset authors: the scripts that write what a mod ships in `assets/`.

Each one turns human-made art in `art/` and/or an `iso/` extraction into a
finished `.dat` at a fixed path under `mods/<mod>/assets/`, using the general
`.dat` toolchain in `scripts/hsd/`. They take few or no arguments - the output
path is the point - so re-running one regenerates exactly what the build stages.

    ap_star          make_ap_star_pieces.py   ApSphere*.dat, ApPieceIcons.dat
                     make_ap_star_shot.py     ApStarShot.dat
                     make_ap_star_assembly.py ApStarParts.dat, ApStarGlow.dat
    archipelago      make_ap_box.py           items/ApBox.dat
                     make_checklist_textures.py  ApChecklistTex.dat
                     make_menu_logo.py        MnTitleKarchi.dat
    custom_machines  make_ui_frames.py        CmUiFrames.dat
    custom_weather   make_backdrop_manifest.py    BackdropManifest.dat
                     verify_backdrop_manifest.py  checks it against the donors

The assets a generic tool authors are not here: `mods/archipelago/assets/items/ApPatch.dat`
and `mods/hypernova/assets/items/MiracleFruit.dat` come from `scripts/hsd/carve_custom_item.py`
command lines, `machines/VcStarAp.art` from `scripts/hsd/make_machine_art.py`, and
`ApIcon.dat` / `ApBanner.dat` from `scripts/utility/card_tile.py`.
"""
