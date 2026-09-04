# SPDX-License-Identifier: GPL-3.0-only
"""Author the AP Box, the Archipelago-branded City Trial item box.

The box is carved out of ITKIND_BOXBLUE rather than generated, so the vanilla
material, PE flags and vertex shading come along unchanged.
Three things are rewritten on top of the carve:

  * The TEX0 index buffer grows from 4 entries to 24 - one UV pair per vertex -
    and the 24 TEX0 index bytes in the display list are re-indexed onto it, so
    each of the six faces maps its own cell of an atlas instead of all six
    sharing the full 0-1 range.
  * Each of the four ImageDescs the box's damage animation swaps between is
    repointed at a generated 4x2 atlas of 64x64 cells, six of them a face of the
    box. Four columns rather than three because TEX0 is stored as u8 with a
    shift of 7, so a cell edge has to land on a multiple of 1/128 - quarters do,
    thirds do not.
  * Every cell is edged the way the vanilla box is: a flat band around the face
    with a flat wedge at each corner. The band is the color of the face opposite
    this one on the hue wheel, so each of the six colors appears once as a face
    and once as an edge.

All box and event weights are zero, so the AP Box never enters a spawn pool. The
archipelago mod spawns it on its own timer.

Run from the repo root:
    uv run --with pillow python scripts/authoring/make_ap_box.py
"""

import colorsys
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, build_archive, u16, u32
from hsd.gx import GX_TF_CMPR, align32, decode_cmpr, encode_cmpr, image_size
from hsd.walker import Walker, carve_ranges

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ITEM_DAT = os.path.join(ROOT, "iso", "files", "Item.dat")
MARK = os.path.join(ROOT, "art", "ap-icon.png")
OUT = os.path.join(ROOT, "mods", "archipelago", "assets", "items", "ApBox.dat")

# Must match mods/custom_items/include/custom_items_api.h and ap_patches.h.
CUSTOM_ITEM_MAGIC = 0x4349544D  # 'CITM'
CUSTOM_ITEM_DESC_VERSION = 6
DESC_SIZE = 0x40
NAME = "AP Box"

ITKIND_BOXBLUE = 0
ITDATA_STRIDE = 0x18

GX_VA_TEX0 = 13
GX_INDEX8 = 2

# The six logo colors, in the logo's own ring order, matching the Archipelago
# Star spheres so the box reads as a set with them.
FACE_COLORS = [
    (201, 118, 130),  # rose
    (117, 194, 117),  # green
    (202, 148, 194),  # violet
    (217, 160, 125),  # tan
    (118, 126, 189),  # blue
    (238, 227, 145),  # yellow
]

ATLAS_COLS, ATLAS_ROWS = 4, 2
CELL = 64
ATLAS_W, ATLAS_H = ATLAS_COLS * CELL, ATLAS_ROWS * CELL

# Face drawn in each atlas cell. The two spare cells repeat the last face so a
# cell edge only ever blends into the same band color it already carries.
CELL_FACE = (0, 1, 2, 3, 4, 5, 5, 5)

# The band around each face carries the color of the face opposite it on the hue
# wheel - the order is tan, yellow, green, blue, violet, rose - so the six colors
# appear once each as a face and once each as an edge.
EDGE_PAIR = (1, 0, 5, 4, 3, 2)

# Band depth and corner wedge leg, in texels, following the vanilla box: a flat
# band with a flat wedge cutting each corner off. The wedge stops short of twice
# the band depth, so no 4x4 CMPR block holds more than two of the three colors.
BAND_TEX = 4
CORNER_TEX = 6
CORNER_COLOR = (0, 0, 0)

BAND_SAT = 1.35  # saturation and value over the face color the band is taken
BAND_VAL = 0.95  # from, so the band reads as an edge and not as another face

MARK_FRAC = 0.58  # Archipelago mark width, as a fraction of the cell
MARK_LIFT = 0.26  # how far the mark moves the face color toward white


