# SPDX-License-Identifier: GPL-3.0-only
"""Append a CharacterKind frame to every character-indexed UI bank in an archive.

The select-screen portraits, name plates, results art and time-attack board are
all TexAnims whose animation frame *is* the CharacterKind: the AObj carries one
CON key per frame whose value is the frame number, so frame N selects image N,
and a hold key at frame 300 pins the last one. The frame count is baked into the
TexAnim header, so a 21st character has no art until every such bank grows.

Two shapes of bank qualify. Most hold one image per character, so `--frames`
finds them by image count. The rest hold one image per character *colour* - the
engine diverts King Dedede to frame 20 + colour and Meta Knight to 30 + colour -
so they are wider than the roster and are found by those diverts sitting on their
ramp instead.

Either way this appends one image (and one TLUT entry when a TLUT track indexes
them), bumps the header counts and adds a key to each ramp track.

The new frame's art comes from `--image`, encoded to suit the bank it lands in:
picture banks take an RGB5A3 copy (format is per frame, so the existing CMPR and
paletted frames are left alone and neither a CMPR encoder nor a quantizer is
needed) and I4 text banks take an I4 intensity map of its alpha. Banks in any
other format fall back to reusing the `--source` frame's descriptor unchanged.

Run from the repo root:
    uv run --with pillow python scripts/hsd/add_ui_frame.py iso/files/Mn*.dat \
        --out-dir mods/custom_machines/assets \
        --image mods/archipelago/assets/ap-icon.png
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, NotAnHSDArchive, build_archive, u16, u32
from hsd.fobj import Key, OP_CON, decode, encode
from hsd.gx import (FORMAT_NAME, GX_TF_I4, GX_TF_RGB5A3, align32,
                    encode_i4_alpha, encode_rgb5a3)
from hsd.schema import array_length, root_for
from hsd.symbols import classify_symbol
from hsd.walker import Walker

GX_TF_C4 = 8
GX_TF_C8 = 9
GX_TF_CMPR = 14

# The two CharacterKinds the engine keeps out of the roster's own frame run, and
# the frames it diverts them to instead - each plus colour.
DEDEDE_KIND = 18
METAKNIGHT_KIND = 19
DEDEDE_FRAME = 20
METAKNIGHT_FRAME = 30

TEXANIM_AOBJ = 0x08
TEXANIM_IMAGES = 0x0C
TEXANIM_TLUTS = 0x10
TEXANIM_N_IMAGES = 0x14
TEXANIM_N_TLUTS = 0x16

AOBJ_TRACKS = 0x08

FOBJ_NEXT = 0x00
FOBJ_LENGTH = 0x04
FOBJ_TRACK = 0x0C
FOBJ_VALUE_FLAG = 0x0D
FOBJ_TAN_FLAG = 0x0E
FOBJ_BUFFER = 0x10

# HSD_A_T_TIMG / HSD_A_T_TCLT: the image-index and TLUT-index tracks.
TRACK_TIMG = 1
TRACK_TCLT = 10


def is_character_bank(data, tex, n_frames):
    """Whether the bank is indexed by CharacterKind. Either it holds one image per
    character, or it holds one per character colour, which makes it wider than the
    roster and leaves the diverts' shape on its image-index ramp: a key for each
    character up to King Dedede, none on his kind or Meta Knight's, and one on each
    of the frames they are diverted to. Plenty of other banks key frames 20 and 30
    without being anyone's, so the whole shape has to match."""
    if u16(data, tex + TEXANIM_N_IMAGES) == n_frames:
        return True
    for fobj in track_chain(data, u32(data, tex + TEXANIM_AOBJ)):
        if data[fobj + FOBJ_TRACK] != TRACK_TIMG:
            continue
        frames = {int(k.frame) for k in ramp_keys(data, fobj)}
        return (frames.issuperset(range(DEDEDE_KIND))
                and frames.isdisjoint((DEDEDE_KIND, METAKNIGHT_KIND))
                and frames.issuperset((DEDEDE_FRAME, METAKNIGHT_FRAME)))
    return False


