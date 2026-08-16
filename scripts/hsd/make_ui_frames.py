# SPDX-License-Identifier: GPL-3.0-only
"""Author the appended UI frame for every character-indexed bank, as a side-car.

The select-screen portraits, name plates, results art and time-attack board are
TexAnims whose animation frame *is* the CharacterKind, so a 21st character has no
art until every such bank grows by one image. Growing them on disc means shipping
eight vanilla archives to carry 86 KB of additions. This builds the additions on
their own instead - the appended image for each bank, and the keyframe buffer its
image-index ramp needs - and custom_machines splices them into the vanilla
archives as they load.

Sixteen banks across eight archives need a frame, but they hold only five
distinct (width, height, format) combinations between them, and most of their
ramps encode to identical bytes, so both are interned: the side-car is ~25 KB
where the rewritten archives were 3.9 MB.

The art is encoded to suit each bank the same way add_ui_frame.py does it -
picture banks take an RGB5A3 copy, I4 text banks an I4 intensity map of its alpha
- because format is per frame and neither a CMPR encoder nor a quantizer is
needed to add one.

The output exports one public:

  apUiFrames  - UiFrameFile[], terminated by a zero name

whose layout must match mods/custom_machines/src/ui_frames.c. A bank names the
donor by data-section offset: those are fixed for GKYE01, and the mod adds the
loaded archive's data base to reach the live TexAnim.

Run from the repo root:
    uv run --with pillow python scripts/hsd/make_ui_frames.py
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.add_ui_frame import (TEXANIM_AOBJ, TEXANIM_IMAGES, TEXANIM_N_IMAGES,
                              TEXANIM_N_TLUTS, TRACK_TCLT, TRACK_TIMG,
                              FOBJ_TRACK, encode_art, encoded_ramp,
                              texanim_offsets, track_chain)
from hsd.archive import Archive, build_archive, u16, u32
from hsd.gx import FORMAT_NAME, align32

ARCHIVE_VERSION = b"001B"
PUBLIC = "apUiFrames"

BANK_SIZE = 0x28
FILE_SIZE = 0x0C

DONORS = ["MnBestrapAll", "MnResult2All", "MnResult4All", "MnResultAll",
          "MnResultCtAll", "MnSelplyAll", "MnSelplyctAll", "MnSelruleAll"]


class Blob:
    """A data section under construction, with its relocation list."""

    def __init__(self):
        self.data = bytearray()
        self.relocs = []
        self.interned = {}
        self.pointed = set()

    def append(self, payload, alignment=4):
        self.data.extend(b"\0" * ((-len(self.data)) & (alignment - 1)))
        off = len(self.data)
        self.data.extend(payload)
        return off

    def intern(self, payload, alignment=4):
        """Append `payload` once; identical payloads share one offset."""
        key = (bytes(payload), alignment)
        if key not in self.interned:
            self.interned[key] = self.append(payload, alignment)
        return self.interned[key]

    def ptr(self, at, target):
        """Write a pointer at `at` and register it for relocation.

        Interned structures are pointed at once per bank that shares them, and a
        slot relocated twice is relocated twice by the engine, so each is
        registered only the first time."""
        struct.pack_into(">I", self.data, at, target)
        if at not in self.pointed:
            self.pointed.add(at)
            self.relocs.append(at)


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


def bank_image(blob, data, tex, src_idx, im):
    """Intern the appended frame's pixels and descriptor; return its offset."""
    src = u32(data, u32(data, tex + TEXANIM_IMAGES) + src_idx * 4)
    w, h = u16(data, src + 0x04), u16(data, src + 0x06)
    fmt, pixels = encode_art(im, w, h, u32(data, src + 0x08))
    if pixels is None:
        raise SystemExit(f"  bank @ {tex:#x}: no encoder for format "
                         f"{u32(data, src + 0x08)}")

    texels = blob.intern(pixels + b"\0" * align32(len(pixels)), 32)
    desc = blob.intern(struct.pack(">IHHIIff", 0, w, h, fmt, 0, 0.0, 0.0))
    blob.ptr(desc, texels)
    return desc, f"{w}x{h} {FORMAT_NAME.get(fmt, fmt)}"


def main(argv):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--iso-dir", default="iso/files", help="extracted disc root")
    p.add_argument("--out", default="mods/custom_machines/assets/ApUiFrames.dat")
    p.add_argument("--image", default="art/ap-icon.png")
    p.add_argument("--frames", type=int, default=20,
                   help="characters in the roster; a bank this many images wide is one of theirs")
    p.add_argument("--source", type=int, default=4,
                   help="frame the new one is cloned from (default 4, Slick Star)")
    p.add_argument("--new-frame", type=int, default=None,
                   help="animation frame that selects the new entry (default --frames)")
    args = p.parse_args(argv[1:])
    if args.new_frame is None:
        args.new_frame = args.frames

    from PIL import Image
    im = Image.open(args.image).convert("RGBA")

    blob = Blob()
    files = []
    for name in DONORS:
        arc = Archive(os.path.join(args.iso_dir, name + ".dat"))
        banks = texanim_offsets(arc, args.frames)
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

            desc, note = bank_image(blob, data, tex, args.source, im)
            timg_buf = encoded_ramp(data, timg, n, args.new_frame)
            timg_off = blob.intern(timg_buf)

            struct.pack_into(">IHHHH", blob.data, at, tex, n, n_tlut, args.source, 0)
            blob.ptr(at + 0x0C, desc)
            struct.pack_into(">I", blob.data, at + 0x10, timg)
            blob.ptr(at + 0x14, timg_off)
            struct.pack_into(">I", blob.data, at + 0x18, len(timg_buf))

            if tclt:
                tclt_buf = encoded_ramp(data, tclt, n, args.new_frame)
                tclt_off = blob.intern(tclt_buf)
                struct.pack_into(">I", blob.data, at + 0x1C, tclt)
                blob.ptr(at + 0x20, tclt_off)
                struct.pack_into(">I", blob.data, at + 0x24, len(tclt_buf))
            print(f"  bank @ {tex:#x}: {n} -> {n + 1} frames, {note}"
                  + (f", tlut {n_tlut} -> {n_tlut + 1}" if tclt else ""))
        files.append((name, len(banks), table))

    name_offs = [blob.append(n.encode("ascii") + b"\0", 1) for n, _, _ in files]
    table = blob.append(b"\0" * (FILE_SIZE * (len(files) + 1)))
    for i, ((name, n_banks, banks), name_off) in enumerate(zip(files, name_offs)):
        at = table + i * FILE_SIZE
        blob.ptr(at, name_off)
        struct.pack_into(">I", blob.data, at + 0x04, n_banks)
        blob.ptr(at + 0x08, banks)

    out = build_archive(blob.data, blob.relocs, [(PUBLIC, table)], ARCHIVE_VERSION)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)
    print(f"wrote {args.out} ({len(out) / 1024:.1f} KB, {len(files)} archive(s), "
          f"{sum(n for _, n, _ in files)} bank(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
