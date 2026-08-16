# SPDX-License-Identifier: GPL-3.0-only
"""Check that splicing ApUiFrames.dat in reproduces a rewritten donor exactly.

custom_machines patches the appended UI frame into each vanilla menu archive as
it loads, instead of shipping a rewritten copy of the archive. This applies the
side-car's patches to the vanilla donors the way the mod does at runtime, applies
add_ui_frame.py's rewrite to the same donors, and compares what the engine would
actually read out of both: every bank's image count, TLUT count, appended image
(dimensions, format and texels) and decoded image-index and TLUT ramps.

The two differ by construction in layout - the rewrite appends to the donor's own
data section, the splice repoints into the side-car's - so the comparison is on
decoded state, not bytes.

Run from the repo root, after make_ui_frames.py:
    uv run --with pillow python scripts/hsd/verify_ui_frames.py
"""
import argparse
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd import add_ui_frame as auf
from hsd.archive import Archive, u16, u32
from hsd.gx import image_size
from hsd.make_ui_frames import BANK_SIZE, FILE_SIZE, PUBLIC


def image_state(data, tex, idx):
    """The bank's idx-th image as (w, h, format, texels)."""
    img = u32(data, u32(data, tex + auf.TEXANIM_IMAGES) + idx * 4)
    w, h, fmt = u16(data, img + 0x04), u16(data, img + 0x06), u32(data, img + 0x08)
    texels = u32(data, img + 0x00)
    return w, h, fmt, bytes(data[texels:texels + image_size(w, h, fmt)])


def ramp_state(data, tex):
    """Decoded keys of the bank's image-index and TLUT tracks."""
    out = {}
    for fobj in auf.track_chain(data, u32(data, tex + auf.TEXANIM_AOBJ)):
        kind = data[fobj + auf.FOBJ_TRACK]
        if kind in (auf.TRACK_TIMG, auf.TRACK_TCLT):
            out[kind] = [(k.frame, k.value, k.op) for k in auf.ramp_keys(data, fobj)]
    return out


def bank_state(data, tex):
    n = u16(data, tex + auf.TEXANIM_N_IMAGES)
    return {
        "n_images": n,
        "n_tluts": u16(data, tex + auf.TEXANIM_N_TLUTS),
        "appended": image_state(data, tex, n - 1),
        "ramps": ramp_state(data, tex),
    }


def read_sidecar(path):
    """{donor name: [UiFrameBank fields]} out of the side-car archive."""
    arc = Archive(path)
    d = arc.data
    table = arc.publics[PUBLIC]
    files = {}
    i = 0
    while True:
        at = table + i * FILE_SIZE
        name_off = u32(d, at)
        if not name_off:
            break
        name = bytes(d[name_off:d.index(0, name_off)]).decode("ascii")
        n_banks, banks = u32(d, at + 0x04), u32(d, at + 0x08)
        rows = []
        for b in range(n_banks):
            o = banks + b * BANK_SIZE
            rows.append({
                "tex": u32(d, o), "n_images": u16(d, o + 0x04),
                "n_tluts": u16(d, o + 0x06), "tlut_src": u16(d, o + 0x08),
                "image": u32(d, o + 0x0C),
                "timg": u32(d, o + 0x10), "timg_buf": u32(d, o + 0x14),
                "timg_len": u32(d, o + 0x18),
                "tclt": u32(d, o + 0x1C), "tclt_buf": u32(d, o + 0x20),
                "tclt_len": u32(d, o + 0x24),
            })
        files[name] = rows
        i += 1
    return arc, files


