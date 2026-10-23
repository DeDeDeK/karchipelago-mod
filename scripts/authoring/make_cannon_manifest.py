#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Author CannonManifest.dat, the recipe custom_events rebuilds the cannon from.

Machine Passage's cannon has no joint or public of its own: its base, turret and
barrel are seven DObjs on the stage's terrain joint in `GrMachine2Model.dat`, and
the whole archive (1.5 MB) does not fit in City Trial's heap. So, as with the
backdrops, nothing is shipped but the recipe: which byte ranges of the file hold
those DObjs and how to relocate them. At runtime the mod reads the ranges off the
retail disc into one allocation and hangs the DObjs off a joint of its own.

The DObjs index the terrain's shared vertex arrays (~190 KB), but only a narrow
slice of them, so only that slice is read, and the array pointer is rebased so the
display lists' unchanged indices still land on the right elements.

The flight is a second, much smaller recipe over `GrMachine2.dat`: the AnimJoint
the stage's cannon launches machines along (rotation and path tracks, the path's
spline joint and its arc-length tables).

Both are found through the stage's own cannon parameters (`grDataMachine2`'s
yakumono data_array entry for the cannon), which name the barrel anchor joint and
the flight. Besides the two recipes the manifest carries what placing the cannon
elsewhere takes: the anchor's parent joint (the pivot, in stage units), the
anchor's local transform (machines are posed on its Y and Z axes), the mesh's
floor below the pivot, and the stage scale the cannon was modelled at.

Exports one public:

  cannonManifest  - CannonManifest {magic, version, mesh {donor, size, ranges,
                    range_num, relocs, reloc_num}, dobjs, dobj_num, pivot,
                    anchor_pos, anchor_rot, floor, scale, path {...}, path_root}

Run from the repo root:
    uv run python scripts/authoring/make_cannon_manifest.py
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, build_archive, f32, s32
from hsd.builder import dobjs_of, walk_joints
from hsd.geom_bounds import joint_world_positions, mat_identity
from hsd.payload import indexed_spans, plan_payload
from hsd.walker import Walker

MODEL_DONOR = "GrMachine2Model.dat"
MODEL_SYMBOL = "grModelMachine2"
STAGE_DONOR = "GrMachine2.dat"
STAGE_SYMBOL = "grDataMachine2"
OUTPUT = "mods/custom_events/assets/CannonManifest.dat"

# grDataMachine2_CreateYakumono creates the cannon with data_idx 1.
CANNON_DATA_IDX = 1
# Main-model joint (preorder) carrying the cannon's DObjs among its 72, and their
# indices in its DObj chain.
TERRAIN_JOINT = 104
CANNON_DOBJS = (1, 2, 11, 12, 13, 14, 15)

MANIFEST_MAGIC = 0x434E4D46  # 'CNMF'
MANIFEST_VERSION = 1
MANIFEST_SIZE = 0x70

DOBJ_NEXT = 0x04  # the runtime chains the kept DObjs itself


def vec3(arc, off):
    return tuple(f32(arc.data, off + 4 * i) for i in range(3))


def cannon_params(stage):
    """(stage scale, barrel anchor joint, flight AnimJoint) from the stage data."""
    d = stage.data
    gd = stage.publics[STAGE_SYMBOL]
    scale = f32(d, stage.ptr(gd + 0x04) + 0x08)
    data_array = stage.ptr(stage.ptr(gd + 0x40))
    desc = stage.ptr(stage.ptr(data_array + 4 * CANNON_DATA_IDX))
    joint, path_idx = s32(d, desc + 0x04), s32(d, desc + 0x10)
    paths = stage.ptr(stage.ptr(gd + 0x24) + 0x0C)
    path = stage.ptr(stage.ptr(paths) + 4 * path_idx)
    return scale, joint, path


def plan_mesh(model, anchor_joint):
    root = model.deref(model.deref(model.publics[MODEL_SYMBOL]))
    joints = walk_joints(model, root)
    offs = [off for off, _ in joints]
    chain = dobjs_of(model, offs[TERRAIN_JOINT])
    dobjs = [chain[i] for i in CANNON_DOBJS]

    # Each DObj walked on its own: blocking its successor keeps the walk out of the
    # rest of the terrain's chain.
    visited = {}
    for d in dobjs:
        w = Walker(model)
        nxt = model.ptr(d + DOBJ_NEXT)
        if nxt:
            w.visited[nxt] = ("stop", 0)
        got = w.walk(d, "DObjDesc")
        got.pop(nxt, None)
        visited.update(got)

    pobjs = []
    for d in dobjs:
        pobj = model.ptr(d + 0x0C)
        while pobj:
            pobjs.append(pobj)
            pobj = model.ptr(pobj + 0x04)
    used = indexed_spans(model, pobjs)

    spans = []
    rebase = {}
    for off, (typ, sz) in visited.items():
        if typ == "vertex_blob":
            lo, hi = used[off]
            spans.append((lo, hi))
            if lo != off:
                rebase[off] = lo
        else:
            spans.append((off, off + sz))

    p = plan_payload(model, spans, rebase=rebase, skip=[d + DOBJ_NEXT for d in dobjs])

    anchor = offs[anchor_joint]
    pivot = offs[joints[anchor_joint][1]]
    assert vec3(model, pivot + 0x14) == (0.0, 0.0, 0.0), "pivot is rotated"
    assert joints[joints[anchor_joint][1]][1] == 0, "pivot is not a root child"
    pivot_pos = vec3(model, pivot + 0x2C)

    # The terrain joint sits at the origin, so its vertices are in stage units.
    floor = min(
        y for d in dobjs for _, y, _ in _dobj_positions(model, offs[TERRAIN_JOINT], d)
    )

    return {
        "payload": p,
        "dobjs": [p.remap[d] for d in dobjs],
        "pivot": pivot_pos,
        "anchor_pos": vec3(model, anchor + 0x2C),
        "anchor_rot": vec3(model, anchor + 0x14),
        "floor": floor - pivot_pos[1],
        "rebased": len(rebase),
        "vertex_bytes": sum(hi - lo for lo, hi in used.values()),
    }


