r"""Archipelago Star assembly-cinematic assets.

Writes the two archives the vanilla legendary assembly cinematic needs to run
with the Archipelago Star in place of Hydra or Dragoon:

    mods/ap_star/assets/ApStarParts.dat   apStarParts
    mods/ap_star/assets/ApStarGlow.dat    apStarGlow, apStarCam

The cinematic reads a `vsData`, three pointers: a glow-model triple
`{JOBJDesc*, FigaTree*, MatAnimJoint*}`, a parts-model triple of the same
shape, and a pointer to a word holding a camera-animation descriptor. The two
halves come from different donors - the parts from the star's own machine
archive, the glow and camera from VsHydra.dat - so they are carved separately
and the three-pointer vsData is assembled in mod RAM.

ApStarParts.dat is the star's main model with every joint's DObj chain trimmed
to its high LOD (the cinematic draws every DObj, so all three LODs would
overlap) plus a 150-frame FigaTree that flies the six pods in from 22 units out
and spins the ring up once they land.

ApStarGlow.dat is Hydra's glow model - the ribbon streaks and impact flashes -
carved down to one streak group, copied back out to six, and given a FigaTree
of its own that aims each streak along its pod's path. The camera descriptor
block rides along byte for byte, with the path joint's scale tightened for a
machine whose footprint is smaller than Hydra's.

Usage:
    uv run python scripts/hsd/make_ap_star_assembly.py
"""

import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd import fobj
from hsd.archive import Archive, build_archive, f32, u16, u32
from hsd.walker import Walker, carve_ranges

MACHINE_DAT = "mods/ap_star/assets/machines/VcStarAp.dat"
HYDRA_DAT = "iso/files/VsHydra.dat"
OUT_DIR = "mods/ap_star/assets"

# Joint indices in the star's main model, in HSD_JObjLoadJoint preorder.
J_BODY = 6
J_RING = 8
J_POD0 = 9
NUM_PODS = 6
NUM_JOINTS = 17

# Offsets into VsHydra.dat. The glow model's first streak group is joints 1-4
# (group root, envelope mesh, anchor bone, head bone); joints 13-17 are the
# shared impact flashes. The camera block is self-contained - every reloc
# inside it targets inside it, and only vsData[2] points in from outside.
HYDRA_GLOW_ROOT = 0x1728
HYDRA_GLOW_MATANIM = 0xA860
HYDRA_G1_ROOT = 0x1768
HYDRA_G1_MESH = 0x17A8
HYDRA_FLASH_ROOT = 0x1B88
HYDRA_MA_G1_ROOT = 0xA86C
HYDRA_MA_FLASH = 0xA8FC
HYDRA_MA_STREAK = 0xA7E0
HYDRA_CAM_LO = 0xA938
HYDRA_CAM_HI = 0xAC08
HYDRA_CAM_DESC = 0xAC00
HYDRA_CAM_PATH_JOINT = 0xAAF8
HYDRA_FIGATREE_TABLE = 0xB944
HYDRA_FIGATREE_TRACKS = 0xB788

# The camera orbits a fixed spiral scaled by its path joint. Hydra's machine
# measures 3.51 x 4.25 across, the star 3.01 x 3.46, so the orbit comes in to
# match; the eye height is left alone, which reads the flat star's ring of pods
# at a steeper angle than Hydra's bulk needs.
CAM_ORBIT_SCALE = 0.85

# How far out along its pod's approach a streak's ribbon tip reaches.
STREAK_REACH = 0.82

END_FRAME = 150.0

# Pod i lands on frame ARRIVE[i]. Hydra's three parts land on 70/80/90 and its
# streaks strobe from 20; six pods divide the same window.
ARRIVE = [70, 74, 78, 82, 86, 90]
STREAK_ON = 20
RING_SPIN_START = 95

POD_START_RADIUS = 22.0
POD_START_HEIGHT = 12.0
POD_SWIRL = math.radians(85.0)

# Fraction of the flight covered at each fraction of the flight time. Mirrors
# the vanilla shape: most of the distance early, a hover, then the last snap.
POD_PROGRESS = [(0.00, 0.00), (0.22, 0.55), (0.45, 0.80),
                (0.70, 0.90), (0.90, 0.955), (1.00, 1.00)]

# Radial overshoot after landing, as (frames past arrival, offset in units).
POD_BOUNCE = [(0, 0.00), (2, -0.30), (5, 0.12), (8, -0.04), (11, 0.00)]
POD_SCALE_POP = [(-1, 1.00), (1, 1.20), (4, 0.90), (7, 1.00)]

