r"""Air ride machine archive cloner.

Copies a `Vc*.dat` machine archive under a new filename and public symbol, so a
new MachineKind can load its own archive without touching the donor. The data
section is carried over byte for byte, with the public renamed, a
`CustomMachineDesc` appended under a second public named `customMachine` (see
mods/custom_machines/include/custom_machines_api.h), and the optional material
recolor below.

A machine archive exports exactly one public, a `KAR_vcDataStar` (or
`KAR_vcDataWheel`), whose seven slots are:

    0x00 VehicleAttributes  0x04 ModelData        0x08 UnkCollisionGroup
    0x0c CollisionAttributes 0x10 CollisionSphere 0x14 HandlingAttributes
    0x18 AnimationBank

`--recolor` walks ModelData's `MainModelRoot` JObj tree and colorizes each
material's CMPR textures, cycling a palette across the materials, so a cloned
placeholder is visually distinct from its donor. Machine materials are
CONSTANT|TEX0 with white material colors - all of their color lives in the
textures - so the tint has to land on the texels rather than on the material.

Usage:
    uv run python scripts/hsd/clone_machine.py \
        iso/files/VcStarSlick.dat \
        mods/custom_machines/assets/machines/VcStarAp.dat \
        vcDataStarAp --name "Archipelago Star" \
        --description "A gift from another world.\nRides like a Slick Star." \
        [--recolor ap]
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, build_archive, u16, u32
from hsd.gx import FORMAT_NAME, image_size, tint_cmpr
from hsd.walker import Walker

VCDATA_MODELDATA = 0x04
MODELDATA_MAIN_ROOT = 0x00
MODELDATA_SHADOW_ROOT = 0x28

MOBJ_TEXTURES = 0x08
TOBJ_NEXT = 0x04
TOBJ_IMAGE = 0x4C
GX_TF_CMPR = 14

# Must match include/custom_machines_api.h.
CUSTOM_MACHINE_MAGIC = 0x434D4348  # 'CMCH'
CUSTOM_MACHINE_DESC_VERSION = 2
DESC_SIZE = 0x28

# Sampled from mods/archipelago/assets/ap-icon.png.
PALETTES = {
    "ap": [
        (0xC9, 0x76, 0x82),  # rose
        (0x75, 0xC2, 0x75),  # green
        (0xCA, 0x94, 0xC2),  # violet
        (0xD9, 0xA0, 0x7D),  # tan
        (0x76, 0x7E, 0xBD),  # blue
        (0xEE, 0xE3, 0x91),  # yellow
    ],
}


def model_roots(arc, public):
    """(main_root, shadow_root) data offsets from the machine's vcData."""
    base = arc.publics[public]
    model_data = u32(arc.data, base + VCDATA_MODELDATA)
    if model_data == 0:
        raise SystemExit(f"'{public}' has no ModelData")
    main = u32(arc.data, model_data + MODELDATA_MAIN_ROOT)
    shadow = u32(arc.data, model_data + MODELDATA_SHADOW_ROOT)
    bone_count = arc.data[model_data + 0x08]
    print(f"  ModelData @ {model_data:#x}: MainModelRoot={main:#x} "
          f"ShadowModelRoot={shadow:#x} BoneCount={bone_count}")
    return main, shadow


def recolor(arc, data, public, palette):
    """Colorize the main model tree's CMPR textures, cycling the palette over
    the distinct texture blobs. Machine models share one texture set across
    their LODs, so tinting per blob rather than per material is what actually
    spreads the palette over the machine."""
    main, _ = model_roots(arc, public)
    if main == 0:
        raise SystemExit("model has no MainModelRoot to recolor")

    visited = Walker(arc).walk(main, "JOBJDesc")
    mobjs = sorted(off for off, (t, _) in visited.items() if t == "MObjDesc")

    done = set()
    tinted = 0
    for mobj in mobjs:
        tobj = u32(arc.data, mobj + MOBJ_TEXTURES)
        while tobj != 0:
            img = u32(arc.data, tobj + TOBJ_IMAGE)
            blob = u32(arc.data, img + 0x00) if img else 0
            if blob and blob not in done:
                done.add(blob)
                w, h = u16(arc.data, img + 0x04), u16(arc.data, img + 0x06)
                fmt = u32(arc.data, img + 0x08)
                if fmt == GX_TF_CMPR:
                    tint = palette[tinted % len(palette)]
                    n = image_size(w, h, fmt, u32(arc.data, img + 0x0C) != 0)
                    data[blob:blob + n] = tint_cmpr(data[blob:blob + n], tint)
                    tinted += 1
                    print(f"  texture @ {blob:#x} {w}x{h} CMPR -> "
                          f"#{tint[0]:02X}{tint[1]:02X}{tint[2]:02X}")
                else:
                    print(f"  texture @ {blob:#x} {w}x{h} "
                          f"{FORMAT_NAME.get(fmt, fmt)} left alone")
            tobj = u32(arc.data, tobj + TOBJ_NEXT)

    print(f"  tinted {tinted} of {len(done)} textures across "
          f"{len(mobjs)} materials")


