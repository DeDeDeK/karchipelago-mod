# SPDX-License-Identifier: GPL-3.0-only
"""Author the six Archipelago Star assembly pieces, world models and HUD icons.

Each piece is a solid-colored sphere in one of the six colors of the Archipelago
logo, in the logo's own ring order (rose at the top, then clockwise). They are
`customItem` archives exactly like the ones carve_custom_item.py produces, but
the model is generated here rather than carved out of Item.dat - Item.dat holds
no sphere.

A piece clones ITKIND_HYDRA1's behavior (a legendary machine piece: the physics,
the state class, the threshold category) and overrides the model, the effect
record and the joint animation. The effect record carries no entries, so touching
a piece grants no stat and never reaches the type 27-32 arms in
Machine_OnTouchItem that would credit a vanilla Hydra/Dragoon piece. Collection
is driven entirely by the custom_items pickup handler the archipelago mod
registers. The NO_MAT_ANIM flag keeps the Hydra piece's material animation off
the sphere - it drives the diffuse color over a 240-frame loop, which would wash
out the color that identifies the piece.

All three box weights and all six event weights are zero: pieces never enter a
spawn pool. They arrive only through the forced-content red box the archipelago
mod schedules, mirroring how the vanilla pieces are delivered.

The model is a UV sphere with per-vertex normals, lit the way the vanilla piece
parts are (CONSTANT | DIFFUSE material, no texture). Its radius matches a Hydra
piece's half-extent, and the descriptor's scale brings it down from there - a
sphere reads as bulkier than the flat, hollow shapes of a vanilla piece at the
same extent.

The joint animation is authored here rather than inherited. The Hydra piece's
squashes X and Y between 1.0 and 0.7 every 30 frames, which on a sphere reads as
a heavy throb; this one breathes uniformly to PULSE_MIN on the same cadence.

The same run writes ApPieceIcons.dat, the collection tracker's art: one alpha-cut
textured quad per color under a single `apPieceIcons_scene_models` public, sized
and shaped like the vanilla Hydra/Dragoon piece icons so a sphere sits in the same
row of anchors those use.

Run from the repo root:
    uv run --with pillow python scripts/authoring/make_ap_star_pieces.py
"""

import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.archive import Blob, build_archive
from hsd.fobj import FMT_FLOAT, JOINT_TRACK, OP_SPL0, Key, encode
from hsd.gx import GX_TF_RGB5A3, align32, encode_rgb5a3
from hsd.quad_model import (
    GX_F32,
    GX_INDEX8,
    GX_POS_XYZ,
    GX_VA_POS,
    JOBJ_CLASSICAL_SCALING,
    JOBJ_ROOT_XLU,
    JOBJ_XLU,
    POBJ_CULLBACK,
    QUAD_DL,
    QUAD_UVS,
    SZ_DOBJ,
    SZ_JOBJ,
    SZ_JOBJSET,
    SZ_MAT,
    SZ_MOBJ,
    SZ_POBJ,
    SZ_VTX_ENTRY,
    quad_positions,
    reserve_quad,
    write_jobj,
    write_quad,
)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(ROOT, "mods", "ap_star", "assets", "items")
ICON_DAT = os.path.join(ROOT, "mods", "ap_star", "assets", "ApPieceIcons.dat")

PUBLIC = "customItem"

# Must match mods/custom_items/include/custom_items_api.h.
CUSTOM_ITEM_MAGIC = 0x4349544D  # 'CITM'
CUSTOM_ITEM_DESC_VERSION = 5
CUSTOM_ITEM_FLAG_NO_MAT_ANIM = 0x1
DESC_SIZE = 0x3C

ITKIND_HYDRA1 = 55
ITGROUP_GOOD = 1
MODEL_FLAG_SIMPLE = 0x02000000  # itData render flag for an unskinned model

# The six logo colors, sampled from art/ap-icon.png, in the
# logo's ring order: rose at 12 o'clock, then clockwise.
PIECES = [
    ("Rose", (201, 118, 130)),
    ("Green", (117, 194, 117)),
    ("Violet", (202, 148, 194)),
    ("Tan", (217, 160, 125)),
    ("Blue", (118, 126, 189)),
    ("Yellow", (238, 227, 145)),
]

# A Hydra piece's model is 2.08 units across the half-extent and renders at a
# scale factor of 2.8, so a sphere of this radius arrives at the same size.
SPHERE_RADIUS = 2.08
SPHERE_SEGMENTS = 16  # longitude divisions
SPHERE_RINGS = 12  # latitude bands, poles included
SPHERE_SCALE = 0.525  # descriptor scale over the Hydra piece's scale factor

