# SPDX-License-Identifier: GPL-3.0-only
"""Re-export a machine archive under the public names HSDraw types.

HSDraw decides what a root is from the suffix of its public name - `_joint` is a
JObj, `_matanim_joint` a MatAnimJoint tree, `_figatree` a FigaTree. A machine
archive exports one public, a `vcData` struct named `vcData<Class><Stem>`, which
matches none of them, so the viewer has no way into the model hanging off it and
opening the struct itself as a joint faults its renderer.

This writes a viewer-only copy: the same data section and relocations under a new
public table, naming the model root, the shadow model and each animation-bank
slot the way HSDraw expects. Every joint carries its three LOD meshes at once and
HSDraw draws all of them, so only the DObjs named by one LOD table are kept on
each joint; a pod's always-drawn glow sprite is in every table and survives with
whichever is chosen.

The result is not a disc asset. Pruning leaves the LOD tables indexing a flat
DObj order the tree no longer has, and nothing binds a machine by these names -
`custom_machines` finds one through its descriptor.

Run from the repo root:
    uv run python scripts/hsd/machine_preview.py \\
        mods/ap_star/assets/machines/VcStarAp.dat
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, build_archive, u32
from hsd.builder import Builder, dobjs_of, flat_dobjs, walk_joints

VCDATA_MODELDATA = 0x04
VCDATA_ANIMBANK = 0x18

MODELDATA_MAIN_ROOT = 0x00
MODELDATA_SHADOW_ROOT = 0x28
MODELDATA_MAIN_LOD = {"high": 0x10, "mid": 0x18, "low": 0x20}

# vcAnimationStar: each slot pairs a joint animation with the material animation
# played alongside it, and either may be NULL.
ANIM_SLOTS = (
    ("Moving", 0x00, 0x04),
    ("Boost", 0x18, 0x1C),
    ("Charge", 0x20, 0x24),
    ("Stop", 0x28, 0x2C),
)


def anim_suffix(b, off):
    """A FigaTree opens with two ints and a frame count and holds its per-node
    track table at +0x0c; an AnimJoint opens with child and next pointers."""
    if b.ptr(off + 0x0C) and not b.ptr(off + 0x00) and not b.ptr(off + 0x04):
        return "figatree"
    return "animjoint"


def prune_lod(b, model_data, root, lod):
    """Drop every DObj the named LOD table does not ask for."""
    joints = walk_joints(b, root)
    flat = flat_dobjs(b, joints)

    table = b.ptr(b.ptr(model_data + MODELDATA_MAIN_LOD[lod]) + 0x04)
    count = u32(b.data, table + 0x00)
    indices = b.ptr(table + 0x04)
    keep = {flat[i] for i in b.data[indices : indices + count]}

    dropped = 0
    for off, _ in joints:
        chain = dobjs_of(b, off)
        kept = [d for d in chain if d in keep]
        dropped += len(chain) - len(kept)
        b.set_ptr(off + 0x10, kept[0] if kept else None)
        for i, d in enumerate(kept):
            b.set_ptr(d + 0x04, kept[i + 1] if i + 1 < len(kept) else None)
    return len(flat) - dropped, dropped


def build(src_path, out_path, prefix, lod):
    arc = Archive(src_path)
    named = [p for p in arc.publics if p.startswith("vcData")]
    if len(named) != 1:
        raise SystemExit(f"expected one vcData public, found {list(arc.publics)}")
    public = named[0]
    b = Builder(arc)

    vcdata = arc.publics[public]
    model_data = b.ptr(vcdata + VCDATA_MODELDATA)
    root = b.ptr(model_data + MODELDATA_MAIN_ROOT)
    if not root:
        raise SystemExit(f"{public} has no model root")
    if prefix is None:
        prefix = public.removeprefix("vcData")

    publics = [(f"{prefix}_joint", root)]
    shadow = b.ptr(model_data + MODELDATA_SHADOW_ROOT)
    if shadow:
        publics.append((f"{prefix}Shadow_joint", shadow))

    bank = b.ptr(vcdata + VCDATA_ANIMBANK)
    for name, anim_off, matanim_off in ANIM_SLOTS if bank else ():
        anim = b.ptr(bank + anim_off)
        matanim = b.ptr(bank + matanim_off)
        if anim:
            publics.append((f"{prefix}{name}_{anim_suffix(b, anim)}", anim))
        if matanim:
            publics.append((f"{prefix}{name}_matanim_joint", matanim))

    if lod == "all":
        kept, dropped = len(flat_dobjs(b, walk_joints(b, root))), 0
    else:
        kept, dropped = prune_lod(b, model_data, root, lod)
    print(f"  {len(walk_joints(b, root))} joints, {kept} DObjs kept, {dropped} dropped")
    for name, off in publics:
        print(f"  {name} @ {off:#07x}")

    out = build_archive(b.data, b.relocs, publics, arc.version)
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(out)
    print(f"  wrote {out_path} ({len(out) / 1024:.1f} KB)")


def main(argv):
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument(
        "src", help="machine archive, e.g. mods/ap_star/assets/machines/VcStarAp.dat"
    )
    p.add_argument(
        "out",
        nargs="?",
        default=None,
        help="output archive (default: out/preview/<stem>_preview.dat)",
    )
    p.add_argument(
        "--prefix",
        default=None,
        help="public name stem (default: the vcData public without 'vcData')",
    )
    p.add_argument(
        "--lod",
        choices=("high", "mid", "low", "all"),
        default="high",
        help="which LOD's meshes to keep; 'all' leaves every DObj drawn at once",
    )
    args = p.parse_args(argv[1:])

    out = args.out
    if out is None:
        stem = os.path.splitext(os.path.basename(args.src))[0]
        out = os.path.join("out", "preview", f"{stem}_preview.dat")

    print(f"Building {out} from {args.src}:")
    build(args.src, out, args.prefix, args.lod)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