def texanim_offsets(arc, n_frames):
    """Every character-indexed TexAnim reachable from a public."""
    walker = Walker(arc)
    for sym, off in arc.publics.items():
        root_type, is_array = root_for(classify_symbol(sym))
        try:
            if is_array:
                for i in range(array_length(arc, off)):
                    walker.walk(u32(arc.data, off + i * 4), root_type)
            else:
                walker.walk(off, root_type)
        except (struct.error, IndexError, RecursionError, KeyError):
            continue
    return sorted(off for off, (t, _) in walker.visited.items()
                  if t == "TexAnim" and is_character_bank(arc.data, off, n_frames))


def append(data, blob, alignment=4):
    """Append a blob at the requested alignment and return its offset."""
    data.extend(b"\0" * ((-len(data)) & (alignment - 1)))
    off = len(data)
    data.extend(blob)
    return off


def append_ptr_array(data, relocs, entries):
    """Append a contiguous pointer array, registering every slot as a reloc."""
    data.extend(b"\0" * ((-len(data)) & 3))
    off = len(data)
    for e in entries:
        relocs.append(len(data))
        data.extend(struct.pack(">I", e))
    return off


def encode_art(im, w, h, fmt):
    """The new frame's pixels, or None when the bank's format has no encoder."""
    scaled = im.resize((w, h))
    if fmt in (GX_TF_CMPR, GX_TF_C4, GX_TF_C8):
        return GX_TF_RGB5A3, encode_rgb5a3(scaled)
    if fmt == GX_TF_I4:
        return GX_TF_I4, encode_i4_alpha(scaled)
    return None, None


def track_chain(data, aobj):
    """The AObj's FOBJDesc offsets, in list order."""
    offs = []
    off = u32(data, aobj + AOBJ_TRACKS) if aobj else 0
    while off:
        offs.append(off)
        off = u32(data, off + FOBJ_NEXT)
    return offs


def ramp_keys(data, fobj):
    """The track's keys, decoded."""
    buf_off = u32(data, fobj + FOBJ_BUFFER)
    return decode(bytes(data[buf_off:buf_off + u32(data, fobj + FOBJ_LENGTH)]),
                  data[fobj + FOBJ_VALUE_FLAG], data[fobj + FOBJ_TAN_FLAG])


def extend_ramp(data, fobj, n, new_frame):
    """Give the ramp a key selecting entry n at `new_frame`.

    Portrait banks index frame by frame, so `new_frame` is free and the key goes
    on the end - the trailing hold key follows it onto the new entry. Banks that
    divert King Dedede have him on `new_frame` already: one key on the name plates,
    a colour apiece on the picture banks. That whole run slides one frame later,
    which the mod matches by patching the engine's `+ 20` to `+ 21`. Meta Knight's
    run starts after the gap the slide runs into, so it stays where it is."""
    value_flag = data[fobj + FOBJ_VALUE_FLAG]
    tan_flag = data[fobj + FOBJ_TAN_FLAG]
    keys = ramp_keys(data, fobj)

    frames = [k.frame for k in keys]
    i = sum(1 for f in frames if f < new_frame)
    if new_frame in frames:
        run = i
        while run < len(keys) and keys[run].frame == new_frame + (run - i):
            run += 1
        if run < len(keys) and keys[run].frame <= new_frame + (run - i):
            raise SystemExit(f"  no room to slide the run at frame {new_frame}")
        for j in range(i, run):
            keys[j] = Key(keys[j].frame + 1, keys[j].value, keys[j].tan, keys[j].op)
    keys.insert(i, Key(float(new_frame), float(n), 0.0, OP_CON))

    # The last key holds the animation past its useful range; keep it on the
    # last real entry, which the new frame becomes when it lands on the end.
    if i == len(keys) - 2:
        keys[-1] = Key(keys[-1].frame, float(n), keys[-1].tan, keys[-1].op)

    buf = encode(keys, value_flag, tan_flag)
    struct.pack_into(">I", data, fobj + FOBJ_LENGTH, len(buf))
    struct.pack_into(">I", data, fobj + FOBJ_BUFFER, append(data, buf))
    return [f"{int(k.frame)}:{k.value:g}" for k in keys]