# The authored joint animation: a uniform breath between 1.0 and PULSE_MIN,
# holding the base kind's 30-frame half period and 240-frame loop.
PULSE_MIN = 0.94
PULSE_HALF_PERIOD = 30
PULSE_END_FRAME = 240

# Struct sizes not shared with the textured-quad leaf.
SZ_EFFECT_INFO = 0x0C
SZ_ANIMJOINT = 0x14
SZ_AOBJDESC = 0x10
SZ_FOBJDESC = 0x14

# JOBJ flags.
JOBJ_LIGHTING = 0x00000080
JOBJ_OPA = 0x00040000
JOBJ_ROOT_OPA = 0x10000000

# Lit, untextured material: material color as the constant, diffuse lighting on,
# alpha from the material. The vanilla piece parts use this with TEX0 added.
MOBJ_RENDER_MODE = 0x00002005

# GX vertex attribute constants beyond the quad's POS/TEX0 pair.
GX_VA_NRM = 10
GX_NRM_XYZ = 0

GX_TRIANGLESTRIP = 0x98  # | VTXFMT0
GX_TRIANGLEFAN = 0xA0

# Ambient is the color darkened, so an unlit face keeps its hue instead of going
# black.
AMBIENT_SCALE = 0.55

ICON_PUBLIC = "apPieceIcons_scene_models"
ICON_TEX = 32  # texture is square; the ball fills it
ICON_HALF = 1.0  # HUD-space half extent, against the anchors' 2.5 spacing
ICON_SUPERSAMPLE = 4