def plan_path(stage, root):
    visited = Walker(stage).walk(root, "AnimJoint")
    spans = [(off, off + sz) for off, (_, sz) in visited.items()]
    p = plan_payload(stage, spans)
    return p, p.remap[root]


def _dobj_positions(arc, jobj, dobj):
    """Positions one DObj of `jobj` draws, through a one-DObj view of the joint."""
    view = _OneDObj(arc, jobj, dobj)
    return list(joint_world_positions(view, jobj, mat_identity()))


class _OneDObj:
    """An archive view in which `jobj`'s DObj list holds only `dobj`."""

    def __init__(self, arc, jobj, dobj):
        data = bytearray(arc.data)
        struct.pack_into(">I", data, jobj + 0x10, dobj)
        struct.pack_into(">I", data, dobj + DOBJ_NEXT, 0)
        self.data = bytes(data)
        self.reloc_set = arc.reloc_set - {dobj + DOBJ_NEXT}


def build(m, out_path):
    data = bytearray(MANIFEST_SIZE)
    relocs = []

    def blob(payload, align=4):
        data.extend(b"\0" * ((-len(data)) % align))
        off = len(data)
        data.extend(payload)
        return off

    def donor_payload(at, donor, p):
        donor_off = blob(donor.encode("ascii") + b"\0", 1)
        rng_off = blob(b"".join(struct.pack(">III", *r) for r in p.ranges))
        rel_off = blob(b"".join(struct.pack(">II", *r) for r in p.relocs))
        struct.pack_into(">II", data, at + 0x00, donor_off, p.size)
        struct.pack_into(">II", data, at + 0x08, rng_off, len(p.ranges))
        struct.pack_into(">II", data, at + 0x10, rel_off, len(p.relocs))
        relocs.extend((at + 0x00, at + 0x08, at + 0x10))

    struct.pack_into(">II", data, 0x00, MANIFEST_MAGIC, MANIFEST_VERSION)
    donor_payload(0x08, MODEL_DONOR, m["payload"])
    dobj_off = blob(b"".join(struct.pack(">I", d) for d in m["dobjs"]))
    struct.pack_into(">II", data, 0x20, dobj_off, len(m["dobjs"]))
    relocs.append(0x20)
    struct.pack_into(">3f", data, 0x28, *m["pivot"])
    struct.pack_into(">3f", data, 0x34, *m["anchor_pos"])
    struct.pack_into(">3f", data, 0x40, *m["anchor_rot"])
    struct.pack_into(">ff", data, 0x4C, m["floor"], m["scale"])
    donor_payload(0x54, STAGE_DONOR, m["path"])
    struct.pack_into(">I", data, 0x6C, m["path_root"])

    out = build_archive(data, relocs, [("cannonManifest", 0)])
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(out)
    return len(out)


def plan():
    stage = Archive(os.path.join("iso/files", STAGE_DONOR))
    model = Archive(os.path.join("iso/files", MODEL_DONOR))
    scale, anchor_joint, path = cannon_params(stage)
    m = plan_mesh(model, anchor_joint)
    m["scale"] = scale
    m["path"], m["path_root"] = plan_path(stage, path)
    return m


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=OUTPUT)
    ap.add_argument("--dry-run", action="store_true", help="report without writing")
    args = ap.parse_args(argv[1:])

    m = plan()
    p, f = m["payload"], m["path"]
    print(
        f"{MODEL_DONOR}: {len(m['dobjs'])} DObjs -> {p.size / 1024:.1f} KB payload, "
        f"{len(p.ranges)} ranges, {len(p.relocs)} relocs, "
        f"{m['rebased']} rebased arrays ({m['vertex_bytes'] / 1024:.1f} KB of vertices)"
    )
    print(
        f"{STAGE_DONOR}: flight -> {f.size} B payload, {len(f.ranges)} ranges, "
        f"{len(f.relocs)} relocs"
    )
    print(
        "  pivot ({:.1f}, {:.1f}, {:.1f})  anchor ({:.1f}, {:.1f}, {:.1f}) "
        "rot ({:.3f}, {:.3f}, {:.3f})  floor {:.1f}  scale {:.2f}".format(
            *m["pivot"], *m["anchor_pos"], *m["anchor_rot"], m["floor"], m["scale"]
        )
    )
    if args.dry_run:
        return 0
    size = build(m, args.out)
    print(f"wrote {args.out} ({size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