TRACK = {"ROTX": 1, "ROTY": 2, "ROTZ": 3, "PATH": 4, "TRAX": 5, "TRAY": 6,
         "TRAZ": 7, "SCAX": 8, "SCAY": 9, "SCAZ": 10, "NODE": 11,
         "BRANCH": 12, "PTCL": 40}


def lerp(a, b, t):
    return a + (b - a) * t


def piecewise(table, x):
    """Linear interpolation over a sorted list of (x, y) pairs."""
    if x <= table[0][0]:
        return table[0][1]
    for (x0, y0), (x1, y1) in zip(table, table[1:]):
        if x <= x1:
            return y0 if x1 == x0 else lerp(y0, y1, (x - x0) / (x1 - x0))
    return table[-1][1]


def rot_zyx(rx, ry, rz):
    """HSD joint rotation: Rz . Ry . Rx, as a row-major 3x3."""
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    return [
        [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
        [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
        [-sy, cy * sx, cy * cx],
    ]


def mat_apply(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


def mat_apply_t(m, v):
    """Transpose-apply, i.e. the inverse of a rotation matrix."""
    return tuple(sum(m[k][i] * v[k] for k in range(3)) for i in range(3))


class Out:
    """An appendable data section plus its reloc source set."""

    def __init__(self, data, relocs):
        self.d = bytearray(data)
        self.relocs = set(relocs)

    def align(self, n=4):
        while len(self.d) % n:
            self.d.append(0)

    def put(self, blob, align=4):
        self.align(align)
        off = len(self.d)
        self.d.extend(blob)
        return off

    def alloc(self, size, align=4):
        return self.put(b"\0" * size, align)

    def w32(self, off, v):
        struct.pack_into(">I", self.d, off, v)

    def wf32(self, off, v):
        struct.pack_into(">f", self.d, off, v)

    def rd32(self, off):
        return u32(self.d, off)

    def ptr(self, off, target):
        self.w32(off, target)
        self.relocs.add(off)

    def clr(self, off):
        self.w32(off, 0)
        self.relocs.discard(off)

    def copy_struct(self, src, size):
        """Copy `size` bytes from `src` in this buffer, carrying its relocs."""
        off = self.put(bytes(self.d[src:src + size]))
        for i in range(0, size, 4):
            if src + i in self.relocs:
                self.relocs.add(off + i)
        return off


class Track:
    """One FigaTree track: which node it drives and its packed keys."""

    def __init__(self, node, kind, keys=None, op=fobj.OP_LIN,
                 raw=None, value_flag=fobj.FMT_FLOAT, tan_flag=fobj.FMT_FLOAT,
                 start_frame=0):
        self.node = node
        self.kind = TRACK[kind] if isinstance(kind, str) else kind
        self.value_flag = value_flag
        self.tan_flag = tan_flag
        self.start_frame = start_frame
        if raw is not None:
            self.buf = raw
        else:
            ks = [fobj.Key(frame=f, value=v, op=op) for f, v in keys]
            self.buf = fobj.encode(ks, value_flag, tan_flag)


def emit_figatree(out, node_count, tracks, end_frame=END_FRAME):
    """Write a FigaTree container and everything it points at. `tracks` is a
    list of Track in any order; they are grouped by node here, since the
    per-node count table is walked in HSD_JObjLoadJoint preorder."""
    by_node = [[] for _ in range(node_count)]
    for t in tracks:
        by_node[t.node].append(t)
    ordered = [t for group in by_node for t in group]

    key_offs = [out.put(t.buf) for t in ordered]

    recs = out.alloc(0x0C * len(ordered))
    for i, t in enumerate(ordered):
        rec = recs + 0x0C * i
        struct.pack_into(">HhBBBB", out.d, rec, len(t.buf), t.start_frame,
                         t.kind, t.value_flag, t.tan_flag, 0)
        out.ptr(rec + 8, key_offs[i])

    table = out.put(bytes(len(g) for g in by_node) + b"\xff")

    tree = out.alloc(0x14)
    out.clr(tree + 0x00)              # name
    out.w32(tree + 0x04, 0)           # type
    out.wf32(tree + 0x08, end_frame)
    out.ptr(tree + 0x0C, table)
    out.ptr(tree + 0x10, recs)
    return tree


class Choreography:
    """Where each pod is on each frame, in the machine model's own space.

    The pods hang off the ring pivot, so their FigaTree tracks are authored in
    that joint's frame; the glow model is placed at the same world transform as
    the parts model, so its streaks need the same points one frame up.
    """

    def __init__(self, arc, root):
        d = arc.data
        joints = joint_preorder(arc, root)
        self.joints = joints

        def tr(j):
            return tuple(f32(d, joints[j] + 0x2C + 4 * k) for k in range(3))

        self.ring_rot_y = f32(d, joints[J_RING] + 0x18)
        self.ring_origin = tuple(
            sum(tr(j)[k] for j in range(J_RING + 1)) for k in range(3))
        self.ring_mtx = rot_zyx(0.0, self.ring_rot_y, 0.0)
        self.rest = [tr(J_POD0 + i) for i in range(NUM_PODS)]
        self.pod_scale = f32(d, joints[J_POD0] + 0x20)

    def theta(self, i):
        x, _, z = self.rest[i]
        return math.atan2(x, z)

    def ring_radius(self, i):
        x, _, z = self.rest[i]
        return math.hypot(x, z)

    def flight(self, i, frame):
        """Pod i's ring-local position on `frame`, before the landing bounce."""
        arrive = ARRIVE[i]
        t = min(1.0, frame / float(arrive))
        u = piecewise(POD_PROGRESS, t)
        ang = self.theta(i) + POD_SWIRL * (1.0 - u) ** 1.4
        rad = lerp(POD_START_RADIUS, self.ring_radius(i), u)
        y = lerp(POD_START_HEIGHT, self.rest[i][1], u)
        return (rad * math.sin(ang), y, rad * math.cos(ang))

    def path(self, i):
        """[(frame, (x, y, z))] for pod i over the whole 150 frames."""
        arrive = ARRIVE[i]
        pts = []
        frame = 0
        while frame < arrive:
            pts.append((frame, self.flight(i, frame)))
            frame += 10 if frame < arrive - 20 else 4
        rx, _, rz = self.rest[i]
        radial = (rx / self.ring_radius(i), 0.0, rz / self.ring_radius(i))
        for df, amount in POD_BOUNCE:
            pos = tuple(self.rest[i][k] + radial[k] * amount for k in range(3))
            pts.append((arrive + df, pos))
        pts.append((int(END_FRAME), self.rest[i]))
        return pts

    def machine_path(self, i):
        """The same path lifted out of the ring pivot into model space."""
        return [(f, tuple(self.ring_origin[k] + mat_apply(self.ring_mtx, p)[k]
                          for k in range(3))) for f, p in self.path(i)]


def joint_preorder(arc, root):
    """Joint descriptor offsets in HSD_JObjLoadJoint order."""
    order = []

    def walk(off):
        order.append(off)
        child = arc.deref(off + 0x08)
        if child:
            walk(child)
        nxt = arc.deref(off + 0x0C)
        if nxt:
            walk(nxt)

    walk(root)
    return order


def group_joints(arc, root, remap):
    """A streak group's four joints - root, envelope mesh, anchor bone, head
    bone - mapped into carved coordinates."""
    mesh = arc.deref(root + 0x08)
    anchor = arc.deref(mesh + 0x08)
    head = arc.deref(anchor + 0x0C)
    return [remap[o] for o in (root, mesh, anchor, head)]


def matanim_group(arc, root, remap):
    """The same four nodes on the material-animation side."""
    mesh = arc.deref(root + 0x00)
    anchor = arc.deref(mesh + 0x00)
    head = arc.deref(anchor + 0x04)
    return [remap[o] for o in (root, mesh, anchor, head)]


def dobj_chain(arc, joint):
    out = []
    o = arc.deref(joint + 0x10)
    while o:
        out.append(o)
        o = arc.deref(o + 0x04)
    return out


def build_parts(root_dir, verbose=True):
    arc = Archive(os.path.join(root_dir, MACHINE_DAT))
    model_data = arc.deref(arc.publics["vcDataStarAp"] + 0x04)
    root = arc.deref(model_data + 0x00)
    joints = joint_preorder(arc, root)
    if len(joints) != NUM_JOINTS:
        raise SystemExit(f"{MACHINE_DAT}: expected {NUM_JOINTS} joints, "
                         f"found {len(joints)}")

    chor = Choreography(arc, root)

    # Keep the high LOD on every drawn joint, plus the pods' XLU glow quad.
    # The cinematic's render callback draws every DObj, so the two lower LODs
    # would z-fight the one that is meant to be seen.
    patched = bytearray(arc.data)
    relocs = list(arc.relocs)
    dropped = set()
    for j, joint in enumerate(joints):
        chain = dobj_chain(arc, joint)
        if len(chain) < 2:
            continue
        keep = [chain[0], chain[-1]] if j >= J_POD0 and len(chain) > 3 else [chain[0]]
        for a, b in zip(keep, keep[1:] + [0]):
            struct.pack_into(">I", patched, a + 0x04, b)
            if b == 0:
                dropped.add(a + 0x04)
    arc.data = bytes(patched)
    arc.relocs = [r for r in relocs if r not in dropped]
    arc.reloc_set = set(arc.relocs)

    visited = Walker(arc).walk(root, "JOBJDesc")

    prefix = bytearray(0x20)
    res = carve_ranges(arc, visited, prefix, base_relocs=(0x00, 0x04))
    out = Out(res.data, res.relocs)

    tracks = []
    spin = [(0, chor.ring_rot_y), (RING_SPIN_START, chor.ring_rot_y),
            (int(END_FRAME), chor.ring_rot_y + 2.0 * math.pi)]
    tracks.append(Track(J_RING, "ROTY", spin))
    for i in range(NUM_PODS):
        path = chor.path(i)
        for axis, comp in (("TRAX", 0), ("TRAY", 1), ("TRAZ", 2)):
            tracks.append(Track(J_POD0 + i, axis, [(f, p[comp]) for f, p in path]))
        pop = [(0, chor.pod_scale)]
        pop += [(ARRIVE[i] + df, chor.pod_scale * m) for df, m in POD_SCALE_POP]
        pop.append((int(END_FRAME), chor.pod_scale))
        for axis in ("SCAX", "SCAY", "SCAZ"):
            tracks.append(Track(J_POD0 + i, axis, pop))

    tree = emit_figatree(out, NUM_JOINTS, tracks)
    out.ptr(0x00, res.remap[root])
    out.ptr(0x04, tree)
    out.clr(0x08)

    blob = build_archive(out.d, sorted(out.relocs),
                         [("apStarParts", 0)], arc.version)
    path = os.path.join(root_dir, OUT_DIR, "ApStarParts.dat")
    with open(path, "wb") as f:
        f.write(blob)
    if verbose:
        print(f"{path}: {len(blob)} bytes, {len(out.relocs)} relocs, "
              f"{len(tracks)} tracks")
    return chor


def hydra_glow_tracks(arc):
    """Decode the impact-flash tracks off Hydra's glow FigaTree, keyed by the
    joint index they drive."""
    d = arc.data
    counts = []
    p = HYDRA_FIGATREE_TABLE
    while d[p] != 0xFF:
        counts.append(d[p])
        p += 1
    out = {}
    t = HYDRA_FIGATREE_TRACKS
    for node, n in enumerate(counts):
        for _ in range(n):
            length = u16(d, t)
            start = struct.unpack_from(">h", d, t + 2)[0]
            kind, vflag, tflag = d[t + 4], d[t + 5], d[t + 6]
            keys = arc.deref(t + 8)
            out.setdefault(node, []).append(
                (kind, vflag, tflag, start, bytes(d[keys:keys + length])))
            t += 0x0C
    return out


def build_glow(root_dir, chor, verbose=True):
    arc = Archive(os.path.join(root_dir, HYDRA_DAT))
    flash = hydra_glow_tracks(arc)

    patched = bytearray(arc.data)
    # Tighten the camera orbit. The path is a JOBJ carrying an HSD_Spline;
    # its scale.x and scale.y are the two horizontal axes of the orbit (the
    # spline itself lies in the XY plane and is swung flat by the joint's
    # rotation), and scale.z has nothing to act on.
    for slot in (0x20, 0x24):
        off = HYDRA_CAM_PATH_JOINT + slot
        struct.pack_into(">f", patched, off, f32(arc.data, off) * CAM_ORBIT_SCALE)
    # Cut streak groups 2 and 3 out of both trees before the walk, so only one
    # group's geometry and material animation is carried over to copy from.
    struct.pack_into(">I", patched, HYDRA_G1_ROOT + 0x0C, HYDRA_FLASH_ROOT)
    struct.pack_into(">I", patched, HYDRA_MA_G1_ROOT + 0x04, HYDRA_MA_FLASH)
    arc.data = bytes(patched)

    visited = Walker(arc).walk(HYDRA_GLOW_ROOT, "JOBJDesc")
    visited.update(Walker(arc).walk(HYDRA_GLOW_MATANIM, "MatAnimJoint"))
    visited[HYDRA_CAM_LO] = ("opaque", HYDRA_CAM_HI - HYDRA_CAM_LO)

    prefix = bytearray(0x20)
    res = carve_ranges(arc, visited, prefix,
                       base_relocs=(0x00, 0x04, 0x08, 0x10))
    out = Out(res.data, res.relocs)
    rm = res.remap

    glow_root = rm[HYDRA_GLOW_ROOT]
    ma_root = rm[HYDRA_GLOW_MATANIM]
    # A head bone's vertices sit this far down its own Y from the bone itself,
    # per its inverse bind matrix, so the ribbon tip trails the bone.
    bind_lift = -f32(arc.data, arc.deref(
        arc.deref(arc.deref(HYDRA_G1_MESH + 0x08) + 0x0C) + 0x38) + 0x1C)

    # Six streak groups: the carved one, then five copies. A group is four
    # joints (root, envelope mesh, anchor bone, head bone); the mesh needs its
    # own DObj, POBJ and envelope array so its two bones resolve to its own
    # copies, and shares the material, vertex array and display list.
    g0 = group_joints(arc, HYDRA_G1_ROOT, rm)
    g0_dobj = out.rd32(g0[1] + 0x10)
    g0_pobj = out.rd32(g0_dobj + 0x0C)
    g0_envs = out.rd32(g0_pobj + 0x14)

    groups = [g0]
    for _ in range(NUM_PODS - 1):
        joints = [out.copy_struct(g0[k], 0x40) for k in range(4)]
        dobj = out.copy_struct(g0_dobj, 0x10)
        pobj = out.copy_struct(g0_pobj, 0x18)
        env_a = out.copy_struct(out.rd32(g0_envs + 0x00), 0x10)
        env_b = out.copy_struct(out.rd32(g0_envs + 0x04), 0x10)
        envs = out.alloc(0x0C)
        out.ptr(envs + 0x00, env_a)
        out.ptr(envs + 0x04, env_b)
        out.clr(envs + 0x08)
        out.ptr(env_a + 0x00, joints[2])
        out.ptr(env_b + 0x00, joints[3])
        out.ptr(pobj + 0x14, envs)
        out.ptr(dobj + 0x0C, pobj)
        out.ptr(joints[1] + 0x10, dobj)
        out.ptr(joints[0] + 0x08, joints[1])
        out.ptr(joints[1] + 0x08, joints[2])
        out.ptr(joints[2] + 0x0C, joints[3])
        out.clr(joints[2] + 0x08)
        out.clr(joints[3] + 0x08)
        out.clr(joints[3] + 0x0C)
        groups.append(joints)

    for a, b in zip(groups, groups[1:]):
        out.ptr(a[0] + 0x0C, b[0])
    out.ptr(groups[-1][0] + 0x0C, rm[HYDRA_FLASH_ROOT])
    out.ptr(glow_root + 0x08, groups[0][0])

    # The material-animation tree has to mirror the joint tree node for node.
    # Every streak shares one alpha curve, re-keyed so a streak stays up until
    # its own pod lands rather than Hydra's single 20-50 fade.
    ma_g0 = matanim_group(arc, HYDRA_MA_G1_ROOT, rm)
    streak_matanim = rm[HYDRA_MA_STREAK]
    aobj = out.rd32(streak_matanim + 0x04)
    fobjdesc = out.rd32(aobj + 0x08)
    alpha = fobj.encode(
        [fobj.Key(frame=f, value=v, op=fobj.OP_LIN) for f, v in
         ((0, 1.0), (STREAK_ON, 1.0), (60, 0.55), (max(ARRIVE), 0.35),
          (max(ARRIVE) + 1, 0.0), (int(END_FRAME), 0.0))],
        fobj.FMT_FLOAT, fobj.FMT_FLOAT)
    alpha_off = out.put(alpha)
    out.w32(fobjdesc + 0x04, len(alpha))
    out.w32(fobjdesc + 0x0C, out.rd32(fobjdesc + 0x0C) & 0xFF000000)
    out.ptr(fobjdesc + 0x10, alpha_off)

    ma_groups = [ma_g0]
    for _ in range(NUM_PODS - 1):
        nodes = [out.copy_struct(ma_g0[k], 0x0C) for k in range(4)]
        out.ptr(nodes[0] + 0x00, nodes[1])
        out.ptr(nodes[1] + 0x00, nodes[2])
        out.ptr(nodes[2] + 0x04, nodes[3])
        out.clr(nodes[2] + 0x00)
        out.clr(nodes[3] + 0x00)
        out.clr(nodes[3] + 0x04)
        out.ptr(nodes[1] + 0x08, streak_matanim)
        ma_groups.append(nodes)
    for a, b in zip(ma_groups, ma_groups[1:]):
        out.ptr(a[0] + 0x04, b[0])
    out.ptr(ma_groups[-1][0] + 0x04, rm[HYDRA_MA_FLASH])
    out.ptr(ma_root + 0x00, ma_groups[0][0])

    node_count = 4 * NUM_PODS + 6
    tracks = []
    for i in range(NUM_PODS):
        base = 1 + 4 * i
        path = chor.machine_path(i)

        # Aim the group so its local +Y runs out along the pod's approach; the
        # ribbon is a quad in the mesh joint's local XY plane, so this also
        # decides which way its width faces.
        sx, sy, sz = path[0][1]
        length = math.sqrt(sx * sx + sy * sy + sz * sz)
        aim_y = math.atan2(sx, sz)
        aim_x = math.acos(max(-1.0, min(1.0, sy / length)))
        aim = rot_zyx(aim_x, aim_y, 0.0)
        tracks.append(Track(base + 1, "ROTX", [(0, aim_x), (int(END_FRAME), aim_x)]))
        tracks.append(Track(base + 1, "ROTY", [(0, aim_y), (int(END_FRAME), aim_y)]))
        tracks.append(Track(base + 1, "ROTZ", [(0, 0.0), (int(END_FRAME), 0.0)]))

        strobe = [(0, 0.0)]
        for f in range(STREAK_ON, ARRIVE[i] + 1):
            strobe.append((f, float((f - STREAK_ON) & 1)))
        strobe.append((ARRIVE[i] + 1, 0.0))
        strobe.append((int(END_FRAME), 0.0))
        tracks.append(Track(base + 0, "BRANCH", strobe, op=fobj.OP_CON))

        taper = [(0, 1.5), (STREAK_ON, 1.5), (ARRIVE[i], 1.0), (int(END_FRAME), 1.0)]
        tracks.append(Track(base + 2, "SCAX", taper))
        tracks.append(Track(base + 3, "SCAX", taper))

        head = [(f, tuple(c * STREAK_REACH + (bind_lift if k == 1 else 0.0)
                          for k, c in enumerate(mat_apply_t(aim, p))))
                for f, p in path]
        for axis, comp in (("TRAX", 0), ("TRAY", 1), ("TRAZ", 2)):
            tracks.append(Track(base + 3, axis, [(f, p[comp]) for f, p in head]))

    # The flash joints keep Hydra's own tracks. Only the flare's visibility is
    # re-authored, from three pulses to one per pod.
    for src_node, dst_node in zip(range(13, 18), range(4 * NUM_PODS + 1, node_count)):
        for kind, vflag, tflag, start, buf in flash.get(src_node, ()):
            if src_node == 17 and kind == TRACK["BRANCH"]:
                continue
            tracks.append(Track(dst_node, kind, raw=buf, value_flag=vflag,
                                tan_flag=tflag, start_frame=start))
    pulses = [(0, 0.0)]
    for arrive in ARRIVE:
        pulses.append((arrive, 1.0))
        pulses.append((arrive + 2, 0.0))
    pulses.append((int(END_FRAME), 0.0))
    tracks.append(Track(node_count - 1, "BRANCH", pulses, op=fobj.OP_CON))

    tree = emit_figatree(out, node_count, tracks)
    out.ptr(0x00, glow_root)
    out.ptr(0x04, tree)
    out.ptr(0x08, ma_root)
    out.ptr(0x10, rm[HYDRA_CAM_DESC])

    blob = build_archive(out.d, sorted(out.relocs),
                         [("apStarGlow", 0), ("apStarCam", 0x10)], arc.version)
    path = os.path.join(root_dir, OUT_DIR, "ApStarGlow.dat")
    with open(path, "wb") as f:
        f.write(blob)
    if verbose:
        print(f"{path}: {len(blob)} bytes, {len(out.relocs)} relocs, "
              f"{len(tracks)} tracks, {node_count} joints")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=os.getcwd(),
                    help="repo root (default: cwd)")
    args = ap.parse_args(argv)
    chor = build_parts(args.root)
    build_glow(args.root, chor)
    return 0


if __name__ == "__main__":
    sys.exit(main())
