# SPDX-License-Identifier: GPL-3.0-only
"""A stage's collision, zones and authored positions in world units.

The stage files are authored unscaled and in joint-local frames. At load the
game bakes every collision and zone vertex by its joint's world matrix
(`grColl_BakeRecords`, 0x800d6a70) and scales the position lists by the stage
scale. `Stage` repeats that bake once, so everything it hands out is in the
units a live machine position is in:

    world = joint_world[bone] * file_vertex

The joint table (`grJointTable_Init`, 0x800d8b98) is the preorder of the
terrain model's root JObj (`Gr<X>Model.dat`, `grModel<X>` slot 0), entry 0
the root and the root's siblings left out; collision joints, zone joints and
stage props name a joint by its index there. `CreateStageModel_3D`
(0x800dcbf0) replaces the root's scale with the stage scale and keeps its
rotation and translation. The bake uses the rest pose, so moving geometry
(lifts, gates, gondolas) is where it starts. The out-of-bounds box is compared
with world positions as stored, unscaled.

Queries run over a uniform XZ grid of triangle buckets, so a point query
touches a handful of triangles instead of the whole mesh.
"""

import math
import os
from collections import namedtuple

from .archive import Archive, f32, s32, u32
from .format import COLL_JOINT_KINDS, zone_kind_name
from .geom_bounds import mat_apply, mat_mul
from .spline import Spline

ISO_FILES = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "iso", "files"
)

# GrCollTri.kind bits 0-2 (the response category every query masks against).
CAT_FLOOR, CAT_WALL, CAT_CEILING = 1, 2, 4
CAT_NAMES = {CAT_FLOOR: "floor", CAT_WALL: "wall", CAT_CEILING: "ceiling"}

# Ground type (GrCollTri.kind bits 4-11) -> surface, named after the sounds the
# game plays on it: GrCommon.dat's ground table (47 rows of 0x1C, at r13+0x5e8)
# gives each type a sound index (+0x14) into VcCommon.dat's machine sound rows,
# whose FGM names say the material. 31-46 are the courses' rough off-road
# versions (speed x0.8 / x0.7).
GROUND_TYPE_NAMES = {
    0: "dirt", 1: "gravel", 2: "sand", 3: "grass", 4: "grass slick", 5: "asphalt",
    6: "asphalt grippy", 7: "ice", 8: "snow", 9: "checker", 10: "checker slick", 11: "wood",
    12: "stone", 13: "rock", 14: "lava", 15: "leaves", 16: "crystal", 17: "metal",
    18: "stream", 19: "wire mesh", 20: "oil", 21: "sand grippy", 22: "rock moving",
    23: "rock moving slick", 24: "rock wall", 25: "space track", 26: "grind rail",
    27: "rail dash", 28: "water rail", 29: "sea", 30: "barrier",
    31: "rough grass", 32: "rough rock", 33: "rough gravel", 34: "rough water", 35: "rough ice",
    36: "rough asphalt", 37: "rough wood", 38: "rough grass", 39: "rough water",
    40: "rough metal", 41: "rough gravel", 42: "rough sand", 43: "rough sand",
    44: "rough grass", 45: "rough grass", 46: "rough sand",
}

POSITION_LISTS = [
    (0x04, "start"),
    (0x08, "enemy"),
    (0x0C, "gravity"),
    (0x10, "airflow"),
    (0x14, "conveyor"),
    (0x18, "item"),
    (0x1C, "event"),
    (0x20, "vehicle"),
    (0x24, "global_dead"),
    (0x28, "local_dead"),
    (0x2C, "yakumono"),
]
AREA_LISTS = [(0x30, "item_area"), (0x34, "vehicle_area")]

GRID_CELL = 16.0

# GrCollTri.flags bit marking a record that animates; its first triangle carries it.
TRI_FLAG_MOVING = 0x20

Tri = namedtuple("Tri", "v0 v1 v2 normal cat gtype flags cjoint bone jkind")
CollJoint = namedtuple("CollJoint", "bone kind face_start face_num moving")
Zone = namedtuple("Zone", "index kind name param_index bone link verts planes lo hi")
Position = namedtuple("Position", "list index pos forward up")
RangePair = namedtuple("RangePair", "index left right area flags")
Prop = namedtuple("Prop", "kind name data_idx instance joint pos yaw lo hi")

# grDataCity1_CreateYakumono (0x8010f268) creates data_array slots 0-30 with a kind
# hardcoded per slot. Each slot's parameter block names the prop's joint: ("at", off)
# reads it at that offset of the block's descriptor, ("list", stride) walks the
# descriptor's {entries, count} list, whose entries hold a joint at +4.
CITY_PROPS = {
    0: (17, ("at", 0)),
    1: (18, ("at", 0)),
    **{i: (46, ("at", 0)) for i in range(2, 18)},
    18: (61, ("at", 0)),
    19: (61, ("at", 0)),
    20: (68, ("at", 0)),
    21: (69, ("at", 0)),
    22: (32, ("at", 0)),
    23: (32, ("at", 0)),
    24: (37, ("list", 0x14)),
    25: (36, ("list", 0x14)),
    26: (38, ("list", 0x14)),
    27: (33, ("list", 0x10)),
    28: (34, ("list", 0x10)),
    29: (35, ("list", 0x10)),
    30: (29, ("at", 4)),
}

