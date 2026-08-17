r"""Archipelago Star machine archive builder.

Rebuilds `VcStarSlick.dat` into the Archipelago Star: six pods on an even ring
instead of three, one per Archipelago logo color, over a repainted platform.
The colors and their ring order come from make_ap_star_pieces' own piece list,
so each pod matches the assembly sphere that stands for it.

Every donor animation survives, because nothing the animations touch is
rebuilt - the three extra pods are new joints hung off the same ring pivot,
drawing the donor's own POBJs through copied DObj/MObj/TObj records.

The Slick Star's 14-joint tree is:

    0-5  donor chain      6  body (3 LOD DObjs)     7  seat hub
    8    ring pivot, the only joint the Moving FigaTree animates
    9-11 pods, children of 8, 4 DObjs each (3 LODs + an XLU glow sprite)
    12   boost particle joint            13  rider seat joint

The three new pods become joints 12-14, which pushes the particle joint to 15
and the seat to 16. Four things outside the JObj tree are keyed to the old
numbering and are repatched here: `ModelData.BoneCount`, the three main LOD
tables (flat DObj indices in JObj preorder), the Moving FigaTree's per-node
track-count table, and the three MatAnimJoint trees, which are walked in
lockstep with the JObj tree and so must grow the same three nodes.

A pod's color is animated, not static: its material diffuse is driven by
DIFFUSE_R/G/B tracks in each of the Moving, Charge and Stop MatAnims (black
under the body texture, magenta on the glow sprite, ramping warm while
charging). Recoloring therefore rewrites those keyframes as well as the
material and the pod's own copy of the body texture, keeping each key's
intensity and how white it was and swapping only the hue.

The platform disc goes the other way. The donor paints it from textures alone,
so this repoints its stages to leave the material color intact and shade it with
a gray sphere map, and hands the pod palette to the descriptor - the registry
walks the disc through those six colors on a wall clock, which no MatAnim in the
archive could do.

Usage:
    uv run python scripts/hsd/make_ap_star.py \
        iso/files/VcStarSlick.dat \
        mods/custom_machines/assets/machines/VcStarAp.dat \
        vcDataStarAp --name "Archipelago Star" \
        --description "A gift from another world.\nSix worlds, one ride."
"""

import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, build_archive, u16, u32
from hsd.clone_machine import append_descriptor
from hsd.fobj import (FMT_FLOAT, FMT_S8, FMT_S16, FMT_U8, FMT_U16, OP_LIN,
                      decode, encode)
from hsd.gx import GX_TF_CMPR, gray_cmpr, image_size, solid_cmpr, tint_cmpr
from hsd.make_ap_star_pieces import PIECES

VCDATA_ATTRIBUTES = 0x00
VCDATA_MODELDATA = 0x04
VCDATA_ANIMBANK = 0x18

MODELDATA_MAIN_ROOT = 0x00
MODELDATA_BONE_COUNT = 0x08
MODELDATA_MAIN_LOD = (0x10, 0x18, 0x20)   # high, mid, low

ANIMBANK_MATANIM = (0x04, 0x24, 0x2C)     # moving, charge, stop
ANIMBANK_PARTICLE_BONES = (0x4C, 0x50, 0x54)

JOBJ_SIZE = 0x40
DOBJ_SIZE = 0x10
MOBJ_SIZE = 0x18
TOBJ_SIZE = 0x5C
MATERIAL_SIZE = 0x14
IMAGE_SIZE = 0x18
MATANIMJOINT_SIZE = 0x0C
MATANIM_SIZE = 0x10
AOBJ_SIZE = 0x10
FOBJDESC_SIZE = 0x14

MAT_DIFFUSE_R, MAT_DIFFUSE_G, MAT_DIFFUSE_B = 4, 5, 6
FMT_MAX = {FMT_S16: 0x7FFF, FMT_U16: 0xFFFF, FMT_S8: 0x7F, FMT_U8: 0xFF}