def append_descriptor(data, relocs, new_public, args):
    """Append a CustomMachineDesc plus its strings, and return its offset. Every
    string slot becomes a new relocation, exactly as the carved-asset scripts
    synthesize their descriptor pointers. A machine with no description leaves
    that slot null, which the registry reads as an empty blurb."""
    data.extend(b"\0" * (-len(data) & 3))
    desc_off = len(data)
    data.extend(b"\0" * DESC_SIZE)

    name_off = len(data)
    data.extend(args.name.encode("ascii") + b"\0")
    symbol_off = len(data)
    data.extend(new_public.encode("ascii") + b"\0")
    description_off = None
    if args.description:
        description_off = len(data)
        data.extend(args.description.encode("ascii") + b"\0")
    data.extend(b"\0" * (-len(data) & 3))

    struct.pack_into(">I", data, desc_off + 0x00, CUSTOM_MACHINE_MAGIC)
    struct.pack_into(">H", data, desc_off + 0x04, CUSTOM_MACHINE_DESC_VERSION)
    struct.pack_into(">H", data, desc_off + 0x06, 0)
    struct.pack_into(">I", data, desc_off + 0x08, name_off)
    struct.pack_into(">I", data, desc_off + 0x0C, symbol_off)
    struct.pack_into(">i", data, desc_off + 0x10, 0)  # star class
    struct.pack_into(">i", data, desc_off + 0x14, 0 if args.no_character else 1)
    struct.pack_into(">i", data, desc_off + 0x18, args.rider_kind)
    struct.pack_into(">i", data, desc_off + 0x1C, args.clone_kind)
    struct.pack_into(">f", data, desc_off + 0x20, args.spawn_weight)

    relocs.extend([desc_off + 0x08, desc_off + 0x0C])
    if description_off is not None:
        struct.pack_into(">I", data, desc_off + 0x24, description_off)
        relocs.append(desc_off + 0x24)
    return desc_off


def clone(src_path, out_path, new_public, palette, args):
    arc = Archive(src_path)
    if len(arc.publics) != 1:
        raise SystemExit(f"expected exactly one public, found {list(arc.publics)}")
    public = next(iter(arc.publics))
    print(f"  public '{public}' @ {arc.publics[public]:#x}, "
          f"{arc.data_size / 1024:.1f} KB data, {arc.nb_reloc} relocs")

    data = bytearray(arc.data)
    if palette is not None:
        recolor(arc, data, public, palette)

    relocs = list(arc.relocs)
    desc_off = append_descriptor(data, relocs, new_public, args)
    print(f"  descriptor '{args.name}' @ {desc_off:#x} "
          f"(character {'no' if args.no_character else 'yes'}, "
          f"clone kind {args.clone_kind}, spawn weight {args.spawn_weight})")
    for line in args.description.split("\n") if args.description else []:
        print(f"    description: {line}")

    publics = [(new_public, arc.publics[public]), ("customMachine", desc_off)]
    externs = [(name, off) for off, name in arc.externs]
    out = build_archive(data, relocs, publics, arc.version, externs)
    with open(out_path, "wb") as f:
        f.write(out)
    print(f"  wrote {out_path} ({len(out) / 1024:.1f} KB, publics "
          f"'{new_public}' + 'customMachine')")


def main(argv):
    p = argparse.ArgumentParser(description="Clone a Vc*.dat machine archive")
    p.add_argument("src", help="donor archive, e.g. iso/files/VcStarSlick.dat")
    p.add_argument("out", help="output archive path")
    p.add_argument("public", help="public symbol name for the clone")
    p.add_argument("--name", default=None, help="display name (default: the public symbol)")
    p.add_argument("--description", default="",
                   help="select-screen blurb under the name; two lines of about 24 "
                        r"characters, split with \n")
    p.add_argument("--recolor", choices=sorted(PALETTES), default=None,
                   help="colorize the model's CMPR textures from a named palette")
    p.add_argument("--no-character", action="store_true",
                   help="register the machine without a CharacterKind or select-grid cell")
    p.add_argument("--rider-kind", type=int, default=0,
                   help="RiderKind for the appended CharacterDesc row (0 = Kirby)")
    p.add_argument("--clone-kind", type=int, default=6,
                   help="star MachineKind whose per-kind engine rows - audio parameters and "
                        "the machine-specific handlers - this machine inherits "
                        "(default 6, Slick Star)")
    p.add_argument("--spawn-weight", type=float, default=2.0,
                   help="City Trial spawn weight; 0 never spawns loose on the field")
    args = p.parse_args(argv[1:])

    if args.name is None:
        args.name = args.public
    args.description = args.description.replace("\\n", "\n")

    print(f"Cloning {args.src} -> {args.out}:")
    clone(args.src, args.out, args.public, PALETTES.get(args.recolor), args)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
