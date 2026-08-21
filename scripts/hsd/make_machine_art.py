# SPDX-License-Identifier: GPL-3.0-only
"""Author a custom machine's UI art side-car from two renders.

The game draws a machine picture from TexAnims whose animation frame is the
CharacterKind, and custom_machines gives every appended character a frame in each
of them. This writes the images that go in one machine's frames: a
`machines/VcStarAp.art` beside `machines/VcStarAp.dat`, which the registry finds by
basename and splices in wherever that machine's CharacterKind is drawn. A machine
with no side-car shows the registry's placeholder instead.

Four images cover every bank, because a bank's role is fully determined by the
geometry of the frame it clones:

    64x64 CMPR  portrait     the character-select grid tile
    80x48 C8    picture      the large art beside the CSS cursor and on results
    80x48 I4    silhouette   the bloom drawn under the picture
    40x40 C4    icon         the time-attack board and the results-screen rows

They come from two renders of the machine, because the icon's angle is not the
picture's: a three-quarter hero view drives the first three, a straight top-down
the fourth. Give both a transparent background, a generous margin so nothing clips
before framing, and at least 4x the target resolution.

The output exports one public:

  customMachineArt  - CustomMachineArt

whose layout must match mods/custom_machines/include/custom_machines_api.h.

Run from the repo root:
    uv run --with pillow python scripts/hsd/make_machine_art.py \\
        mods/ap_star/assets/machines/VcStarAp.art \\
        --hero art/ap-star-hero.png --topdown art/ap-star-top.png
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.add_ui_frame import encode_art
from hsd.archive import build_archive
from hsd.gx import FORMAT_NAME, align32
from hsd.make_ui_frames import Blob
from hsd.ui_art import BANK_ROLE, role_images

# Must match include/custom_machines_api.h.
CUSTOM_MACHINE_ART_MAGIC = 0x434D4152  # 'CMAR'
CUSTOM_MACHINE_ART_VERSION = 1
ART_SIZE = 0x0C

ARCHIVE_VERSION = b"001B"
PUBLIC = "customMachineArt"


def build(out_path, hero, topdown):
    images = role_images(hero, topdown)
    blob = Blob()

    entries = blob.append(b"\0" * (ART_SIZE * len(BANK_ROLE)))
    for i, ((w, h, src_fmt), role) in enumerate(sorted(BANK_ROLE.items())):
        fmt, pixels = encode_art(images[role], w, h, src_fmt)
        if pixels is None:
            raise SystemExit(f"{role}: no encoder for source format {src_fmt}")

        texels = blob.intern(pixels + b"\0" * align32(len(pixels)), 32)
        desc = blob.intern(struct.pack(">IHHIIff", 0, w, h, fmt, 0, 0.0, 0.0))
        blob.ptr(desc, texels)

        at = entries + i * ART_SIZE
        struct.pack_into(">HHI", blob.data, at, w, h, src_fmt)
        blob.ptr(at + 0x08, desc)
        print(f"  {role:11s} {w}x{h} {FORMAT_NAME.get(fmt, fmt)}, "
              f"{len(pixels)} B over source format {src_fmt}")

    head = blob.append(struct.pack(">IHHI", CUSTOM_MACHINE_ART_MAGIC,
                                   CUSTOM_MACHINE_ART_VERSION, len(BANK_ROLE), 0))
    blob.ptr(head + 0x08, entries)

    out = build_archive(blob.data, blob.relocs, [(PUBLIC, head)], ARCHIVE_VERSION)
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(out)
    print(f"  wrote {out_path} ({len(out) / 1024:.1f} KB, {len(BANK_ROLE)} image(s))")


def main(argv):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out", help="output side-car, e.g. mods/ap_star/assets/machines/VcStarAp.art")
    p.add_argument("--hero", required=True,
                   help="three-quarter render; drives the portrait, picture and silhouette")
    p.add_argument("--topdown", default=None,
                   help="straight-down render for the small icon (default: --hero)")
    args = p.parse_args(argv[1:])

    from PIL import Image
    hero = Image.open(args.hero).convert("RGBA")
    topdown = Image.open(args.topdown).convert("RGBA") if args.topdown else hero

    print(f"Building {args.out}:")
    build(args.out, hero, topdown)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