# The joint carrying the platform disc, and the TObj flag fields that decide how
# far a texture stage gets to interfere with the material color.
PLATFORM_JOINT = 6
COLORMAP_MASK, COLORMAP_SHIFT = 0x000F0000, 16
ALPHAMAP_MASK, ALPHAMAP_SHIFT = 0x00F00000, 20
COLORMAP_MODULATE, COLORMAP_PASS = 4, 6
ALPHAMAP_PASS = 5

# Ring slot 0 sits on +Z like the donor's third pod, which carries ROTY -3pi/2.
# +Z is the machine's front, so slots run clockwise seen from above, matching
# the logo's own order.
RING_PHASE = -1.5 * math.pi


def ring_pose(slot, count, radius, height):
    phi = 2.0 * math.pi * slot / count
    ry = (phi + RING_PHASE) % (2.0 * math.pi) - 2.0 * math.pi
    return (radius * math.sin(phi), height, radius * math.cos(phi)), ry


def normalized(color):
    """An 8-bit RGB triple as floats scaled so its brightest channel is 1.0.
    Intensity comes from the donor keyframe, so the hue is all that is kept."""
    peak = max(max(color), 1)
    return tuple(c / peak for c in color)


def map_color(src, tint):
    """Recolor one animated RGB triple. Black stays black, a fully saturated
    source becomes the tint, and white stays white, so a charge ramp that ends
    on a white flash still ends on a white flash."""
    intensity = max(src)
    if intensity <= 0.0:
        return (0.0, 0.0, 0.0)
    white = min(src) / intensity
    return tuple(intensity * (t + (1.0 - t) * white) for t in tint)


def fit_flag(values, flag):
    """Lower a value flag's fixed-point exponent until every value fits its
    format. The donor's diffuse tracks are quantized just tight enough for
    their own peaks, and a brighter tint would otherwise overflow the byte."""
    fmt = flag & 0xE0
    if fmt == FMT_FLOAT:
        return flag
    limit = FMT_MAX[fmt]
    shift = flag & 0x1F
    peak = max((abs(v) for v in values), default=0.0)
    while shift > 0 and round(peak * (1 << shift)) > limit:
        shift -= 1
    return (flag & 0xE0) | shift


def track_value(keys, frame):
    """Evaluate a CON/LIN keyframe track. The donor's material tracks use no
    other interpolation op."""
    if not keys:
        return 0.0
    if frame <= keys[0].frame:
        return keys[0].value
    for a, b in zip(keys, keys[1:]):
        if a.frame <= frame <= b.frame:
            if a.op == OP_LIN and b.frame > a.frame:
                t = (frame - a.frame) / (b.frame - a.frame)
                return a.value + (b.value - a.value) * t
            return a.value
    return keys[-1].value


class Builder:
    """A mutable copy of an archive's data section with a bump allocator on
    the end, tracking the relocation set as pointers are written."""

    def __init__(self, arc):
        self.arc = arc
        self.data = bytearray(arc.data)
        self.relocs = set(arc.relocs)

    def ptr(self, off):
        return u32(self.data, off) if off in self.relocs else 0

    def set_ptr(self, off, target):
        struct.pack_into(">I", self.data, off, target)
        if target:
            self.relocs.add(off)
        else:
            self.relocs.discard(off)

    def alloc(self, size, align=4):
        self.data.extend(b"\0" * (-len(self.data) % align))
        off = len(self.data)
        self.data.extend(b"\0" * size)
        return off

    def blob(self, payload, align=4):
        off = self.alloc(len(payload), align)
        self.data[off:off + len(payload)] = payload
        return off

    def copy(self, src, size, align=4):
        """Duplicate `size` bytes, carrying every relocation inside them. The
        copied pointers still name their originals, which is what makes the
        new pods share the donor's geometry."""
        off = self.alloc(size, align)
        self.data[off:off + size] = self.data[src:src + size]
        for r in range(src, src + size, 4):
            if r in self.relocs:
                self.relocs.add(off + r - src)
        return off