def build_mark(size):
    """The Archipelago logo's silhouette, centered on a `size` field. Only the
    alpha is read - the mark is drawn in the face's own color, so none of the
    logo's six hues reaches the box."""
    from PIL import Image

    logo = Image.open(MARK).convert("RGBA").split()[3]
    logo = logo.crop(logo.getbbox())
    n = max(1, round(size * MARK_FRAC))
    out = Image.new("L", (size, size), 0)
    out.paste(logo.resize((n, n), Image.LANCZOS), ((size - n) // 2,) * 2)
    return out


def edge_color(face):
    """The band color a face carries: its opposite's color, deepened so the band
    reads as an edge rather than as a second face."""
    h, s, v = colorsys.rgb_to_hsv(*[c / 255.0 for c in FACE_COLORS[EDGE_PAIR[face]]])
    rgb = colorsys.hsv_to_rgb(h, min(1.0, s * BAND_SAT), v * BAND_VAL)
    return tuple(round(c * 255) for c in rgb)


def draw_edges(atlas, x0, y0, face):
    """Stamp the band and its four corner wedges over the cell at (x0, y0)."""
    band = edge_color(face)
    put = atlas.load()
    for y in range(CELL):
        for x in range(CELL):
            dx, dy = min(x, CELL - 1 - x), min(y, CELL - 1 - y)
            if dx + dy < CORNER_TEX:
                put[x0 + x, y0 + y] = CORNER_COLOR
            elif dx < BAND_TEX or dy < BAND_TEX:
                put[x0 + x, y0 + y] = band


def build_cells(frames):
    """The atlas cell sets, one list of six per damage stage.

    A cell is the face color carrying the Archipelago mark as a lightness lift.
    The edges are drawn later, over the finished cell: the vanilla box's border
    does not crack, and neither does this one.

    Each cracked stage is the base cell multiplied by the vanilla box's own
    luminance loss at that stage, so a crack darkens the face exactly where it
    darkens the vanilla box, and nothing else moves."""
    from PIL import Image, ImageChops

    size = frames[0].size
    mark = build_mark(size[0])

    base = []
    for color in FACE_COLORS:
        hi = tuple(round(c + (255 - c) * MARK_LIFT) for c in color)
        im = Image.new("RGB", size, color)
        put, m = im.load(), mark.load()
        for y in range(size[1]):
            for x in range(size[0]):
                t = m[x, y] / 255.0
                if t:
                    put[x, y] = tuple(
                        round(color[i] + (hi[i] - color[i]) * t) for i in range(3)
                    )
        base.append(im)

    out = [base]
    lum0 = frames[0].convert("L").load()
    for stage in frames[1:]:
        lum = stage.convert("L").load()
        loss = Image.new("L", size)
        put = loss.load()
        for y in range(size[1]):
            for x in range(size[0]):
                put[x, y] = min(255, 255 * (lum[x, y] + 1) // (lum0[x, y] + 1))
        loss = loss.convert("RGB")
        out.append([ImageChops.multiply(c, loss) for c in base])
    return out


def build_atlas(cells):
    """Six faces laid onto one ATLAS_W x ATLAS_H image, each with its edges."""
    from PIL import Image

    atlas = Image.new("RGB", (ATLAS_W, ATLAS_H))
    for i, face in enumerate(CELL_FACE):
        x0, y0 = (i % ATLAS_COLS) * CELL, (i // ATLAS_COLS) * CELL
        atlas.paste(cells[face].resize((CELL, CELL), Image.LANCZOS), (x0, y0))
        draw_edges(atlas, x0, y0, face)
    return atlas


def vtx_entries(data, off):
    """The VtxDescList at `off` as [(entry_off, attr, atype)], terminator last."""
    out = []
    while True:
        attr = u32(data, off)
        atype = u32(data, off + 4)
        out.append((off, attr, atype))
        if attr == 0xFF:
            return out
        off += 0x18


def main():
    arc = Archive(ITEM_DAT)
    d = arc.data
    if "itData" not in arc.publics:
        raise SystemExit("public symbol 'itData' not found - is this Item.dat?")

    itdata = arc.publics["itData"] + ITKIND_BOXBLUE * ITDATA_STRIDE
    model_pair = u32(d, itdata + 0x08)
    root_jobj = u32(d, model_pair)
    model_flag = u32(d, model_pair + 4)
    mat_anim = u32(d, u32(d, itdata + 0x0C) + 0x04)
    if mat_anim == 0:
        raise SystemExit("kind 0 anim slot 0 has no material animation")

    walker = Walker(arc)
    walker.walk(root_jobj, "JOBJDesc")
    model_objs = dict(walker.visited)
    visited = walker.walk(mat_anim, "MatAnimJoint")

    pobjs = [o for o, (t, _) in model_objs.items() if t == "POBJDesc"]
    imgs = [o for o, (t, _) in model_objs.items() if t == "ImageDesc"]
    tex_anims = [o for o, (t, _) in visited.items() if t == "TexAnim"]
    if len(pobjs) != 1 or len(imgs) != 1 or len(tex_anims) != 1:
        raise SystemExit(
            f"expected one POBJ, ImageDesc and TexAnim, got "
            f"{len(pobjs)} / {len(imgs)} / {len(tex_anims)}"
        )
    pobj, img_desc, tex_anim = pobjs[0], imgs[0], tex_anims[0]

    img_tbl = u32(d, tex_anim + 0x0C)
    n_frames = u16(d, tex_anim + 0x14)
    frame_descs = [u32(d, img_tbl + i * 4) for i in range(n_frames)]
    if n_frames != 4 or 0 in frame_descs:
        raise SystemExit(f"expected a 4-entry image table, got {n_frames}")
    vtx_list = u32(d, pobj + 0x08)
    dl_off = u32(d, pobj + 0x10)

    entries = vtx_entries(d, vtx_list)
    stride = 0
    tex_entry = tex_byte = None
    for entry, attr, atype in entries:
        if attr == 0xFF:
            break
        if atype != GX_INDEX8:
            raise SystemExit(
                f"attr {attr} is not INDEX8 - the re-index assumes "
                "one index byte per attribute"
            )
        if attr == GX_VA_TEX0:
            tex_entry, tex_byte = entry, stride
        stride += 1
    if tex_entry is None:
        raise SystemExit("model has no TEX0 attribute")

    tex_blob = u32(d, tex_entry + 0x14)
    tex_stride = u16(d, tex_entry + 0x12)

    prim, count = d[dl_off], u16(d, dl_off + 1)
    if prim & 0xF8 != 0x80 or count != 24:
        raise SystemExit(
            f"expected a 24-vertex GX_QUADS list, got prim {prim:#04x} count {count}"
        )
    print(
        f"  box model: JOBJDesc={root_jobj:#x} flag={model_flag:#010x}, "
        f"{count} vertices, TEX0 index at byte {tex_byte} of {stride}"
    )

    # Every vertex gets its own UV pair, keeping the corner of the cell that the
    # source's 4-entry table gave it so each face's orientation is preserved.
    old_uv = [
        (d[tex_blob + i * tex_stride], d[tex_blob + i * tex_stride + 1])
        for i in range(4)
    ]
    new_uv = []
    for i in range(count):
        su, sv = old_uv[d[dl_off + 3 + i * stride + tex_byte]]
        face = i // 4
        col, row = face % ATLAS_COLS, face // ATLAS_COLS
        u = (col + (1 if su else 0)) * (128 // ATLAS_COLS)
        v = (row + (1 if sv else 0)) * (128 // ATLAS_ROWS)
        new_uv.append((u, v))

    frames = []
    for desc in frame_descs:
        blob = u32(d, desc + 0x00)
        iw, ih, ifmt = u16(d, desc + 0x04), u16(d, desc + 0x06), u32(d, desc + 0x08)
        frames.append(decode_cmpr(d[blob : blob + image_size(iw, ih, ifmt)], iw, ih))
    atlases = [encode_cmpr(build_atlas(cells)) for cells in build_cells(frames)]
    print(
        f"  atlases: {len(atlases)} x {ATLAS_W}x{ATLAS_H} CMPR from "
        f"{frames[0].width}x{frames[0].height} sources"
    )

    # The old TEX0 table and every source texture are replaced, so neither is
    # carried and none of their pointers is translated by carve_ranges. The
    # animation itself is carved unchanged - only what its ImageDescs point at
    # moves. The model's ImageDesc shares stage 0's buffer, as it does in the
    # source.
    del visited[tex_blob]
    for blob in {u32(d, o) for o in frame_descs + [img_desc]}:
        del visited[blob]

    name_bytes = NAME.encode("ascii") + b"\0"
    prefix = bytearray((DESC_SIZE + len(name_bytes) + 3) & ~3)
    prefix[DESC_SIZE : DESC_SIZE + len(name_bytes)] = name_bytes

    res = carve_ranges(
        arc,
        visited,
        prefix,
        base_relocs=(0x08, 0x14, 0x3C),
        skip_relocs=[tex_entry + 0x14, img_desc + 0x00]
        + [o + 0x00 for o in frame_descs],
    )
    new_data, remap, relocs = res.data, res.remap, res.relocs
    print(f"  reached {len(visited)} objects, kept {len(res.intervals)} ranges")

    struct.pack_into(">I", new_data, 0x00, CUSTOM_ITEM_MAGIC)
    struct.pack_into(">HH", new_data, 0x04, CUSTOM_ITEM_DESC_VERSION, 0)
    struct.pack_into(">I", new_data, 0x08, DESC_SIZE)  # name (reloc)
    struct.pack_into(">i", new_data, 0x0C, ITKIND_BOXBLUE)  # base_kind
    struct.pack_into(">I", new_data, 0x10, 0)  # flags
    struct.pack_into(">I", new_data, 0x14, remap[root_jobj])  # model (reloc)
    struct.pack_into(">I", new_data, 0x18, 0)  # effect_info
    struct.pack_into(">3H", new_data, 0x1C, 0, 0, 0)  # weight_box
    struct.pack_into(">H", new_data, 0x22, 0)  # weight_free
    struct.pack_into(">6H", new_data, 0x24, 0, 0, 0, 0, 0, 0)  # weight_event
    struct.pack_into(">I", new_data, 0x30, model_flag)
    struct.pack_into(">f", new_data, 0x34, 0.0)  # scale (inherit)
    struct.pack_into(">I", new_data, 0x38, 0)  # joint_anim
    struct.pack_into(">I", new_data, 0x3C, remap[mat_anim])  # mat_anim (reloc)

    dl_new = remap[dl_off]
    for i in range(count):
        new_data[dl_new + 3 + i * stride + tex_byte] = i

    def append(blob, cacheline=False):
        pad = align32(len(new_data)) if cacheline else (-len(new_data)) & 3
        new_data.extend(b"\0" * pad)
        off = len(new_data)
        new_data.extend(blob)
        return off

    uv_new = append(b"".join(bytes((u & 0xFF, v & 0xFF)) for u, v in new_uv), True)
    struct.pack_into(">I", new_data, remap[tex_entry] + 0x14, uv_new)
    relocs.append(remap[tex_entry] + 0x14)

    atlas_offs = [append(blob, True) for blob in atlases]
    for desc, off in zip([img_desc] + frame_descs, [atlas_offs[0]] + atlas_offs):
        struct.pack_into(
            ">IHHI", new_data, remap[desc], off, ATLAS_W, ATLAS_H, GX_TF_CMPR
        )
        relocs.append(remap[desc])

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    out = build_archive(new_data, relocs, [("customItem", 0)], arc.version)
    with open(OUT, "wb") as f:
        f.write(out)
    print(f"  wrote {OUT} ({len(out) / 1024:.1f} KB, public 'customItem')")
    return 0


if __name__ == "__main__":
    sys.exit(main())