# The event archive's location ranges into City Trial's event positions.
CITY_EVENT_POSITIONS = [
    (0, 10, "Dyna Blade"),
    (10, 20, "Tac"),
    (20, 30, "Meteor"),
    (30, 40, "Pillar"),
    (40, 60, "Restoration Area"),
    (60, 70, "Rail Fire"),
    (70, 80, "Secret Chamber"),
    (80, 100, "Machine Formation"),
]


def city_event_name(index):
    return next((name for lo, hi, name in CITY_EVENT_POSITIONS if lo <= index < hi), None)


def yaku_kind_names():
    """YakuKind value -> enum name, read from hoshi so the two never drift."""
    import re

    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "externals",
                        "hoshi", "include", "yakumono.h")
    try:
        with open(path) as f:
            src = f.read()
    except OSError:
        return {}
    body = re.search(r"typedef enum YakuKind\s*\{(.*?)\}\s*YakuKind;", src, re.DOTALL)
    out = {}
    for name, value in re.findall(r"YAKUKIND_(\w+)\s*=\s*(-?\d+)", body.group(1) if body else ""):
        out.setdefault(int(value), name)
    return out
Rail = namedtuple("Rail", "index line path next prev twin bone radius")


def ground_type_name(gtype, number=False):
    name = GROUND_TYPE_NAMES.get(gtype, f"type {gtype}")
    return f"{name} ({gtype})" if number and gtype in GROUND_TYPE_NAMES else name


def heading_deg(fx, fz):
    """Heading of a forward vector in degrees from +Z toward +X."""
    return math.degrees(math.atan2(fx, fz))


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _norm(a):
    n = math.sqrt(_dot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n) if n > 0 else (0.0, 0.0, 0.0)


# (file stem, GroundKind, player-facing name, aliases). The GroundKind indexes
# the game's stage file table (0x804a2ffc). Top Ride's courses are tile maps in
# A2a2dBG_*.dat and have no ground file.
STAGES = [
    ("GrPlants1", 0, "Fantasy Meadows", ["meadows"]),
    ("GrHeat2", 1, "Magma Flows", ["magma"]),
    ("GrDesert1", 2, "Sky Sands", ["sands", "desert"]),
    ("GrCheck2", 3, "Checker Knights", ["checker"]),
    ("GrValley2", 4, "Celestial Valley", ["valley"]),
    ("GrMachine2", 5, "Machine Passage", ["machine"]),
    ("GrSpace2", 6, "Nebula Belt", ["nebula", "space"]),
    ("GrSky2", 7, "Beanstalk Park", ["beanstalk"]),
    ("GrIce1", 8, "Frozen Hillside", ["ice", "hillside"]),
    ("GrCity1", 9, "City Trial", ["city", "citytrial", "freerun", "derby4", "derby5"]),
    ("GrZeroyon1", 10, "Drag Race 3", ["drag3"]),
    ("GrZeroyon3", 11, "Drag Race 2", ["drag2"]),
    ("GrZeroyon4", 12, "Drag Race 4", ["drag4"]),
    ("GrZeroyon5", 13, "Drag Race 1", ["drag1"]),
    ("GrPasture1", 14, "Kirby Melee 1", ["melee1"]),
    ("GrColosseum1", 15, "Destruction Derby 1", ["derby1", "dedede", "vsdedede"]),
    ("GrColosseum3", 16, "Destruction Derby 2", ["derby2"]),
    ("GrColosseum5", 17, "Kirby Melee 2", ["melee2"]),
    ("GrJump1", 18, "High Jump", ["highjump"]),
    ("GrJump2", 19, "Target Flight", ["target"]),
    ("GrJump3", 20, "Air Glider", ["glider"]),
    ("GrDedede1", 21, "Destruction Derby 3", ["derby3"]),
    ("GrSimple", 26, "Collision test ground", ["simple"]),
    ("GrSimple2", 27, "Collision test ground 2", ["simple2"]),
]


def stage_file(name):
    """The file stem an alias or player-facing name stands for, else `name`."""
    key = name.lower().replace(" ", "").replace("_", "")
    for stem, _, label, aliases in STAGES:
        if key in [stem.lower(), stem[2:].lower(), label.lower().replace(" ", "")] + aliases:
            return stem
    prefix = [stem for stem, _, _, _ in STAGES if stem[2:].lower().startswith(key)]
    if len(prefix) == 1:
        return prefix[0]
    if os.path.exists(name) or name.startswith("Gr"):
        return name
    return "Gr" + name