def walk_joints(b, root):
    """(offset, parent index) per joint, in the preorder the LOD tables and
    the animation trees index by."""
    out = []

    def rec(off, parent):
        i = len(out)
        out.append((off, parent))
        child, nxt = b.ptr(off + 0x08), b.ptr(off + 0x0C)
        if child:
            rec(child, i)
        if nxt:
            rec(nxt, parent)

    rec(root, -1)
    return out


def walk_matanim(b, root):
    """The same preorder over an HSD_MatAnimJoint tree."""
    out = []

    def rec(off, parent):
        i = len(out)
        out.append((off, parent))
        child, nxt = b.ptr(off + 0x00), b.ptr(off + 0x04)
        if child:
            rec(child, i)
        if nxt:
            rec(nxt, parent)

    rec(root, -1)
    return out


def chain(b, head, next_off):
    out = []
    cur = head
    while cur:
        out.append(cur)
        cur = b.ptr(cur + next_off)
    return out


def dobjs_of(b, jobj):
    head = b.ptr(jobj + 0x10)
    return chain(b, head, 0x04) if head else []


def flat_dobjs(b, joints):
    out = []
    for off, _ in joints:
        out.extend(dobjs_of(b, off))
    return out


def set_vec3(b, off, values):
    struct.pack_into(">3f", b.data, off, *values)


def set_diffuse(b, material, color):
    b.data[material + 0x04:material + 0x07] = bytes(
        min(255, int(round(c * 255))) for c in color)


class Donor:
    """The Slick Star layout this script rewrites, validated on load."""

    def __init__(self, b):
        self.b = b
        if len(b.arc.publics) != 1:
            raise SystemExit(f"expected one public, found {list(b.arc.publics)}")
        self.public = next(iter(b.arc.publics))
        self.vcdata = b.arc.publics[self.public]
        self.attributes = b.ptr(self.vcdata + VCDATA_ATTRIBUTES)
        self.model_data = b.ptr(self.vcdata + VCDATA_MODELDATA)
        self.anim_bank = b.ptr(self.vcdata + VCDATA_ANIMBANK)
        self.root = b.ptr(self.model_data + MODELDATA_MAIN_ROOT)
        self.joints = walk_joints(b, self.root)
        self.pods = [i for i, (_, parent) in enumerate(self.joints) if parent == 8]
        self.dobjs = flat_dobjs(b, self.joints)

        bone_count = b.data[self.model_data + MODELDATA_BONE_COUNT]
        if (len(self.joints), bone_count) != (14, 14) or self.pods != [9, 10, 11]:
            raise SystemExit(
                f"donor is not a 14-joint Slick Star (joints={len(self.joints)}, "
                f"BoneCount={bone_count}, ring children={self.pods})")
        if len(self.dobjs) != 15 or any(
                len(dobjs_of(b, self.joints[i][0])) != 4 for i in self.pods):
            raise SystemExit(f"donor pods are not 4 DObjs each ({len(self.dobjs)} total)")

        self.figatree = b.ptr(self.anim_bank + 0x00)
        self.matanim_trees = [b.ptr(self.anim_bank + o) for o in ANIMBANK_MATANIM]
        if not self.figatree or not all(self.matanim_trees):
            raise SystemExit("donor is missing its Moving/Charge/Stop animations")
        for tree in self.matanim_trees:
            nodes = walk_matanim(b, tree)
            if len(nodes) != len(self.joints):
                raise SystemExit(
                    f"MatAnimJoint tree @{tree:#x} has {len(nodes)} nodes, "
                    f"expected {len(self.joints)}")