def sphere_mesh(radius, segments, rings):
    """A UV sphere about the origin, wound so every face points outward under the
    engine's front-face convention. Returns (positions, normals, prims), where a
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

    prims = [
        (
            GX_TRIANGLEFAN,
            [0] + [band_rows[0][j % segments] for j in range(segments + 1)],
        )
    ]
    for b in range(len(band_rows) - 1):
        upper, lower = band_rows[b], band_rows[b + 1]
        strip = []
        for j in range(segments + 1):
            jj = j % segments
            strip.extend((upper[jj], lower[jj]))
        prims.append((GX_TRIANGLESTRIP, strip))
    prims.append(
        (
            GX_TRIANGLEFAN,
            [south]
            + [band_rows[-1][(segments - j) % segments] for j in range(segments + 1)],
        )
    )
    return positions, normals, prims


def outward_facing(positions, normals, prims):
    """Count the triangles wound the way the engine calls front-facing, and the
    triangles wound the other way. A mesh the cull flags can be trusted on has
    all of its faces in the first bucket."""
    front = back = 0
    for opcode, indices in prims:
        tris = []
        if opcode == GX_TRIANGLESTRIP:
            for i in range(len(indices) - 2):
                tri = indices[i : i + 3]
                tris.append(tri if i % 2 == 0 else [tri[1], tri[0], tri[2]])
        elif opcode == GX_TRIANGLEFAN:
            for i in range(1, len(indices) - 1):
                tris.append([indices[0], indices[i], indices[i + 1]])
        for a, b, c in tris:
            pa, pb, pc = positions[a], positions[b], positions[c]
            e1 = [pb[i] - pa[i] for i in range(3)]
            e2 = [pc[i] - pa[i] for i in range(3)]
            g = (
                e1[1] * e2[2] - e1[2] * e2[1],
                e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0],
            )
            n = [sum(normals[v][i] for v in (a, b, c)) for i in range(3)]
            if sum(g[i] * n[i] for i in range(3)) < 0.0:
                front += 1
            else:
                back += 1
    return front, back


def pulse_keys():
    """Alternating full-size and PULSE_MIN keys across the loop, as the spline
    with zero tangents the vanilla piece animation uses."""
    steps = PULSE_END_FRAME // PULSE_HALF_PERIOD
    return [
        Key(
            frame=i * PULSE_HALF_PERIOD,
            op=OP_SPL0,
            value=1.0 if i % 2 == 0 else PULSE_MIN,
        )
        for i in range(steps + 1)
    ]


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
    data[name_off : name_off + len(name_bytes)] = name_bytes
    reserve(-len(data) & 3)  # 4-align what follows

    effect = reserve(SZ_EFFECT_INFO)
    root = reserve(SZ_JOBJ)
    child = reserve(SZ_JOBJ)
    dobj = reserve(SZ_DOBJ)
    mobj = reserve(SZ_MOBJ)
    mat = reserve(SZ_MAT)
    pobj = reserve(SZ_POBJ)
    vtx = reserve(3 * SZ_VTX_ENTRY)
    anim_root = reserve(SZ_ANIMJOINT)
    anim_child = reserve(SZ_ANIMJOINT)
    aobj = reserve(SZ_AOBJDESC)
    fobj = reserve(3 * SZ_FOBJDESC)

    pos_off = blob(b"".join(struct.pack(">3f", *p) for p in positions))
    nrm_off = blob(b"".join(struct.pack(">3f", *n) for n in normals))
    dl = display_list(prims)
    dl_off = blob(dl)
    keys = encode(pulse_keys(), FMT_FLOAT, FMT_FLOAT)
    keys_off = blob(keys)

    # CustomItemDesc: clone a legendary piece, override model, effect record and
    # joint animation.
    w32(desc + 0x00, CUSTOM_ITEM_MAGIC)
    struct.pack_into(">HH", data, desc + 0x04, CUSTOM_ITEM_DESC_VERSION, 0)
    ptr(desc + 0x08, name_off)
    struct.pack_into(">i", data, desc + 0x0C, ITKIND_HYDRA1)  # base_kind
    w32(desc + 0x10, CUSTOM_ITEM_FLAG_NO_MAT_ANIM)  # flags
    ptr(desc + 0x14, root)  # model
    ptr(desc + 0x18, effect)  # effect_info
    struct.pack_into(">3H", data, desc + 0x1C, 0, 0, 0)  # weight_box
    struct.pack_into(">H", data, desc + 0x22, 0)  # weight_free
    struct.pack_into(">6H", data, desc + 0x24, 0, 0, 0, 0, 0, 0)  # weight_event
    w32(desc + 0x30, MODEL_FLAG_SIMPLE)
    wf32(desc + 0x34, SPHERE_SCALE)  # scale
    ptr(desc + 0x38, anim_root)  # joint_anim

    # PatchEffectInfo: no entries, so pickup grants nothing and dispatches nowhere.
    w32(effect + 0x00, 0)  # entries
    w32(effect + 0x04, 0)  # count
    w32(effect + 0x08, ITGROUP_GOOD)

    w32(root + 0x04, JOBJ_ROOT_OPA | JOBJ_CLASSICAL_SCALING)
    ptr(root + 0x08, child)
    wf32(root + 0x20, 1.0)
    wf32(root + 0x24, 1.0)
    wf32(root + 0x28, 1.0)

    w32(child + 0x04, JOBJ_OPA | JOBJ_LIGHTING | JOBJ_CLASSICAL_SCALING)
    ptr(child + 0x10, dobj)
    wf32(child + 0x20, 1.0)
    wf32(child + 0x24, 1.0)
    wf32(child + 0x28, 1.0)

    ptr(dobj + 0x08, mobj)
    ptr(dobj + 0x0C, pobj)

    w32(mobj + 0x04, MOBJ_RENDER_MODE)
    ptr(mobj + 0x0C, mat)

    w32(mat + 0x00, rgba(color, AMBIENT_SCALE))  # ambient
    w32(mat + 0x04, rgba(color))  # diffuse
    w32(mat + 0x08, rgba(color))  # specular, unused without the bit
    wf32(mat + 0x0C, 1.0)  # alpha
    wf32(mat + 0x10, 50.0)  # shininess

    ptr(pobj + 0x08, vtx)
    # Wound clockwise, so culling the back faces drops the inside of the ball.
    struct.pack_into(">HH", data, pobj + 0x0C, POBJ_CULLBACK, len(dl) // 32)
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

    # The animation tree mirrors the model's joints: nothing on the root, the
    # breath on the joint that carries the geometry. Bit 0 of the trailing field
    # is the joint's classical-scaling flag, which the model already sets.
    ptr(anim_root + 0x00, anim_child)
    w32(anim_root + 0x10, 1)
    ptr(anim_child + 0x08, aobj)
    w32(anim_child + 0x10, 1)

    wf32(aobj + 0x04, float(PULSE_END_FRAME))
    ptr(aobj + 0x08, fobj)

    for i, track in enumerate(JOINT_TRACK[t] for t in ("SCAX", "SCAY", "SCAZ")):
        f = fobj + i * SZ_FOBJDESC
        if i + 1 < 3:
            ptr(f + 0x00, f + SZ_FOBJDESC)
        w32(f + 0x04, len(keys))
        data[f + 0x0C] = track
        data[f + 0x0D] = 0  # value_flag 0: keys are raw floats
        data[f + 0x0E] = 0
        ptr(f + 0x10, keys_off)

    return build_archive(data, relocs, [(PUBLIC, desc)])


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
            spec = 255.0 * 0.6 * (lam**16)
            px[x, y] = tuple(min(255, int(v * shade + spec)) for v in color) + (255,)
    return im.resize((ICON_TEX, ICON_TEX), Image.LANCZOS)


def build_icon_archive(colors):
    """One JOBJSet per color under a single NULL-terminated scene_models public."""
    blob = Blob()

    n = len(colors)
    public_arr = blob.append(b"\0" * 4 * (n + 1))
    icons = []
    for _ in colors:
        icon = dict(
            jobjset=blob.append(b"\0" * SZ_JOBJSET),
            root=blob.append(b"\0" * SZ_JOBJ),
            child=blob.append(b"\0" * SZ_JOBJ),
        )
        icon["quad"] = reserve_quad(blob)
        icons.append(icon)

    h = ICON_HALF
    pos_off = blob.append(quad_positions(-h, -h, h, h), 32)
    uv_off = blob.append(QUAD_UVS, 32)
    dl_off = blob.append(QUAD_DL, 32)
    for icon, color in zip(icons, colors):
        icon["tex_off"] = blob.append(encode_rgb5a3(icon_image(color)), 32)

    for i, icon in enumerate(icons):
        blob.ptr(public_arr + i * 4, icon["jobjset"])
        blob.ptr(icon["jobjset"] + 0x00, icon["root"])
        write_jobj(
            blob,
            icon["root"],
            JOBJ_ROOT_XLU | JOBJ_CLASSICAL_SCALING,
            child=icon["child"],
        )
        write_jobj(
            blob,
            icon["child"],
            JOBJ_XLU | JOBJ_CLASSICAL_SCALING,
            dobj=icon["quad"].dobj,
        )
        write_quad(
            blob,
            icon["quad"],
            pos_off,
            uv_off,
            dl_off,
            icon["tex_off"],
            ICON_TEX,
            ICON_TEX,
            GX_TF_RGB5A3,
        )

    return build_archive(blob.data, blob.relocs, [(ICON_PUBLIC, public_arr)])


def main():
    positions, normals, prims = sphere_mesh(
        SPHERE_RADIUS, SPHERE_SEGMENTS, SPHERE_RINGS
    )
    tris = sum(len(v) - 2 for _, v in prims)
    print(
        f"sphere r={SPHERE_RADIUS} scale={SPHERE_SCALE} {SPHERE_SEGMENTS}x{SPHERE_RINGS}: "
        f"{len(positions)} verts, {tris} triangles, {len(prims)} primitives"
    )
    if len(positions) > 255:
        raise SystemExit("index8 vertex arrays hold 255 entries; lower the resolution")

    # Backface culling is wrong if any face is wound the other way.
    front, back = outward_facing(positions, normals, prims)
    if back:
        raise SystemExit(f"{back} of {front + back} triangles face inward")
    print(f"  back-culled, breath 1.0 <-> {PULSE_MIN} every {PULSE_HALF_PERIOD} frames")

    os.makedirs(OUT_DIR, exist_ok=True)
    for color_name, color in PIECES:
        name = f"AP Sphere {color_name}"
        out = os.path.join(OUT_DIR, f"ApSphere{color_name}.dat")
        archive = build_piece(name, color, positions, normals, prims)
        with open(out, "wb") as f:
            f.write(archive)
        print(
            f"  {name:22s} #{color[0]:02X}{color[1]:02X}{color[2]:02X}  "
            f"{os.path.relpath(out, ROOT)} ({len(archive)} bytes)"
        )

    icons = build_icon_archive([color for _, color in PIECES])
    with open(ICON_DAT, "wb") as f:
        f.write(icons)
    print(
        f"  {len(PIECES)} HUD icons {ICON_TEX}x{ICON_TEX} RGB5A3  "
        f"{os.path.relpath(ICON_DAT, ROOT)} ({len(icons) / 1024:.1f} KB, "
        f"public '{ICON_PUBLIC}')"
    )


if __name__ == "__main__":
    main()