def resolve(name):
    """(data_path, model_path or None) for a stage given as `GrCity1`, `City1`
    or a path to its `Gr*.dat`."""
    if os.path.exists(name):
        data = name
    else:
        stem = name if name.startswith("Gr") else "Gr" + name
        data = os.path.join(ISO_FILES, stem + ".dat")
        if not os.path.exists(data):
            raise FileNotFoundError(f"no stage file {stem}.dat under iso/files")
    base, ext = os.path.splitext(data)
    model = base + "Model" + ext
    return data, (model if os.path.exists(model) else None)


class Stage:
    def __init__(self, name):
        self.data_path, self.model_path = resolve(name)
        self.stem = os.path.splitext(os.path.basename(self.data_path))[0]
        self.arc = Archive(self.data_path)
        pubs = [p for p in self.arc.publics if p.startswith("grData")]
        if not pubs:
            raise ValueError(f"{self.stem}.dat has no grData public")
        self.gd = self.arc.publics[pubs[0]]
        d = self.arc.data
        sn = self._ptr(self.gd + 0x04)
        self.scale = f32(d, sn + 0x08)
        self.gravity = f32(d, sn + 0x0C)
        self.oob_min = tuple(f32(d, sn + 0xCC + 4 * k) for k in range(3))
        self.oob_max = tuple(f32(d, sn + 0xD8 + 4 * k) for k in range(3))
        self.joints, self.joint_parent = self._joint_table()
        self._load_collision()
        self._load_zones()
        self._load_positions()
        self._build_grid()
        self._splines = None

    @property
    def label(self):
        return next((row[2] for row in STAGES if row[0] == self.stem), self.stem)

    @property
    def splines(self):
        """The spline web in world units, read on first use: `course` (the
        course pieces), `route` (the indices of the pieces one lap runs),
        `loop`, `cpu` (RangePair left/right road edges CPUs steer between),
        `conveyor`, `rail`, `heavy`, and `rails` (each Rail names its LINEAR
        collision `line` and the `path` ridden, both indices into `rail`)."""
        if self._splines is None:
            self._splines = self._load_splines()
        return self._splines

    def _spline_list(self, lst):
        d = self.arc.data
        if not lst:
            return []
        base = self._ptr(lst)
        out = []
        for i in range(u32(d, lst + 0x04)):
            sp = self._ptr(base + 4 * i)
            if sp:
                out.append(Spline(self.arc, sp, self.scale))
        return out

    def _load_splines(self):
        d = self.arc.data
        out = dict(course=[], route=[], loop=False, cpu=[], conveyor=[], rail=[], heavy=[], rails=[])
        sn = self._ptr(self.gd + 0x1C)
        if sn:
            setup = self._ptr(sn)
            if setup:
                out["course"] = self._spline_list(self._ptr(setup))
                route = self._ptr(setup + 0x04)
                if route:
                    base = self._ptr(route)
                    out["route"] = [s32(d, base + 4 * i) for i in range(u32(d, route + 0x04))]
                out["loop"] = bool(d[setup + 0x10])
            rng = self._ptr(sn + 0x04)
            if rng:
                base = self._ptr(rng)
                for i in range(u32(d, rng + 0x04)):
                    r = base + 0x18 * i
                    left, right = self._ptr(r), self._ptr(r + 0x04)
                    out["cpu"].append(RangePair(
                        i, Spline(self.arc, left, self.scale) if left else None,
                        Spline(self.arc, right, self.scale) if right else None,
                        u32(d, r + 0x10), u32(d, r + 0x14)))
            conv = self._ptr(sn + 0x10)
            if conv:
                out["conveyor"] = self._spline_list(self._ptr(conv))
            out["rail"] = self._spline_list(self._ptr(sn + 0x14))
            out["heavy"] = self._spline_list(self._ptr(sn + 0x18))
        rn = self._ptr(self.gd + 0x38)
        if rn:
            base = self._ptr(rn)
            for i in range(u32(d, rn + 0x04)):
                r = self._ptr(base + 4 * i)
                if not r:
                    continue
                param = self._ptr(r)
                out["rails"].append(Rail(
                    i, s32(d, r + 0x04), s32(d, r + 0x08),
                    [s32(d, r + 0x10 + 4 * k) for k in range(3)], s32(d, r + 0x1C),
                    s32(d, r + 0x28), s32(d, r + 0x2C), f32(d, param + 0x08) if param else 0.0))
        return out

    def props(self):
        """City Trial's placed props, one per instance, with the joint they hang
        off and the bounds of the collision under that joint. Other stages place
        props from their own code and get an empty list."""
        if self.stem != "GrCity1":
            return []
        d = self.arc.data
        yn = self._ptr(self.gd + 0x40)
        arr = self._ptr(yn) if yn else 0
        names = yaku_kind_names()
        under = {}
        for cj in self.coll_joints:
            b = cj.bone
            while b >= 0:
                under.setdefault(b, []).append(cj)
                b = self.joint_parent[b] if b < len(self.joint_parent) else -1
        out = []

        def add(kind, di, n, j):
            m = self.joint(j)
            pts = [v for cj in under.get(j, ())
                   for t in self.tris[cj.face_start:cj.face_start + cj.face_num]
                   for v in (t.v0, t.v1, t.v2)]
            lo = tuple(min(p[k] for p in pts) for k in range(3)) if pts else None
            hi = tuple(max(p[k] for p in pts) for k in range(3)) if pts else None
            out.append(Prop(kind, names.get(kind, f"kind{kind}").lower(), di, n, j,
                            (m[0][3], m[1][3], m[2][3]), math.degrees(math.atan2(m[0][2], m[2][2])),
                            lo, hi))

        for di, (kind, (how, arg)) in CITY_PROPS.items():
            block = self._ptr(arr + 4 * di) if arr else 0
            desc = self._ptr(block) if block else 0
            if not desc:
                continue
            if how == "at":
                add(kind, di, 0, s32(d, desc + arg))
            else:
                ents = self._ptr(desc)
                for n in range(s32(d, desc + 4)):
                    add(kind, di, n, s32(d, ents + arg * n + 4))
        return out

    def lap_length(self):
        sp = self.splines
        return sum(sp["course"][i].length for i in sp["route"] if 0 <= i < len(sp["course"]))

    def _ptr(self, off):
        return self.arc.ptr(off) if off in self.arc.reloc_set else 0

    def joint(self, bone):
        """World matrix (3x4) of a joint-table entry."""
        if 0 <= bone < len(self.joints):
            return self.joints[bone]
        return self.joints[0]

    def _joint_table(self):
        if not self.model_path:
            return joint_world_matrices(None, 0, self.scale), [-1]
        m = Archive(self.model_path)
        pubs = [p for p in m.publics if p.startswith("grModel") and "Motion" not in p]
        terrain = m.ptr(m.publics[pubs[0]]) if pubs else 0
        root = m.ptr(terrain) if terrain and terrain in m.reloc_set else 0
        parents = []
        mats = joint_world_matrices(m, root, self.scale, parents)
        return mats, parents

    def _load_collision(self):
        d = self.arc.data
        cn = self._ptr(self.gd + 0x18)
        self.verts, self.tris, self.coll_joints = [], [], []
        if not cn:
            return
        vp, tp, jp = self._ptr(cn), self._ptr(cn + 0x08), self._ptr(cn + 0x10)
        raw = [
            (f32(d, vp + 12 * i), f32(d, vp + 12 * i + 4), f32(d, vp + 12 * i + 8))
            for i in range(u32(d, cn + 0x04))
        ]
        self.verts = list(raw)
        owner = {}
        for j in range(u32(d, cn + 0x14)):
            r = jp + 0x1C * j
            bone, vs, vn = s32(d, r), u32(d, r + 0x04), u32(d, r + 0x08)
            fs, fn, kind = u32(d, r + 0x0C), u32(d, r + 0x10), u32(d, r + 0x14)
            m = self.joint(bone)
            for v in range(vs, vs + vn):
                self.verts[v] = mat_apply(m, *raw[v])
            for f in range(fs, fs + fn):
                owner[f] = j
            moving = fn > 0 and u32(d, tp + 0x14 * fs + 0x10) & TRI_FLAG_MOVING != 0
            self.coll_joints.append(CollJoint(bone, kind, fs, fn, moving))
        for i in range(u32(d, cn + 0x0C)):
            r = tp + 0x14 * i
            a, b, c = (self.verts[u32(d, r + 4 * k)] for k in range(3))
            kind, flags = u32(d, r + 0x0C), u32(d, r + 0x10)
            j = owner.get(i, -1)
            bone, jkind = (self.coll_joints[j].bone, self.coll_joints[j].kind) if j >= 0 else (0, 0)
            self.tris.append(
                Tri(a, b, c, _norm(_cross(_sub(c, a), _sub(b, a))), kind & 7,
                    (kind >> 4) & 0xFF, flags, j, bone, jkind)
            )

    def _load_zones(self):
        d = self.arc.data
        cn = self._ptr(self.gd + 0x18)
        self.zones = []
        if not cn:
            return
        vp, tp, jp = self._ptr(cn + 0x18), self._ptr(cn + 0x20), self._ptr(cn + 0x28)
        for z in range(u32(d, cn + 0x2C)):
            r = jp + 0x4C * z
            bone, vs, vn = s32(d, r), u32(d, r + 0x04), u32(d, r + 0x08)
            fs, fn = u32(d, r + 0x0C), u32(d, r + 0x10)
            m = self.joint(bone)
            verts = [
                mat_apply(m, *(f32(d, vp + 12 * v + 4 * k) for k in range(3)))
                for v in range(vs, vs + vn)
            ]
            word = u32(d, tp + 0x18 * fs + 0x10) if fn else 0
            kind = word & 0x01FFFFFF
            cx = sum(v[0] for v in verts) / len(verts)
            cy = sum(v[1] for v in verts) / len(verts)
            cz = sum(v[2] for v in verts) / len(verts)
            planes = []
            for f in range(fs, fs + fn):
                t = tp + 0x18 * f
                a, b, c = (
                    mat_apply(m, *(f32(d, vp + 12 * u32(d, t + 4 + 4 * k) + 4 * q) for q in range(3)))
                    for k in range(3)
                )
                n = _norm(_cross(_sub(b, a), _sub(c, a)))
                if n == (0.0, 0.0, 0.0):
                    continue
                if _dot(n, _sub((cx, cy, cz), a)) > 0:
                    n = (-n[0], -n[1], -n[2])
                planes.append((n, _dot(n, a)))
            lo = tuple(min(v[k] for v in verts) for k in range(3))
            hi = tuple(max(v[k] for v in verts) for k in range(3))
            self.zones.append(
                Zone(z, kind, zone_kind_name(kind), word >> 28, bone, s32(d, r + 0x14),
                     verts, planes, lo, hi)
            )

    def _load_positions(self):
        d = self.arc.data
        pn = self._ptr(self.gd + 0x20)
        self.positions = {}
        if not pn:
            return
        pj = self._ptr(pn)
        pjoints = joint_world_matrices(self.arc, pj, self.scale) if pj else []
        s = self.scale
        root = [[s, 0, 0, 0], [0, s, 0, 0], [0, 0, s, 0]]
        for slots, stride in ((POSITION_LISTS, 0x24), (AREA_LISTS, 0x18)):
            for foff, name in slots:
                lst = self._ptr(pn + foff)
                if not lst:
                    continue
                n, idx, inline = u32(d, lst + 0x08), self._ptr(lst), self._ptr(lst + 0x04)
                out = []
                for i in range(n):
                    if inline:
                        r = inline + stride * i
                        vals = [f32(d, r + 4 * k) for k in range(stride // 4)]
                        pos = mat_apply(root, *vals[0:3])
                        fwd = _norm(tuple(vals[3:6]))
                        up = _norm(tuple(vals[6:9])) if stride == 0x24 else (0.0, 1.0, 0.0)
                    elif idx:
                        j = u32(d, idx + 4 * i)
                        m = pjoints[j] if j < len(pjoints) else root
                        pos = (m[0][3], m[1][3], m[2][3])
                        fwd = _norm((m[0][2], m[1][2], m[2][2]))
                        up = _norm((m[0][1], m[1][1], m[2][1]))
                    else:
                        continue
                    out.append(Position(name, i, pos, fwd, up))
                self.positions[name] = out

    def _build_grid(self):
        xs = [v[0] for v in self.verts] + [self.oob_min[0], self.oob_max[0]]
        zs = [v[2] for v in self.verts] + [self.oob_min[2], self.oob_max[2]]
        self.gx0, self.gz0 = min(xs), min(zs)
        self.gnx = int((max(xs) - self.gx0) // GRID_CELL) + 1
        self.gnz = int((max(zs) - self.gz0) // GRID_CELL) + 1
        self.grid = {}
        for i, t in enumerate(self.tris):
            ix0, iz0 = self._cell(min(t.v0[0], t.v1[0], t.v2[0]), min(t.v0[2], t.v1[2], t.v2[2]))
            ix1, iz1 = self._cell(max(t.v0[0], t.v1[0], t.v2[0]), max(t.v0[2], t.v1[2], t.v2[2]))
            for ix in range(ix0, ix1 + 1):
                for iz in range(iz0, iz1 + 1):
                    self.grid.setdefault((ix, iz), []).append(i)

    def _cell(self, x, z):
        ix = min(max(int((x - self.gx0) // GRID_CELL), 0), self.gnx - 1)
        iz = min(max(int((z - self.gz0) // GRID_CELL), 0), self.gnz - 1)
        return ix, iz

    def column(self, x, z, cats=CAT_FLOOR | CAT_WALL | CAT_CEILING):
        """Every triangle crossing the vertical line at (x, z), as (y, tri index),
        highest first. Near-vertical triangles are left out."""
        out = []
        for i in self.grid.get(self._cell(x, z), ()):
            t = self.tris[i]
            if not t.cat & cats or abs(t.normal[1]) < 1e-3:
                continue
            y = _tri_y(t, x, z)
            if y is not None:
                out.append((y, i))
        out.sort(reverse=True)
        return out

    def floors(self, x, z, skip_gtypes=(30,)):
        """The standable surfaces at (x, z), highest first: floor triangles that
        face up, so the ones a downward Raycast_Ground can land on."""
        return [
            (y, i)
            for y, i in self.column(x, z, CAT_FLOOR)
            if self.tris[i].normal[1] > 0 and self.tris[i].gtype not in skip_gtypes
        ]

    def ground(self, x, z, below=None, skip_gtypes=(30,)):
        """(y, tri index) of the first standable surface at or below `below`
        (the top one when None), or None."""
        for y, i in self.floors(x, z, skip_gtypes):
            if below is None or y <= below + 1e-3:
                return y, i
        return None

    def ceiling(self, x, y, z):
        """(y, tri index) of the nearest downward-facing triangle above y, any
        category, or None."""
        best = None
        for ty, i in self.column(x, z):
            t = self.tris[i]
            if ty > y + 1e-3 and t.normal[1] < 0 and t.gtype != 30 and (best is None or ty < best[0]):
                best = (ty, i)
        return best

    def raycast(self, p0, p1, cats=CAT_FLOOR | CAT_WALL | CAT_CEILING, two_sided=False,
                skip_gtypes=(30,)):
        """Nearest hit of the segment p0 -> p1 as (t, point, tri index), or None.
        One-sided like the game's narrowphase unless `two_sided`."""
        dvec = _sub(p1, p0)
        best = None
        for i in self._cells_along(p0, p1):
            t = self.tris[i]
            if not t.cat & cats or t.gtype in skip_gtypes:
                continue
            if not two_sided and _dot(dvec, t.normal) >= 0:
                continue
            h = _ray_tri(p0, dvec, t)
            if h is not None and (best is None or h < best[0]):
                best = (h, i)
        if best is None:
            return None
        h, i = best
        return h, (p0[0] + dvec[0] * h, p0[1] + dvec[1] * h, p0[2] + dvec[2] * h), i

    def _cells_along(self, p0, p1):
        seen = set()
        n = int(math.hypot(p1[0] - p0[0], p1[2] - p0[2]) / (GRID_CELL * 0.5)) + 1
        cells = set()
        for k in range(n + 1):
            u = k / n
            ix, iz = self._cell(p0[0] + (p1[0] - p0[0]) * u, p0[2] + (p1[2] - p0[2]) * u)
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    cells.add((ix + dx, iz + dz))
        for c in cells:
            for i in self.grid.get(c, ()):
                if i not in seen:
                    seen.add(i)
                    yield i

    def zones_at(self, p):
        """Zones whose box contains p."""
        out = []
        for z in self.zones:
            if not all(z.lo[k] - 1e-3 <= p[k] <= z.hi[k] + 1e-3 for k in range(3)):
                continue
            if all(_dot(n, p) <= dist + 1e-3 for n, dist in z.planes):
                out.append(z)
        return out

    def in_oob(self, p):
        return all(self.oob_min[k] <= p[k] <= self.oob_max[k] for k in range(3))

    def tri_desc(self, i):
        t = self.tris[i]
        return (
            f"{CAT_NAMES.get(t.cat, f'cat{t.cat}')} {ground_type_name(t.gtype, number=True)}"
            f" joint {t.bone} ({COLL_JOINT_KINDS.get(t.jkind, t.jkind).lower()})"
        )


LANDMARKS_CSV = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs", "data", "stage-landmarks.csv"
)
LANDMARK_COLUMNS = ["stage", "name", "aliases", "shape", "x", "y", "z", "r", "hx", "hy", "hz",
                    "source", "note"]


class Landmark:
    """A named place: a point, a sphere (r), a vertical cylinder (r, hy) or a
    box (hx, hy, hz), all centered on (x, y, z) in world units."""

    def __init__(self, row):
        self.stage = row["stage"]
        self.name = row["name"]
        self.aliases = [a.strip() for a in row.get("aliases", "").split(";") if a.strip()]
        self.shape = row.get("shape") or "point"
        self.center = tuple(float(row[k]) for k in ("x", "y", "z"))

        def num(k):
            v = row.get(k, "")
            return float(v) if v not in ("", None) else 0.0

        self.r, self.hx, self.hy, self.hz = num("r"), num("hx"), num("hy"), num("hz")
        self.source = row.get("source", "")
        self.note = row.get("note", "")

    def names(self):
        return [self.name] + self.aliases

    def matches(self, text):
        text = text.lower()
        return any(text in n.lower() for n in self.names())

    def distance(self, p):
        """Distance from p to the landmark's shape, 0 inside it."""
        c = self.center
        d = [p[k] - c[k] for k in range(3)]
        if self.shape == "sphere":
            return max(0.0, math.sqrt(_dot(d, d)) - self.r)
        if self.shape == "cylinder":
            h = max(0.0, math.hypot(d[0], d[2]) - self.r)
            v = max(0.0, abs(d[1]) - self.hy)
            return math.hypot(h, v)
        if self.shape == "box":
            e = [max(0.0, abs(d[0]) - self.hx), max(0.0, abs(d[1]) - self.hy),
                 max(0.0, abs(d[2]) - self.hz)]
            return math.sqrt(_dot(e, e))
        return math.sqrt(_dot(d, d))

    def extent(self):
        """A one-line description of the shape's size."""
        if self.shape == "sphere":
            return f"r {self.r:g}"
        if self.shape == "cylinder":
            return f"r {self.r:g}, y {self.center[1] - self.hy:g}..{self.center[1] + self.hy:g}"
        if self.shape == "box":
            return f"half {self.hx:g} x {self.hy:g} x {self.hz:g}"
        return "point"


def load_landmarks(stem=None, path=LANDMARKS_CSV):
    import csv

    if not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    return [Landmark(r) for r in rows if stem is None or r["stage"] == stem]


def append_landmark(row, path=LANDMARKS_CSV):
    import csv

    new = not os.path.exists(path)
    with open(path, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=LANDMARK_COLUMNS, lineterminator="\n")
        if new:
            w.writeheader()
        w.writerow({k: row.get(k, "") for k in LANDMARK_COLUMNS})


class Raster:
    """A top-down sampling of a stage's standable surfaces on a square grid of
    `res` world units, cell centers starting at (x0, z0). Per cell: the top
    surface's height and triangle (None where nothing is standable), the
    number of standable layers more than LAYER_GAP apart, and the land mask
    (a top surface that is not the sea). `max_y` drops every surface above
    it, to see under an overhang."""

    LAYER_GAP = 8.0

    def __init__(self, stage, res=4.0, bounds=None, max_y=None):
        self.res = res
        if bounds is None:
            bounds = (stage.oob_min[0], stage.oob_min[2], stage.oob_max[0], stage.oob_max[2])
        bx0, bz0, bx1, bz1 = bounds
        self.nx = max(1, math.ceil((bx1 - bx0) / res))
        self.nz = max(1, math.ceil((bz1 - bz0) / res))
        self.x0, self.z0 = bx0 + res / 2, bz0 + res / 2
        n = self.nx * self.nz
        self.top_y = [None] * n
        self.top_tri = [-1] * n
        cells = [[] for _ in range(n)]
        for i, t in enumerate(stage.tris):
            if t.cat != CAT_FLOOR or t.normal[1] <= 0 or t.gtype == 30:
                continue
            self._scan(t, i, cells)
        self.layers = [0] * n
        for c, hits in enumerate(cells):
            if max_y is not None:
                hits = [h for h in hits if h[0] <= max_y]
            if not hits:
                continue
            hits.sort(reverse=True)
            self.top_y[c], self.top_tri[c] = hits[0]
            k, last = 1, hits[0][0]
            for y, _ in hits[1:]:
                if last - y > self.LAYER_GAP:
                    k += 1
                    last = y
            self.layers[c] = k
        self.land = [
            ti >= 0 and stage.tris[ti].gtype != 29 for ti in self.top_tri
        ]

    def _scan(self, t, i, cells):
        (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = t.v0, t.v1, t.v2
        den = (z2 - z3) * (x1 - x3) + (x3 - x2) * (z1 - z3)
        if abs(den) < 1e-9:
            return
        res = self.res
        i0 = max(0, math.ceil((min(x1, x2, x3) - self.x0) / res))
        i1 = min(self.nx - 1, math.floor((max(x1, x2, x3) - self.x0) / res))
        j0 = max(0, math.ceil((min(z1, z2, z3) - self.z0) / res))
        j1 = min(self.nz - 1, math.floor((max(z1, z2, z3) - self.z0) / res))
        a, b, c, e = (z2 - z3) / den, (x3 - x2) / den, (z3 - z1) / den, (x1 - x3) / den
        nx = self.nx
        for j in range(j0, j1 + 1):
            z = self.z0 + j * res
            dz = z - z3
            row = j * nx
            for ii in range(i0, i1 + 1):
                x = self.x0 + ii * res
                dx = x - x3
                l1 = a * dx + b * dz
                if l1 < -1e-6:
                    continue
                l2 = c * dx + e * dz
                if l2 < -1e-6 or l1 + l2 > 1.0 + 1e-6:
                    continue
                cells[row + ii].append((l1 * y1 + l2 * y2 + (1.0 - l1 - l2) * y3, i))

    def index(self, x, z):
        """Cell index of the cell containing (x, z), or -1 outside."""
        i = math.floor((x - self.x0) / self.res + 0.5)
        j = math.floor((z - self.z0) / self.res + 0.5)
        if 0 <= i < self.nx and 0 <= j < self.nz:
            return j * self.nx + i
        return -1

    def center(self, c):
        return self.x0 + (c % self.nx) * self.res, self.z0 + (c // self.nx) * self.res

    def distance(self, mask):
        """Per-cell distance in world units to the nearest cell where `mask` is
        true (0 inside it), by a two-pass chamfer transform."""
        nx, nz = self.nx, self.nz
        big = float("inf")
        dist = [0.0 if m else big for m in mask]
        a, b = 1.0, math.sqrt(2.0)
        for j in range(nz):
            for i in range(nx):
                c = j * nx + i
                v = dist[c]
                if v == 0.0:
                    continue
                if i > 0:
                    v = min(v, dist[c - 1] + a)
                if j > 0:
                    v = min(v, dist[c - nx] + a)
                    if i > 0:
                        v = min(v, dist[c - nx - 1] + b)
                    if i < nx - 1:
                        v = min(v, dist[c - nx + 1] + b)
                dist[c] = v
        for j in range(nz - 1, -1, -1):
            for i in range(nx - 1, -1, -1):
                c = j * nx + i
                v = dist[c]
                if v == 0.0:
                    continue
                if i < nx - 1:
                    v = min(v, dist[c + 1] + a)
                if j < nz - 1:
                    v = min(v, dist[c + nx] + a)
                    if i < nx - 1:
                        v = min(v, dist[c + nx + 1] + b)
                    if i > 0:
                        v = min(v, dist[c + nx - 1] + b)
                dist[c] = v
        return [v * self.res for v in dist]


def mtx_srt(s, r, t, parent_scale):
    """HSD_MtxSRT: T * Rz * Ry * Rx * S, with the off-diagonal terms corrected
    by the parent's accumulated scale the way HSD_JObjMakeMatrix passes it."""
    sx, cx = math.sin(r[0]), math.cos(r[0])
    sy, cy = math.sin(r[1]), math.cos(r[1])
    sz, cz = math.sin(r[2]), math.cos(r[2])
    rot = [
        [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
        [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
        [-sy, cy * sx, cy * cx],
    ]

    def k(i, j):
        if parent_scale is None or i == j:
            return 1.0
        pi = parent_scale[i]
        return parent_scale[j] / (pi + (1.1920929e-07 if pi >= 0 else -1.1920929e-07))

    return [[rot[i][j] * s[j] * k(i, j) for j in range(3)] + [t[i]] for i in range(3)]


def joint_world_matrices(arc, root, scale, parents=None):
    """World matrices of a joint tree in the game's joint-table order: the root
    (its scale replaced by `scale`), then its children depth-first. Children of
    an INSTANCE joint are skipped, as HSD_JObjWalkTree does. Each joint's parent
    index is appended to `parents` when given."""
    if not root:
        return [[[scale, 0, 0, 0], [0, scale, 0, 0], [0, 0, scale, 0]]]
    d, out = arc.data, []

    def ptr(off):
        return u32(d, off) if off in arc.reloc_set else 0

    def visit(off, parent, parent_scale, parent_index):
        flags = u32(d, off + 0x04)
        r, s, t = ([f32(d, off + o + 4 * i) for i in range(3)] for o in (0x14, 0x20, 0x2C))
        if parent is None:
            s = [scale] * 3
        if flags & 0x8:  # CLASSICAL_SCALING: children see the parent's scale as is
            acc = parent_scale
        else:
            acc = [s[i] * parent_scale[i] for i in range(3)] if parent_scale else s
        local = mtx_srt(s, r, t, parent_scale)
        world = mat_mul(parent, local) if parent else local
        index = len(out)
        out.append(world)
        if parents is not None:
            parents.append(parent_index)
        if flags & 0x1000:
            return
        c = ptr(off + 0x08)
        while c:
            visit(c, world, acc, index)
            c = ptr(c + 0x0C)

    visit(root, None, None, -1)
    return out


def _tri_y(t, x, z):
    (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = t.v0, t.v1, t.v2
    den = (z2 - z3) * (x1 - x3) + (x3 - x2) * (z1 - z3)
    if abs(den) < 1e-9:
        return None
    l1 = ((z2 - z3) * (x - x3) + (x3 - x2) * (z - z3)) / den
    l2 = ((z3 - z1) * (x - x3) + (x1 - x3) * (z - z3)) / den
    l3 = 1.0 - l1 - l2
    if l1 < -1e-6 or l2 < -1e-6 or l3 < -1e-6:
        return None
    return l1 * y1 + l2 * y2 + l3 * y3


def _ray_tri(o, dvec, t):
    e1, e2 = _sub(t.v1, t.v0), _sub(t.v2, t.v0)
    p = _cross(dvec, e2)
    det = _dot(e1, p)
    if abs(det) < 1e-12:
        return None
    inv = 1.0 / det
    s = _sub(o, t.v0)
    u = _dot(s, p) * inv
    if u < 0.0 or u > 1.0:
        return None
    q = _cross(s, e1)
    v = _dot(dvec, q) * inv
    if v < 0.0 or u + v > 1.0:
        return None
    h = _dot(e2, q) * inv
    return h if 0.0 <= h <= 1.0 else None