def retint_aobj(b, aobj, tint):
    """Rewrite one material AObj's DIFFUSE_R/G/B keyframes onto `tint`. Each
    track keeps its frames, ops and quantization format; only the values and,
    where a brighter tint needs the headroom, the fixed-point exponent move."""
    tracks = {}
    for fd in chain(b, b.ptr(aobj + 0x08), 0x00):
        kind = b.data[fd + 0x0C]
        if kind not in (MAT_DIFFUSE_R, MAT_DIFFUSE_G, MAT_DIFFUSE_B):
            continue
        length = u32(b.data, fd + 0x04)
        buf_off = b.ptr(fd + 0x10)
        value_flag, tan_flag = b.data[fd + 0x0D], b.data[fd + 0x0E]
        keys = decode(bytes(b.data[buf_off:buf_off + length]), value_flag, tan_flag)
        tracks[kind] = (fd, value_flag, tan_flag, keys)
    if len(tracks) != 3:
        return 0

    channels = (MAT_DIFFUSE_R, MAT_DIFFUSE_G, MAT_DIFFUSE_B)
    recolored = {}
    for c, kind in enumerate(channels):
        _, _, _, keys = tracks[kind]
        recolored[kind] = [
            map_color(tuple(track_value(tracks[o][3], k.frame) for o in channels),
                      tint)[c]
            for k in keys
        ]

    for kind in channels:
        fd, value_flag, tan_flag, keys = tracks[kind]
        for key, value in zip(keys, recolored[kind]):
            key.value = value
        value_flag = fit_flag(recolored[kind], value_flag)
        buf = encode(keys, value_flag, tan_flag)
        b.data[fd + 0x0D] = value_flag
        struct.pack_into(">I", b.data, fd + 0x04, len(buf))
        b.set_ptr(fd + 0x10, b.blob(buf))
    return 3


def copy_matanim_chain(b, head):
    """Deep-copy a pod's MatAnim chain down to its keyframe buffers, sharing
    the texture animation, so the copy can be recolored on its own."""
    first = None
    prev = None
    for src in chain(b, head, 0x00):
        mat = b.copy(src, MATANIM_SIZE)
        b.set_ptr(mat + 0x00, 0)
        aobj_src = b.ptr(src + 0x04)
        if aobj_src:
            aobj = b.copy(aobj_src, AOBJ_SIZE)
            b.set_ptr(mat + 0x04, aobj)
            prev_fd = None
            for fd_src in chain(b, b.ptr(aobj_src + 0x08), 0x00):
                fd = b.copy(fd_src, FOBJDESC_SIZE)
                b.set_ptr(fd + 0x00, 0)
                if prev_fd is None:
                    b.set_ptr(aobj + 0x08, fd)
                else:
                    b.set_ptr(prev_fd + 0x00, fd)
                prev_fd = fd
        if prev is None:
            first = mat
        else:
            b.set_ptr(prev + 0x00, mat)
        prev = mat
    return first


def copy_pod_render(b, donor_pod, tinted_image):
    """Copy a pod's DObj chain, giving each DObj its own MObj, material and
    TObj while sharing the donor's POBJs. The three LOD DObjs get their own
    ImageDesc pointed at `tinted_image`; the glow sprite shares the donor's,
    since its texture supplies alpha only."""
    first = None
    prev = None
    for i, dobj_src in enumerate(dobjs_of(b, donor_pod)):
        dobj = b.copy(dobj_src, DOBJ_SIZE)
        b.set_ptr(dobj + 0x04, 0)

        mobj = b.copy(b.ptr(dobj_src + 0x08), MOBJ_SIZE)
        b.set_ptr(dobj + 0x08, mobj)
        b.set_ptr(mobj + 0x0C, b.copy(b.ptr(mobj + 0x0C), MATERIAL_SIZE))

        tobj = b.copy(b.ptr(mobj + 0x08), TOBJ_SIZE)
        b.set_ptr(mobj + 0x08, tobj)
        b.set_ptr(tobj + 0x04, 0)
        if i < 3:
            image = b.copy(b.ptr(tobj + 0x4C), IMAGE_SIZE)
            b.set_ptr(tobj + 0x4C, image)
            b.set_ptr(image + 0x00, tinted_image)

        if prev is None:
            first = dobj
        else:
            b.set_ptr(prev + 0x04, dobj)
        prev = dobj
    return first


