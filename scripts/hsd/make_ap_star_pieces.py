# SPDX-License-Identifier: GPL-3.0-only
"""Author the six Archipelago Star assembly pieces, world models and HUD icons.

Each piece is a solid-colored sphere in one of the six colors of the Archipelago
logo, in the logo's own ring order (rose at the top, then clockwise). They are
`customItem` archives exactly like the ones carve_custom_item.py produces, but
the model is generated here rather than carved out of Item.dat - Item.dat holds
no sphere.

A piece clones ITKIND_HYDRA1's behavior (a legendary machine piece: the physics,
the state class, the threshold category) and overrides the model and the effect
record. The effect record carries no entries, so touching a piece grants no stat
and never reaches the type 27-32 arms in Machine_OnTouchItem that would credit a
vanilla Hydra/Dragoon piece. Collection is driven entirely by the custom_items
pickup handler the archipelago mod registers. The NO_MAT_ANIM flag keeps the
Hydra piece's material animation off the sphere - it drives the diffuse color
over a 240-frame loop, which would wash out the color that identifies the piece.

All three box weights and all six event weights are zero: pieces never enter a
spawn pool. They arrive only through the forced-content red box the archipelago
mod schedules, mirroring how the vanilla pieces are delivered.

The model is a UV sphere with per-vertex normals, lit the way the vanilla piece
parts are (CONSTANT | DIFFUSE material, no texture). Its radius matches a Hydra
piece's half-extent, and the descriptor's scale brings it down from there - a
sphere reads as bulkier than the flat, hollow shapes of a vanilla piece at the
same extent. Culling is off - a closed sphere reads identically either way and it
removes any dependence on winding.

The same run writes ApPieceIcons.dat, the collection tracker's art: one alpha-cut
textured quad per color under a single `apPieceIcons_scene_models` public, sized
and shaped like the vanilla Hydra/Dragoon piece icons so a sphere sits in the same
row of anchors those use.

Run from the repo root:
    uv run --with pillow python scripts/hsd/make_ap_star_pieces.py
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.archive import build_archive
from hsd.gx import GX_TF_RGB5A3, align32, encode_rgb5a3

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(ROOT, "mods", "archipelago", "assets", "items")
ICON_DAT = os.path.join(ROOT, "mods", "archipelago", "assets", "ApPieceIcons.dat")

ARCHIVE_VERSION = b"001B"
PUBLIC = "customItem"

# Must match mods/custom_items/include/custom_items_api.h.
CUSTOM_ITEM_MAGIC = 0x4349544D  # 'CITM'
CUSTOM_ITEM_DESC_VERSION = 4
CUSTOM_ITEM_FLAG_NO_MAT_ANIM = 0x1
DESC_SIZE = 0x38

ITKIND_HYDRA1 = 55
ITGROUP_GOOD = 1
MODEL_FLAG_SIMPLE = 0x02000000  # itData render flag for an unskinned model

# The six logo colors, sampled from art/ap-icon.png, in the
# logo's ring order: rose at 12 o'clock, then clockwise.
PIECES = [
    ("Rose",   (201, 118, 130)),
    ("Green",  (117, 194, 117)),
    ("Violet", (202, 148, 194)),
    ("Tan",    (217, 160, 125)),
    ("Blue",   (118, 126, 189)),
    ("Yellow", (238, 227, 145)),
]

# A Hydra piece's model is 2.08 units across the half-extent and renders at a
# scale factor of 2.8, so a sphere of this radius arrives at the same size.
SPHERE_RADIUS = 2.08
SPHERE_SEGMENTS = 12  # longitude divisions
SPHERE_RINGS = 8      # latitude bands, poles included
SPHERE_SCALE = 0.7    # descriptor scale over the Hydra piece's scale factor

# Struct sizes.
SZ_JOBJ = 0x40
SZ_DOBJ = 0x10
SZ_MOBJ = 0x18
SZ_MAT = 0x14
SZ_POBJ = 0x18
SZ_VTX_ENTRY = 0x18
SZ_EFFECT_INFO = 0x0C

# JOBJ flags.
JOBJ_CLASSICAL_SCALING = 0x00000008
JOBJ_LIGHTING = 0x00000080
JOBJ_OPA = 0x00040000
JOBJ_ROOT_OPA = 0x10000000

# Lit, untextured material: material color as the constant, diffuse lighting on,
# alpha from the material. The vanilla piece parts use this with TEX0 added.
MOBJ_RENDER_MODE = 0x00002005

# GX vertex attribute constants.
GX_VA_POS = 9
GX_VA_NRM = 10
GX_INDEX8 = 2
GX_POS_XYZ = 1
GX_NRM_XYZ = 0
GX_F32 = 4

GX_TRIANGLESTRIP = 0x98  # | VTXFMT0
GX_TRIANGLEFAN = 0xA0

# Ambient is the color darkened, so an unlit face keeps its hue instead of going
# black.
AMBIENT_SCALE = 0.55

ICON_PUBLIC = "apPieceIcons_scene_models"
ICON_TEX = 32           # texture is square; the ball fills it
ICON_HALF = 1.0         # HUD-space half extent, against the anchors' 2.5 spacing
ICON_SUPERSAMPLE = 4

# Alpha-cut quad: unlit CONSTANT material, TEX0, NO_ZUPDATE, XLU pass, alpha from
# the material modulated by the texture, so the ball's edge cuts itself out.
ICON_RENDER_MODE = 0x60002011
GX_VA_TEX0 = 13
GX_TEX_ST = 1
JOBJ_XLU = 0x00080000
JOBJ_ROOT_XLU = 0x20000000
TOBJ_FLAGS = 0x00340010  # COORD_UV | LIGHTMAP_DIFFUSE | CM_MODULATE | AM_MODULATE
POBJ_FLAGS_CULLFRONT = 0x8000
SZ_JOBJSET = 0x10
SZ_TOBJ = 0x5C
SZ_IMG = 0x18


def sphere_mesh(radius, segments, rings):
    """A UV sphere about the origin. Returns (positions, normals, prims), where a
    prim is (gx_opcode, [vertex index, ...]) and positions/normals are parallel."""
    positions = [(0.0, radius, 0.0)]
    normals = [(0.0, 1.0, 0.0)]
    band_rows = []
    for i in range(1, rings):
        phi = math.pi * i / rings
        ny, ring_r = math.cos(phi), math.sin(phi)
        row = []
        for j in range(segments):
            theta = 2.0 * math.pi * j / segments
            nx, nz = ring_r * math.cos(theta), ring_r * math.sin(theta)
            row.append(len(positions))
            positions.append((radius * nx, radius * ny, radius * nz))
            normals.append((nx, ny, nz))
        band_rows.append(row)
    south = len(positions)
    positions.append((0.0, -radius, 0.0))
    normals.append((0.0, -1.0, 0.0))

    prims = [(GX_TRIANGLEFAN,
              [0] + [band_rows[0][j % segments] for j in range(segments + 1)])]
    for b in range(len(band_rows) - 1):
        upper, lower = band_rows[b], band_rows[b + 1]
        strip = []
        for j in range(segments + 1):
            jj = j % segments
            strip.extend((lower[jj], upper[jj]))
        prims.append((GX_TRIANGLESTRIP, strip))
    prims.append((GX_TRIANGLEFAN,
                  [south] + [band_rows[-1][(segments - j) % segments]
                             for j in range(segments + 1)]))
    return positions, normals, prims


def display_list(prims):
    """GX display list over INDEX8 position + normal, padded to 32 bytes."""
    dl = bytearray()
    for opcode, indices in prims:
        dl.append(opcode)
        dl.extend(struct.pack(">H", len(indices)))
        for i in indices:
            dl.extend((i, i))  # positions and normals share an index
    dl.extend(b"\0" * align32(len(dl)))
    return bytes(dl)


def rgba(color, scale=1.0):
    r, g, b = (min(255, int(round(c * scale))) for c in color)
    return (r << 24) | (g << 16) | (b << 8) | 0xFF


def build_piece(name, color, positions, normals, prims):
    data = bytearray()
    relocs = []

    def reserve(size):
        off = len(data)
        data.extend(b"\0" * size)
        return off

    def blob(payload):
        data.extend(b"\0" * align32(len(data)))
        off = len(data)
        data.extend(payload)
        return off

    def w32(off, val):
        struct.pack_into(">I", data, off, val & 0xFFFFFFFF)

    def wf32(off, val):
        struct.pack_into(">f", data, off, val)

    def ptr(off, target):
        w32(off, target)
        relocs.append(off)

    desc = reserve(DESC_SIZE)
    name_bytes = name.encode("ascii") + b"\0"
    name_off = reserve(len(name_bytes))
    data[name_off:name_off + len(name_bytes)] = name_bytes
    reserve(-len(data) & 3)  # 4-align what follows

    effect = reserve(SZ_EFFECT_INFO)
    root = reserve(SZ_JOBJ)
    child = reserve(SZ_JOBJ)
    dobj = reserve(SZ_DOBJ)
    mobj = reserve(SZ_MOBJ)
    mat = reserve(SZ_MAT)
    pobj = reserve(SZ_POBJ)
    vtx = reserve(3 * SZ_VTX_ENTRY)

    pos_off = blob(b"".join(struct.pack(">3f", *p) for p in positions))
    nrm_off = blob(b"".join(struct.pack(">3f", *n) for n in normals))
    dl = display_list(prims)
    dl_off = blob(dl)

    # CustomItemDesc: clone a legendary piece, override model and effect record.
    w32(desc + 0x00, CUSTOM_ITEM_MAGIC)
    struct.pack_into(">HH", data, desc + 0x04, CUSTOM_ITEM_DESC_VERSION, 0)
    ptr(desc + 0x08, name_off)
    struct.pack_into(">i", data, desc + 0x0C, ITKIND_HYDRA1)   # base_kind
    w32(desc + 0x10, CUSTOM_ITEM_FLAG_NO_MAT_ANIM)             # flags
    ptr(desc + 0x14, root)                                     # model
    ptr(desc + 0x18, effect)                                   # effect_info
    struct.pack_into(">3H", data, desc + 0x1C, 0, 0, 0)        # weight_box
    struct.pack_into(">H", data, desc + 0x22, 0)               # weight_free
    struct.pack_into(">6H", data, desc + 0x24, 0, 0, 0, 0, 0, 0)  # weight_event
    w32(desc + 0x30, MODEL_FLAG_SIMPLE)
    wf32(desc + 0x34, SPHERE_SCALE)                            # scale

    # PatchEffectInfo: no entries, so pickup grants nothing and dispatches nowhere.
    w32(effect + 0x00, 0)  # entries
    w32(effect + 0x04, 0)  # count
    w32(effect + 0x08, ITGROUP_GOOD)

    w32(root + 0x04, JOBJ_ROOT_OPA | JOBJ_CLASSICAL_SCALING)
    ptr(root + 0x08, child)
    wf32(root + 0x20, 1.0); wf32(root + 0x24, 1.0); wf32(root + 0x28, 1.0)

    w32(child + 0x04, JOBJ_OPA | JOBJ_LIGHTING | JOBJ_CLASSICAL_SCALING)
    ptr(child + 0x10, dobj)
    wf32(child + 0x20, 1.0); wf32(child + 0x24, 1.0); wf32(child + 0x28, 1.0)

    ptr(dobj + 0x08, mobj)
    ptr(dobj + 0x0C, pobj)

    w32(mobj + 0x04, MOBJ_RENDER_MODE)
    ptr(mobj + 0x0C, mat)

    w32(mat + 0x00, rgba(color, AMBIENT_SCALE))  # ambient
    w32(mat + 0x04, rgba(color))                 # diffuse
    w32(mat + 0x08, rgba(color))                 # specular, unused without the bit
    wf32(mat + 0x0C, 1.0)                        # alpha
    wf32(mat + 0x10, 50.0)                       # shininess

    ptr(pobj + 0x08, vtx)
    struct.pack_into(">HH", data, pobj + 0x0C, 0, len(dl) // 32)  # no culling
    ptr(pobj + 0x10, dl_off)

    def vtx_entry(base, attr, comp_cnt, stride, vptr):
        w32(base + 0x00, attr)
        w32(base + 0x04, GX_INDEX8)
        w32(base + 0x08, comp_cnt)
        w32(base + 0x0C, GX_F32)
        data[base + 0x10] = 0  # frac
        struct.pack_into(">H", data, base + 0x12, stride)
        ptr(base + 0x14, vptr)

    vtx_entry(vtx + 0 * SZ_VTX_ENTRY, GX_VA_POS, GX_POS_XYZ, 12, pos_off)
    vtx_entry(vtx + 1 * SZ_VTX_ENTRY, GX_VA_NRM, GX_NRM_XYZ, 12, nrm_off)
    w32(vtx + 2 * SZ_VTX_ENTRY + 0x00, 0xFF)  # terminator

    return build_archive(data, relocs, [(PUBLIC, desc)], ARCHIVE_VERSION)


def icon_image(color):
    """A shaded ball on transparent ground, lit from the upper left like the
    scene lights the world models. Supersampled, so the resize gives the rim a
    soft alpha edge instead of a stair-step."""
    from PIL import Image
    n = ICON_TEX * ICON_SUPERSAMPLE
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    px = im.load()
    c = (n - 1) / 2.0
    radius = n / 2.0 - ICON_SUPERSAMPLE * 0.5
    lx, ly, lz = -0.45, -0.50, 0.74
    for y in range(n):
        for x in range(n):
            dx, dy = (x - c) / radius, (y - c) / radius
            d2 = dx * dx + dy * dy
            if d2 > 1.0:
                continue
            nz = math.sqrt(1.0 - d2)
            lam = max(0.0, dx * lx + dy * ly + nz * lz)
            shade = 0.45 + 0.55 * lam
            spec = 255.0 * 0.6 * (lam ** 16)
            px[x, y] = tuple(min(255, int(v * shade + spec)) for v in color) + (255,)
    return im.resize((ICON_TEX, ICON_TEX), Image.LANCZOS)


def build_icon_archive(colors):
    """One JOBJSet per color under a single NULL-terminated scene_models public."""
    data = bytearray()
    relocs = []

    def reserve(size):
        off = len(data)
        data.extend(b"\0" * size)
        return off

    def blob(payload):
        data.extend(b"\0" * align32(len(data)))
        off = len(data)
        data.extend(payload)
        return off

    def w32(off, val):
        struct.pack_into(">I", data, off, val & 0xFFFFFFFF)

    def wf32(off, val):
        struct.pack_into(">f", data, off, val)

    def ptr(off, target):
        w32(off, target)
        relocs.append(off)

    n = len(colors)
    public_arr = reserve(4 * (n + 1))
    icons = []
    for _ in colors:
        icons.append(dict(jobjset=reserve(SZ_JOBJSET), root=reserve(SZ_JOBJ),
                          child=reserve(SZ_JOBJ), dobj=reserve(SZ_DOBJ),
                          mobj=reserve(SZ_MOBJ), tobj=reserve(SZ_TOBJ),
                          mat=reserve(SZ_MAT), img=reserve(SZ_IMG),
                          pobj=reserve(SZ_POBJ), vtx=reserve(3 * SZ_VTX_ENTRY)))

    h = ICON_HALF
    positions = struct.pack(">12f", -h, -h, 0.0, -h, h, 0.0, h, -h, 0.0, h, h, 0.0)
    uvs = struct.pack(">8f", 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0)
    dl = bytes([0x98, 0x00, 0x04, 0, 0, 1, 1, 2, 2, 3, 3])
    dl += b"\0" * align32(len(dl))
    pos_off, uv_off, dl_off = blob(positions), blob(uvs), blob(dl)
    for icon, color in zip(icons, colors):
        icon["tex_off"] = blob(encode_rgb5a3(icon_image(color)))

    def vtx_entry(base, attr, comp_cnt, stride, vptr):
        w32(base + 0x00, attr)
        w32(base + 0x04, GX_INDEX8)
        w32(base + 0x08, comp_cnt)
        w32(base + 0x0C, GX_F32)
        data[base + 0x10] = 0
        struct.pack_into(">H", data, base + 0x12, stride)
        ptr(base + 0x14, vptr)

    for i, icon in enumerate(icons):
        ptr(public_arr + i * 4, icon["jobjset"])
        ptr(icon["jobjset"] + 0x00, icon["root"])

        w32(icon["root"] + 0x04, JOBJ_ROOT_XLU | JOBJ_CLASSICAL_SCALING)
        ptr(icon["root"] + 0x08, icon["child"])
        wf32(icon["root"] + 0x20, 1.0); wf32(icon["root"] + 0x24, 1.0); wf32(icon["root"] + 0x28, 1.0)

        w32(icon["child"] + 0x04, JOBJ_XLU | JOBJ_CLASSICAL_SCALING)
        ptr(icon["child"] + 0x10, icon["dobj"])
        wf32(icon["child"] + 0x20, 1.0); wf32(icon["child"] + 0x24, 1.0); wf32(icon["child"] + 0x28, 1.0)

        ptr(icon["dobj"] + 0x08, icon["mobj"])
        ptr(icon["dobj"] + 0x0C, icon["pobj"])

        w32(icon["mobj"] + 0x04, ICON_RENDER_MODE)
        ptr(icon["mobj"] + 0x08, icon["tobj"])
        ptr(icon["mobj"] + 0x0C, icon["mat"])

        w32(icon["tobj"] + 0x0C, 4)  # TEX0 coords
        wf32(icon["tobj"] + 0x1C, 1.0); wf32(icon["tobj"] + 0x20, 1.0); wf32(icon["tobj"] + 0x24, 1.0)
        data[icon["tobj"] + 0x3C] = 1
        data[icon["tobj"] + 0x3D] = 1
        w32(icon["tobj"] + 0x40, TOBJ_FLAGS)
        wf32(icon["tobj"] + 0x44, 1.0)
        w32(icon["tobj"] + 0x48, 1)
        ptr(icon["tobj"] + 0x4C, icon["img"])

        w32(icon["mat"] + 0x00, 0xFFFFFFFF)
        w32(icon["mat"] + 0x04, 0xFFFFFFFF)
        w32(icon["mat"] + 0x08, 0xFFFFFFFF)
        wf32(icon["mat"] + 0x0C, 1.0)
        wf32(icon["mat"] + 0x10, 50.0)

        ptr(icon["img"] + 0x00, icon["tex_off"])
        struct.pack_into(">HH", data, icon["img"] + 0x04, ICON_TEX, ICON_TEX)
        w32(icon["img"] + 0x08, GX_TF_RGB5A3)

        ptr(icon["pobj"] + 0x08, icon["vtx"])
        struct.pack_into(">HH", data, icon["pobj"] + 0x0C, POBJ_FLAGS_CULLFRONT, len(dl) // 32)
        ptr(icon["pobj"] + 0x10, dl_off)

        vtx_entry(icon["vtx"] + 0 * SZ_VTX_ENTRY, GX_VA_POS, GX_POS_XYZ, 12, pos_off)
        vtx_entry(icon["vtx"] + 1 * SZ_VTX_ENTRY, GX_VA_TEX0, GX_TEX_ST, 8, uv_off)
        w32(icon["vtx"] + 2 * SZ_VTX_ENTRY + 0x00, 0xFF)

    return build_archive(data, relocs, [(ICON_PUBLIC, public_arr)], ARCHIVE_VERSION)


def main():
    positions, normals, prims = sphere_mesh(SPHERE_RADIUS, SPHERE_SEGMENTS, SPHERE_RINGS)
    tris = sum(len(v) - 2 for _, v in prims)
    print(f"sphere r={SPHERE_RADIUS} scale={SPHERE_SCALE} {SPHERE_SEGMENTS}x{SPHERE_RINGS}: "
          f"{len(positions)} verts, {tris} triangles, {len(prims)} primitives")
    if len(positions) > 255:
        raise SystemExit("index8 vertex arrays hold 255 entries; lower the resolution")

    os.makedirs(OUT_DIR, exist_ok=True)
    for color_name, color in PIECES:
        name = f"AP Sphere {color_name}"
        out = os.path.join(OUT_DIR, f"ApSphere{color_name}.dat")
        archive = build_piece(name, color, positions, normals, prims)
        with open(out, "wb") as f:
            f.write(archive)
        print(f"  {name:22s} #{color[0]:02X}{color[1]:02X}{color[2]:02X}  "
              f"{os.path.relpath(out, ROOT)} ({len(archive)} bytes)")

    icons = build_icon_archive([color for _, color in PIECES])
    with open(ICON_DAT, "wb") as f:
        f.write(icons)
    print(f"  {len(PIECES)} HUD icons {ICON_TEX}x{ICON_TEX} RGB5A3  "
          f"{os.path.relpath(ICON_DAT, ROOT)} ({len(icons) / 1024:.1f} KB, "
          f"public '{ICON_PUBLIC}')")


if __name__ == "__main__":
    main()