def grow_bank(data, relocs, tex, src_idx, im, new_frame):
    n = u16(data, tex + TEXANIM_N_IMAGES)
    img_tbl = u32(data, tex + TEXANIM_IMAGES)
    images = [u32(data, img_tbl + i * 4) for i in range(n)]

    src = images[src_idx]
    w, h = u16(data, src + 0x04), u16(data, src + 0x06)
    fmt = u32(data, src + 0x08)

    new_fmt, pixels = encode_art(im, w, h, fmt) if im is not None else (None, None)
    if pixels is None:
        new_img = src
        note = f"{w}x{h} {FORMAT_NAME.get(fmt, fmt)}, reuses frame {src_idx}"
    else:
        blob = append(data, pixels + b"\0" * align32(len(pixels)), 32)
        new_img = append(data, struct.pack(">IHHIIff", blob, w, h, new_fmt, 0, 0.0, 0.0))
        relocs.append(new_img)
        note = f"{w}x{h} {FORMAT_NAME.get(new_fmt, new_fmt)}"

    struct.pack_into(">I", data, tex + TEXANIM_IMAGES,
                     append_ptr_array(data, relocs, images + [new_img]))
    struct.pack_into(">H", data, tex + TEXANIM_N_IMAGES, n + 1)

    ramp = None
    for fobj in track_chain(data, u32(data, tex + TEXANIM_AOBJ)):
        kind = data[fobj + FOBJ_TRACK]
        if kind not in (TRACK_TIMG, TRACK_TCLT):
            continue
        if kind == TRACK_TCLT:
            n_tlut = u16(data, tex + TEXANIM_N_TLUTS)
            tlut_tbl = u32(data, tex + TEXANIM_TLUTS)
            tluts = [u32(data, tlut_tbl + i * 4) for i in range(n_tlut)]
            struct.pack_into(">I", data, tex + TEXANIM_TLUTS,
                             append_ptr_array(data, relocs, tluts + [tluts[src_idx]]))
            struct.pack_into(">H", data, tex + TEXANIM_N_TLUTS, n_tlut + 1)
        keys = extend_ramp(data, fobj, n, new_frame)
        if kind == TRACK_TIMG:
            ramp = keys

    if ramp is None:
        raise SystemExit(f"  bank @ {tex:#x}: no image-index track to extend")
    print(f"  bank @ {tex:#x}: {n} -> {n + 1} frames, {note}")
    print(f"    ramp: {' '.join(ramp)}")


def grow_archive(src, out, args, im):
    arc = Archive(src)
    banks = texanim_offsets(arc, args.frames)
    if not banks:
        return False

    data = bytearray(arc.data)
    relocs = list(arc.relocs)
    print(f"{src} -> {out}:")
    for tex in banks:
        grow_bank(data, relocs, tex, args.source, im, args.new_frame)

    publics = list(arc.publics.items())
    externs = [(name, off) for off, name in arc.externs]
    blob = build_archive(data, relocs, publics, arc.version, externs)
    with open(out, "wb") as f:
        f.write(blob)
    print(f"  wrote {out} ({len(blob) / 1024:.1f} KB, {len(banks)} bank(s), "
          f"{len(relocs) - arc.nb_reloc} new relocs)")
    return True


def main(argv):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("srcs", nargs="+", help="donor archives; those with no matching bank are skipped")
    p.add_argument("--out-dir", required=True, help="directory the rewritten archives land in")
    p.add_argument("--frames", type=int, default=20,
                   help="characters in the roster; a bank this many images wide is one of "
                        "theirs, as is any bank whose ramp keys the colour diverts")
    p.add_argument("--source", type=int, default=4,
                   help="frame the new one is cloned from (default 4, Slick Star)")
    p.add_argument("--new-frame", type=int, default=None,
                   help="animation frame that selects the new entry (default --frames)")
    p.add_argument("--image", default=None,
                   help="PNG for the new frame; omitted reuses the source frame")
    args = p.parse_args(argv[1:])
    if args.new_frame is None:
        args.new_frame = args.frames

    im = None
    if args.image:
        from PIL import Image
        im = Image.open(args.image).convert("RGBA")

    os.makedirs(args.out_dir, exist_ok=True)
    grown = 0
    for src in args.srcs:
        try:
            if grow_archive(src, os.path.join(args.out_dir, os.path.basename(src)), args, im):
                grown += 1
        except NotAnHSDArchive:
            continue
    print(f"{grown} of {len(args.srcs)} archive(s) carried a character-indexed bank")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