def pod_texture(b, donor_pod, color):
    """A tinted private copy of the pod body texture, which all three donor
    pods otherwise share."""
    image = b.ptr(b.ptr(b.ptr(dobjs_of(b, donor_pod)[0] + 0x08) + 0x08) + 0x4C)
    blob = b.ptr(image + 0x00)
    w, h = u16(b.data, image + 0x04), u16(b.data, image + 0x06)
    fmt = u32(b.data, image + 0x08)
    if fmt != GX_TF_CMPR:
        raise SystemExit(f"pod texture @{blob:#x} is format {fmt}, expected CMPR")
    n = image_size(w, h, fmt, u32(b.data, image + 0x0C) != 0)
    return b.blob(tint_cmpr(bytes(b.data[blob:blob + n]), color), align=32)


def repoint_pod_texture(b, pod_jobj, tinted_image):
    for dobj in dobjs_of(b, pod_jobj)[:3]:
        image = b.ptr(b.ptr(b.ptr(dobj + 0x08) + 0x08) + 0x4C)
        b.set_ptr(image + 0x00, tinted_image)


def set_texture_map(b, tobj, mask, shift, mode):
    flags = u32(b.data, tobj + 0x40)
    struct.pack_into(">I", b.data, tobj + 0x40, (flags & ~mask) | (mode << shift))


def blob_of(b, image):
    """(offset, byte length) of an ImageDesc's texel blob."""
    fmt = u32(b.data, image + 0x08)
    if fmt != GX_TF_CMPR:
        raise SystemExit(f"platform texture is format {fmt}, expected CMPR")
    return b.ptr(image + 0x00), image_size(
        u16(b.data, image + 0x04), u16(b.data, image + 0x06), fmt,
        u32(b.data, image + 0x0C) != 0)


def paint_platform(b, donor, color, gloss):
    """Move the platform disc's color into its material, where the runtime can
    animate it.

    The donor drives the disc entirely from textures: stage 0 REPLACEs the color
    with a striped 256x256 map and takes its 1-bit alpha as the disc's cutout,
    then stage 1 BLENDs a magenta sphere map over that at full strength, so the
    material color never reaches a pixel. Here stage 0 is reduced to a single
    opaque texel that passes both channels through - which is what retires the
    Slick Star's stripes, and with them the cutout, leaving a solid disc - and
    stage 1 becomes a gray gloss that MODULATEs the material color. The material
    renders with RENDER_CONSTANT and no lighting, so its diffuse is the disc's
    whole color, shaded only by that gloss."""
    dobjs = dobjs_of(b, donor.joints[PLATFORM_JOINT][0])
    flat = solid_cmpr(8, 8, (255, 255, 255))
    base_images, gloss_blob = [], None

    for dobj in dobjs:
        mobj = b.ptr(dobj + 0x08)
        set_diffuse(b, b.ptr(mobj + 0x0C), tuple(c / 255.0 for c in color))
        tobjs = chain(b, b.ptr(mobj + 0x08), 0x04)
        if len(tobjs) != 2:
            raise SystemExit(
                f"platform DObj @{dobj:#x} has {len(tobjs)} textures, expected 2")

        set_texture_map(b, tobjs[0], COLORMAP_MASK, COLORMAP_SHIFT, COLORMAP_PASS)
        set_texture_map(b, tobjs[0], ALPHAMAP_MASK, ALPHAMAP_SHIFT, ALPHAMAP_PASS)
        set_texture_map(b, tobjs[1], COLORMAP_MASK, COLORMAP_SHIFT, COLORMAP_MODULATE)
        base_images.append(b.ptr(tobjs[0] + 0x4C))
        gloss_blob = blob_of(b, b.ptr(tobjs[1] + 0x4C))

    # All three LOD DObjs share both blobs, so each is rewritten once. The flat
    # texel is laid over the head of the stripe map's own 32 KB rather than
    # appended, which retires it without growing the archive.
    stripe, stripe_len = blob_of(b, base_images[0])
    b.data[stripe:stripe + stripe_len] = flat + b"\0" * (stripe_len - len(flat))
    for image in base_images:
        struct.pack_into(">HH", b.data, image + 0x04, 8, 8)
    gloss_off, gloss_len = gloss_blob
    b.data[gloss_off:gloss_off + gloss_len] = gray_cmpr(
        bytes(b.data[gloss_off:gloss_off + gloss_len]), gloss)
    print(f"  platform #{color[0]:02X}{color[1]:02X}{color[2]:02X} over a "
          f"{gloss:.2f}-floor gloss map @{gloss_off:#x}, "
          f"{stripe_len // 1024} KB of Slick stripes retired @{stripe:#x}")


