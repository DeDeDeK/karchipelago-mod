r"""Air ride machine archive cloner.

Copies a `Vc*.dat` machine archive under a new filename and public symbol, so a
new MachineKind can load its own archive without touching the donor. The data
section is carried over byte for byte, with the public renamed and a
`CustomMachineDesc` appended under a second public named `customMachine` (see
mods/custom_machines/include/custom_machines_api.h).

A machine archive exports exactly one public, a `KAR_vcDataStar` (or
`KAR_vcDataWheel`), whose seven slots are:

    0x00 VehicleAttributes  0x04 ModelData        0x08 UnkCollisionGroup
    0x0c CollisionAttributes 0x10 CollisionSphere 0x14 HandlingAttributes
    0x18 AnimationBank

Nothing inside those slots is touched, so a clone is its donor with a new name.
A machine that wants its own shape or paint edits the archive itself; the
Archipelago Star's builder, make_ap_star.py, is the worked example.

Usage:
    uv run python scripts/hsd/clone_machine.py \
        iso/files/VcStarWing.dat \
        mods/custom_machines/assets/machines/VcStarMine.dat \
        vcDataStarMine --name "Mine Star" \
        --description "Borrowed wings." --clone-kind 2
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, build_archive

# Must match include/custom_machines_api.h.
CUSTOM_MACHINE_MAGIC = 0x434D4348  # 'CMCH'
CUSTOM_MACHINE_DESC_VERSION = 3
DESC_SIZE = 0x38


def append_descriptor(data, relocs, new_public, args, palette=None):
    """Append a CustomMachineDesc plus its strings, and return its offset. Every
    string slot becomes a new relocation, exactly as the carved-asset scripts
    synthesize their descriptor pointers. A machine with no description leaves
    that slot null, which the registry reads as an empty blurb.

    `palette` is (joint, period, count, offset) for a machine whose materials on
    that joint cycle through a color table already written into `data`; without
    one the joint slot is -1 and the registry leaves the machine's colors alone."""
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

    joint, period, count, offset = palette if palette else (-1, 0.0, 0, 0)
    struct.pack_into(">i", data, desc_off + 0x28, joint)
    struct.pack_into(">f", data, desc_off + 0x2C, period)
    struct.pack_into(">i", data, desc_off + 0x30, count)

    relocs.extend([desc_off + 0x08, desc_off + 0x0C])
    if description_off is not None:
        struct.pack_into(">I", data, desc_off + 0x24, description_off)
        relocs.append(desc_off + 0x24)
    if palette:
        struct.pack_into(">I", data, desc_off + 0x34, offset)
        relocs.append(desc_off + 0x34)
    return desc_off


def clone(src_path, out_path, new_public, args):
    arc = Archive(src_path)
    if len(arc.publics) != 1:
        raise SystemExit(f"expected exactly one public, found {list(arc.publics)}")
    public = next(iter(arc.publics))
    print(f"  public '{public}' @ {arc.publics[public]:#x}, "
          f"{arc.data_size / 1024:.1f} KB data, {arc.nb_reloc} relocs")

    data = bytearray(arc.data)
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
    p.add_argument("src", help="donor archive, e.g. iso/files/VcStarWing.dat")
    p.add_argument("out", help="output archive path")
    p.add_argument("public", help="public symbol name for the clone")
    p.add_argument("--name", default=None, help="display name (default: the public symbol)")
    p.add_argument("--description", default="",
                   help="select-screen blurb under the name; two lines of about 24 "
                        r"characters, split with \n")
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
    clone(args.src, args.out, args.public, args)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
