# SPDX-License-Identifier: GPL-3.0-only
"""Author the appended UI frames for every character-indexed bank, as a side-car.

The select-screen portraits, name plates, results art, the small machine icon the
results and stadium-select screens draw beside a player, and the time-attack board
are TexAnims whose animation frame *is* the CharacterKind, so an appended character
has no art until every such bank grows by an image. Growing them on disc means
shipping eight vanilla archives to carry the additions. This builds the
additions on their own instead - a placeholder frame for each bank, and the
keyframe buffer its ramps need - and custom_machines splices them into the vanilla
archives as they load, putting each registered machine's own art over the
placeholder wherever the machine shipped a .art side-car.

The banks grow by a fixed --appended frames, not by however many machines happen
to be registered: the ramps are keyframe data authored here, so their width has to
be decided here too. The unclaimed slots hold the placeholder and no CharacterKind
ever reaches them.

Twenty banks across eight archives need frames, but they hold only four distinct
(width, height, format) combinations between them, and most of their ramps encode
to identical bytes, so both are interned.

A bank's frames are not all the same shape - King Dedede's picture is 56x80 and
Meta Knight's 104x52 against Slick Star's 80x48 - so the Sicon models size their
quad per character through a pair of AnimJoint scale tracks sharing the image
ramp's timeline. Those are widened too, their new keys holding the --source
frame's scale, which is the shape every appended frame is encoded at; without it
an appended character draws at whatever shape the frame it displaced used to hold.

The appended frames start at frame 20, where the roster ends, which is where the
engine already diverts King Dedede and Meta Knight's colours. Those two runs slide
--appended frames later; the mod rewrites the sixteen addi immediates that form
them to match.

The output exports one public:

  apUiFrames  - UiFrameFile[], terminated by a zero name

whose layout must match mods/custom_machines/src/ui_frames.c. A bank names the
donor by data-section offset: those are fixed for GKYE01, and the mod adds the
loaded archive's data base to reach the live TexAnim.

Run from the repo root:
    uv run --with pillow python scripts/authoring/make_ui_frames.py
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, Blob, build_archive, u16, u32
from hsd.gx import FORMAT_NAME, align32
from hsd.ui_art import BANK_ROLE
from hsd.ui_banks import (
    FOBJ_TRACK,
    TEXANIM_AOBJ,
    TEXANIM_IMAGES,
    TEXANIM_N_IMAGES,
    TEXANIM_N_TLUTS,
    TRACK_TCLT,
    TRACK_TIMG,
    encode_art,
    encoded_ramp,
    index_values,
    joint_ramp_offsets,
    ramp_value_at,
    texanim_offsets,
    track_chain,
)

PUBLIC = "apUiFrames"

BANK_SIZE = 0x34
RAMP_SIZE = 0x10
FILE_SIZE = 0x14

# MnSelruleAll is left out on purpose. Its 96x24 I4 bank holds the rule screen's
# option labels and is only found by the character-bank test because it happens to
# hold 20 images; its animation frame is a rule value, never a CharacterKind.
DONORS = [
    "MnBestrapAll",
    "MnResult2All",
    "MnResult4All",
    "MnResultAll",
    "MnResultCtAll",
    "MnSelplyAll",
    "MnSelplyctAll",
    "MnSelstadiumAll",
]


def bank_tracks(data, tex):
    """The bank's image-index and TLUT-index FObj offsets, 0 where absent."""
    timg = tclt = 0
    for fobj in track_chain(data, u32(data, tex + TEXANIM_AOBJ)):
        kind = data[fobj + FOBJ_TRACK]
        if kind == TRACK_TIMG:
            timg = fobj
        elif kind == TRACK_TCLT:
            tclt = fobj
    return timg, tclt


def bank_geometry(data, tex, src_idx):
    """The source frame's (width, height, format) - the bank's role key."""
    src = u32(data, u32(data, tex + TEXANIM_IMAGES) + src_idx * 4)
    return u16(data, src + 0x04), u16(data, src + 0x06), u32(data, src + 0x08)


def bank_image(blob, geom, art):
    """Intern the placeholder frame's pixels and descriptor; return its offset."""
    w, h, src_fmt = geom
    fmt, pixels = encode_art(art, w, h, src_fmt)
    if pixels is None:
        raise SystemExit(f"  no encoder for source format {src_fmt}")

    texels = blob.intern(pixels + b"\0" * align32(len(pixels)), 32)
    desc = blob.intern(struct.pack(">IHHIIff", 0, w, h, fmt, 0, 0.0, 0.0))
    blob.ptr(desc, texels)
    return desc, f"{w}x{h} {FORMAT_NAME.get(fmt, fmt)}"