def rebuild_lod_tables(b, donor, pod_count):
    """Rewrite the three main LOD tables over the new flat DObj indices. Each
    holds its LOD's opaque DObjs and then every pod's always-drawn glow."""
    joints = walk_joints(b, donor.root)
    dobjs = flat_dobjs(b, joints)
    index = {off: i for i, off in enumerate(dobjs)}
    body = dobjs_of(b, joints[6][0])
    pods = [dobjs_of(b, joints[i][0]) for i, (_, parent) in enumerate(joints)
            if parent == 8]
    if len(pods) != pod_count:
        raise SystemExit(f"rebuilt tree has {len(pods)} pods, expected {pod_count}")

    for lod, slot in enumerate(MODELDATA_MAIN_LOD):
        entries = b.ptr(b.ptr(donor.model_data + slot) + 0x04)
        table = bytes([index[body[lod]]]
                      + [index[p[lod]] for p in pods]
                      + [index[p[3]] for p in pods])
        struct.pack_into(">I", b.data, entries + 0x00, len(table))
        b.set_ptr(entries + 0x04, b.blob(table))
        print(f"  LOD table {('high', 'mid', 'low')[lod]}: {list(table)}")
    return len(dobjs)


def rebuild_figatree(b, donor, added):
    """Extend the Moving FigaTree's per-node track-count table. The pods sit
    under the animated ring pivot and carry no tracks of their own, so the new
    nodes are that many more zeros, spliced in after the last donor pod."""
    counts = []
    table = b.ptr(donor.figatree + 0x0C)
    while b.data[table + len(counts)] != 0xFF:
        counts.append(b.data[table + len(counts)])
    if len(counts) != len(donor.joints):
        raise SystemExit(f"FigaTree has {len(counts)} nodes, expected {len(donor.joints)}")
    at = donor.pods[-1] + 1
    counts = counts[:at] + [0] * added + counts[at:]
    b.set_ptr(donor.figatree + 0x0C, b.blob(bytes(counts) + b"\xff"))
    print(f"  FigaTree node table: {counts}")