def splice(donor, side, rows):
    """Apply the side-car to a donor the way the mod does, in one flat buffer.

    The mod works on two separately loaded archives and writes pointers between
    them; here both data sections share one address space, with the side-car
    placed after the donor, so the same pointer writes are expressible."""
    base = len(donor.data)
    data = bytearray(donor.data) + bytearray(side.data)

    for r in rows:
        tex = r["tex"]
        n = u16(data, tex + auf.TEXANIM_N_IMAGES)
        if n != r["n_images"]:
            raise SystemExit(f"  bank {tex:#x}: n_images {n} != side-car {r['n_images']}")

        imgs = [u32(data, u32(data, tex + auf.TEXANIM_IMAGES) + i * 4) for i in range(n)]
        tbl = len(data)
        data.extend(struct.pack(f">{n + 1}I", *imgs, base + r["image"]))
        struct.pack_into(">I", data, tex + auf.TEXANIM_IMAGES, tbl)
        struct.pack_into(">H", data, tex + auf.TEXANIM_N_IMAGES, n + 1)

        if r["tclt"]:
            nt = u16(data, tex + auf.TEXANIM_N_TLUTS)
            tluts = [u32(data, u32(data, tex + auf.TEXANIM_TLUTS) + i * 4) for i in range(nt)]
            tbl = len(data)
            data.extend(struct.pack(f">{nt + 1}I", *tluts, tluts[r["tlut_src"]]))
            struct.pack_into(">I", data, tex + auf.TEXANIM_TLUTS, tbl)
            struct.pack_into(">H", data, tex + auf.TEXANIM_N_TLUTS, nt + 1)

        for off, buf, ln in ((r["timg"], r["timg_buf"], r["timg_len"]),
                             (r["tclt"], r["tclt_buf"], r["tclt_len"])):
            if off:
                struct.pack_into(">I", data, off + auf.FOBJ_LENGTH, ln)
                struct.pack_into(">I", data, off + auf.FOBJ_BUFFER, base + buf)

    # The side-car's own pointers still hold data-section offsets; relocate the
    # ones the donor now reaches, which is every pointer the appended image uses.
    for reloc in side.relocs:
        at = base + reloc
        struct.pack_into(">I", data, at, base + u32(data, at))
    return data


def main(argv):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--iso-dir", default="iso/files")
    p.add_argument("--sidecar", default="mods/custom_machines/assets/ApUiFrames.dat")
    p.add_argument("--image", default="art/ap-icon.png")
    p.add_argument("--frames", type=int, default=20)
    p.add_argument("--source", type=int, default=4)
    p.add_argument("--new-frame", type=int, default=None)
    args = p.parse_args(argv[1:])
    if args.new_frame is None:
        args.new_frame = args.frames

    from PIL import Image
    im = Image.open(args.image).convert("RGBA")

    side, files = read_sidecar(args.sidecar)
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, rows in files.items():
            src = os.path.join(args.iso_dir, name + ".dat")

            auf.grow_archive(src, os.path.join(tmp, name + ".dat"), args, im)
            grown = Archive(os.path.join(tmp, name + ".dat"))
            spliced = splice(Archive(src), side, rows)

            # The rewrite only ever appends, so a bank sits at the same
            # data-section offset in the donor, the rewrite and the side-car.
            for r in rows:
                tex = r["tex"]
                want = bank_state(grown.data, tex)
                got = bank_state(spliced, tex)
                for field in ("n_images", "n_tluts", "appended", "ramps"):
                    if want[field] != got[field]:
                        bad += 1
                        print(f"  MISMATCH {name} bank {tex:#x} {field}:")
                        if field == "appended":
                            w, h, f, px = want[field]
                            w2, h2, f2, px2 = got[field]
                            print(f"    rewrite {w}x{h} fmt {f} {len(px)}B  "
                                  f"splice {w2}x{h2} fmt {f2} {len(px2)}B  "
                                  f"texels {'equal' if px == px2 else 'DIFFER'}")
                        else:
                            print(f"    rewrite {want[field]}")
                            print(f"    splice  {got[field]}")
    print(f"\n{'FAIL' if bad else 'OK'}: {sum(len(r) for r in files.values())} "
          f"bank(s) across {len(files)} archive(s), {bad} mismatch(es)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