def main(argv):
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--iso-dir", default="iso/files", help="extracted disc root")
    p.add_argument("--out", default="mods/custom_machines/assets/CmUiFrames.dat")
    p.add_argument(
        "--image",
        default="art/ap-icon.png",
        help="placeholder art, shown for any machine that ships no .art side-car",
    )
    p.add_argument(
        "--frames",
        type=int,
        default=20,
        help="characters in the roster; a bank this many images wide is one of theirs",
    )
    p.add_argument(
        "--source",
        type=int,
        default=4,
        help="frame the new ones are cloned from (default 4, Slick Star)",
    )
    p.add_argument(
        "--appended",
        type=int,
        default=13,
        help="frames every bank grows by, which caps how many registered machines "
        "can carry art; must match CUSTOM_MACHINE_MAX",
    )
    args = p.parse_args(argv[1:])

    from PIL import Image

    im = Image.open(args.image).convert("RGBA")

    blob = Blob()
    files = []
    for name in DONORS:
        arc = Archive(os.path.join(args.iso_dir, name + ".dat"))
        banks = [
            tex
            for tex in texanim_offsets(arc, args.frames)
            if bank_geometry(arc.data, tex, args.source) in BANK_ROLE
        ]
        if not banks:
            raise SystemExit(f"{name}: no character-indexed bank")

        table = blob.append(b"\0" * (BANK_SIZE * len(banks)))
        print(f"{name}: {len(banks)} bank(s)")
        for i, tex in enumerate(banks):
            at = table + i * BANK_SIZE
            data = arc.data
            n = u16(data, tex + TEXANIM_N_IMAGES)
            n_tlut = u16(data, tex + TEXANIM_N_TLUTS)
            timg, tclt = bank_tracks(data, tex)
            if not timg:
                raise SystemExit(f"  bank @ {tex:#x}: no image-index track")

            geom = bank_geometry(data, tex, args.source)
            desc, note = bank_image(blob, geom, im)
            timg_buf, timg_flag = encoded_ramp(
                data, timg, index_values(n, args.appended)
            )
            timg_off = blob.intern(timg_buf)

            struct.pack_into(
                ">IHHHH", blob.data, at, tex, n, n_tlut, args.source, args.appended
            )
            blob.ptr(at + 0x0C, desc)
            struct.pack_into(">I", blob.data, at + 0x10, timg)
            blob.ptr(at + 0x14, timg_off)
            struct.pack_into(">I", blob.data, at + 0x18, len(timg_buf))
            struct.pack_into(">HHI", blob.data, at + 0x28, geom[0], geom[1], geom[2])
            struct.pack_into(">B", blob.data, at + 0x30, timg_flag)

            if tclt:
                tclt_buf, tclt_flag = encoded_ramp(
                    data, tclt, index_values(n_tlut, args.appended)
                )
                tclt_off = blob.intern(tclt_buf)
                struct.pack_into(">I", blob.data, at + 0x1C, tclt)
                blob.ptr(at + 0x20, tclt_off)
                struct.pack_into(">I", blob.data, at + 0x24, len(tclt_buf))
                struct.pack_into(">B", blob.data, at + 0x31, tclt_flag)
            print(
                f"  bank @ {tex:#x}: {BANK_ROLE[geom]}, {n} -> {n + args.appended} frames, "
                f"{note}"
                + (f", tlut {n_tlut} -> {n_tlut + args.appended}" if tclt else "")
            )

        joints = joint_ramp_offsets(arc)
        ramps = blob.append(b"\0" * (RAMP_SIZE * len(joints))) if joints else 0
        for i, fobj in enumerate(joints):
            at = ramps + i * RAMP_SIZE
            value = ramp_value_at(arc.data, fobj, args.source)
            buf, flag = encoded_ramp(arc.data, fobj, [value] * args.appended)
            struct.pack_into(">I", blob.data, at, fobj)
            blob.ptr(at + 0x04, blob.intern(buf))
            struct.pack_into(">I", blob.data, at + 0x08, len(buf))
            struct.pack_into(">B", blob.data, at + 0x0C, flag)
            print(f"  joint ramp @ {fobj:#x}: holds frame {args.source}'s {value:g}")
        files.append((name, len(banks), table, len(joints), ramps))

    name_offs = [blob.append(f[0].encode("ascii") + b"\0", 1) for f in files]
    table = blob.append(b"\0" * (FILE_SIZE * (len(files) + 1)))
    for i, ((name, n_banks, banks, n_ramps, ramps), name_off) in enumerate(
        zip(files, name_offs)
    ):
        at = table + i * FILE_SIZE
        blob.ptr(at, name_off)
        struct.pack_into(">I", blob.data, at + 0x04, n_banks)
        blob.ptr(at + 0x08, banks)
        struct.pack_into(">I", blob.data, at + 0x0C, n_ramps)
        if n_ramps:
            blob.ptr(at + 0x10, ramps)

    out = build_archive(blob.data, blob.relocs, [(PUBLIC, table)])
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)
    print(
        f"wrote {args.out} ({len(out) / 1024:.1f} KB, {len(files)} archive(s), "
        f"{sum(f[1] for f in files)} bank(s), {sum(f[3] for f in files)} joint ramp(s))"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