def build(src_path, out_path, new_public, args):
    arc = Archive(src_path)
    b = Builder(arc)
    donor = Donor(b)
    pod_count = len(PIECES)
    print(f"  donor '{donor.public}' @ {donor.vcdata:#x}, {arc.data_size / 1024:.1f} KB "
          f"data, {arc.nb_reloc} relocs, {len(donor.joints)} joints, "
          f"{len(donor.dobjs)} DObjs")

    donor_pod = donor.joints[donor.pods[0]][0]
    tinted = [pod_texture(b, donor_pod, color) for _, color in PIECES]
    ring = [ring_pose(i, pod_count, args.ring_radius, args.ring_height)
            for i in range(pod_count)]

    pod_joints = [donor.joints[i][0] for i in donor.pods]
    for slot in range(len(donor.pods), pod_count):
        jobj = b.copy(donor_pod, JOBJ_SIZE)
        b.set_ptr(jobj + 0x08, 0)
        b.set_ptr(jobj + 0x0C, 0)
        b.set_ptr(jobj + 0x10, copy_pod_render(b, donor_pod, tinted[slot]))
        b.set_ptr(pod_joints[-1] + 0x0C, jobj)
        pod_joints.append(jobj)

    for slot, jobj in enumerate(pod_joints):
        name, color = PIECES[slot]
        (translation, ry) = ring[slot]
        set_vec3(b, jobj + 0x14, (0.0, ry, 0.0))
        set_vec3(b, jobj + 0x20, (args.pod_scale,) * 3)
        set_vec3(b, jobj + 0x2C, translation)
        if slot < len(donor.pods):
            repoint_pod_texture(b, jobj, tinted[slot])
        glow_material = b.ptr(b.ptr(dobjs_of(b, jobj)[3] + 0x08) + 0x0C)
        set_diffuse(b, glow_material, map_color((1.0, 0.0, 1.0), normalized(color)))
        print(f"  pod {slot} {name:6s} #{color[0]:02X}{color[1]:02X}{color[2]:02X} "
              f"@{jobj:#07x} "
              f"t=({translation[0]:.3f}, {translation[1]:.3f}, {translation[2]:.3f}) "
              f"ry={ry:.3f} scale={args.pod_scale}")

    retinted = 0
    for tree in donor.matanim_trees:
        nodes = walk_matanim(b, tree)
        donor_chain = b.ptr(nodes[donor.pods[0]][0] + 0x08)
        last = nodes[donor.pods[-1]][0]
        for _ in range(pod_count - len(donor.pods)):
            node = b.copy(nodes[donor.pods[0]][0], MATANIMJOINT_SIZE)
            b.set_ptr(node + 0x00, 0)
            b.set_ptr(node + 0x04, 0)
            b.set_ptr(node + 0x08, copy_matanim_chain(b, donor_chain))
            b.set_ptr(last + 0x04, node)
            last = node
        pod_nodes = walk_matanim(b, tree)[donor.pods[0]:][:pod_count]
        for slot, (node, _) in enumerate(pod_nodes):
            tint = normalized(PIECES[slot][1])
            for matanim in chain(b, b.ptr(node + 0x08), 0x00):
                aobj = b.ptr(matanim + 0x04)
                if aobj:
                    retinted += retint_aobj(b, aobj, tint)
    print(f"  retinted {retinted} diffuse tracks across "
          f"{len(donor.matanim_trees)} animations")

    added = pod_count - len(donor.pods)
    paint_platform(b, donor, args.platform_color, args.platform_gloss)
    joints = walk_joints(b, donor.root)
    total_dobjs = rebuild_lod_tables(b, donor, pod_count)
    rebuild_figatree(b, donor, added)

    # The new pods precede the particle and seat joints in preorder, so every
    # joint index stored outside the tree past the last donor pod shifts.
    def shifted(i):
        return i + added if i > donor.pods[-1] else i

    b.data[donor.model_data + MODELDATA_BONE_COUNT] = len(joints)
    seat = shifted(struct.unpack_from(">i", b.data, donor.attributes + 0x00)[0])
    struct.pack_into(">i", b.data, donor.attributes + 0x00, seat)
    bones = []
    for slot in ANIMBANK_PARTICLE_BONES:
        bone = struct.unpack_from(">i", b.data, donor.anim_bank + slot)[0]
        bone = shifted(bone) if bone >= 0 else bone
        struct.pack_into(">i", b.data, donor.anim_bank + slot, bone)
        bones.append(bone)
    print(f"  BoneCount {len(joints)}, {total_dobjs} DObjs, "
          f"seat bone {seat}, particle bones {bones}")

    # The platform's cycle is wall-clock, which no MatAnim can be: the moving
    # animation's rate scales with velocity and the charge animation's frame is
    # the charge gauge. So the colors ship as descriptor data and the registry
    # walks them onto the joint's materials each frame.
    palette = b.blob(b"".join(struct.pack(">I", (r << 16) | (g << 8) | blue)
                              for _, (r, g, blue) in PIECES))
    relocs = sorted(b.relocs)
    desc_off = append_descriptor(b.data, relocs, new_public, args,
                                 palette=(PLATFORM_JOINT, args.palette_period,
                                          pod_count, palette))
    print(f"  descriptor '{args.name}' @ {desc_off:#x} "
          f"(clone kind {args.clone_kind}, spawn weight {args.spawn_weight}, "
          f"joint {PLATFORM_JOINT} cycles {pod_count} colors every "
          f"{args.palette_period:g}s)")

    publics = [(new_public, donor.vcdata), ("customMachine", desc_off)]
    externs = [(name, off) for off, name in arc.externs]
    out = build_archive(b.data, relocs, publics, arc.version, externs)
    with open(out_path, "wb") as f:
        f.write(out)
    print(f"  wrote {out_path} ({len(out) / 1024:.1f} KB, +{(len(out) - arc.file_size) / 1024:.1f} KB)")


def color_arg(text):
    text = text.lstrip("#")
    if len(text) != 6:
        raise argparse.ArgumentTypeError(f"expected RRGGBB, got '{text}'")
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def main(argv):
    p = argparse.ArgumentParser(description="Build the Archipelago Star machine archive")
    p.add_argument("src", help="donor archive, iso/files/VcStarSlick.dat")
    p.add_argument("out", help="output archive path")
    p.add_argument("public", help="public symbol name for the machine")
    p.add_argument("--name", default=None, help="display name (default: the public symbol)")
    p.add_argument("--description", default="",
                   help="select-screen blurb under the name; two lines of about 24 "
                        r"characters, split with \n")
    p.add_argument("--ring-radius", type=float, default=2.20,
                   help="pod ring radius; the donor's three pods sit at 2.45")
    p.add_argument("--ring-height", type=float, default=1.05,
                   help="pod ring height above the body root; the donor's is 0.875")
    p.add_argument("--pod-scale", type=float, default=1.25,
                   help="uniform scale on each pod joint")
    p.add_argument("--platform-color", type=color_arg, default=color_arg("BFF5BF"),
                   help="RRGGBB the platform disc rests at, and what it shows wherever "
                        "nothing is cycling its palette")
    p.add_argument("--platform-gloss", type=float, default=0.75,
                   help="the platform's dimmest shade as a fraction of its color; the "
                        "sphere map's highlight always reaches the color in full, so 1 "
                        "is a flat disc and 0 a hard falloff")
    p.add_argument("--palette-period", type=float, default=12.0,
                   help="seconds for the platform to walk the whole pod palette")
    p.add_argument("--no-character", action="store_true",
                   help="register the machine without a CharacterKind or select-grid cell")
    p.add_argument("--rider-kind", type=int, default=0,
                   help="RiderKind for the appended CharacterDesc row (0 = Kirby)")
    p.add_argument("--clone-kind", type=int, default=6,
                   help="star MachineKind whose per-kind engine rows this machine "
                        "inherits (default 6, Slick Star)")
    p.add_argument("--spawn-weight", type=float, default=1.0,
                   help="City Trial spawn weight; 0 never spawns loose on the field")
    args = p.parse_args(argv[1:])

    if args.name is None:
        args.name = args.public
    args.description = args.description.replace("\\n", "\n")

    print(f"Building {args.out} from {args.src}:")
    build(args.src, args.out, args.public, args)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
