#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Report the world-space geometry box of each joint in the title fg scene.

Enumerates ScMenTitleFg_scene_models in GObj_GetJObjIndex order (pre-order
DFS, root = index 0), accumulating each joint's T*R*S world transform, and
prints its own-geometry XY bounding box. Used to place the KARchipelago logo
quad exactly over the vanilla logo joints {10,11,14,16,17}.

Usage: uv run python scripts/hsd/menu_logo_bounds.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.archive import Archive, u32
from hsd.geom_bounds import (jobj_local_mtx, joint_world_positions, mat_identity,
                             mat_mul)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PATH = os.path.join(ROOT, "iso", "files", "MnTitleAll.dat")
SYM = "ScMenTitleFg_scene_models"
TARGET = {10, 11, 14, 16, 17}


def joint_bbox(arc, off, world):
    minv = [1e30, 1e30, 1e30]
    maxv = [-1e30, -1e30, -1e30]
    n = 0
    for wx, wy, wz in joint_world_positions(arc, off, world):
        minv[0] = min(minv[0], wx); maxv[0] = max(maxv[0], wx)
        minv[1] = min(minv[1], wy); maxv[1] = max(maxv[1], wy)
        minv[2] = min(minv[2], wz); maxv[2] = max(maxv[2], wz)
        n += 1
    return minv, maxv, n


def index_order(arc, root):
    """(index, offset, world) per joint in GObj_GetJObjIndex order: pre-order
    DFS, child before siblings, the root's own siblings excluded."""
    order = []

    def walk(off, parent_m, is_top):
        world = mat_mul(parent_m, jobj_local_mtx(arc.data, off))
        order.append((len(order), off, world))
        child = u32(arc.data, off + 0x08) if (off + 0x08) in arc.reloc_set else 0
        if child:
            walk(child, world, False)
        if not is_top:
            nxt = u32(arc.data, off + 0x0C) if (off + 0x0C) in arc.reloc_set else 0
            if nxt:
                walk(nxt, parent_m, False)

    walk(root, mat_identity(), True)
    return order


def main():
    arc = Archive(PATH)
    mg = u32(arc.data, arc.publics[SYM])  # array[0] -> ModelGroup
    root = u32(arc.data, mg)              # ModelGroup -> root JOBJDesc

    ux0 = uy0 = 1e30
    ux1 = uy1 = -1e30
    for idx, off, world in index_order(arc, root):
        minv, maxv, n = joint_bbox(arc, off, world)
        tx, ty = world[0][3], world[1][3]
        mark = " <== LOGO" if idx in TARGET else ""
        if not n:
            print(f"idx {idx:2d} @0x{off:05x}  T=({tx:6.1f},{ty:6.1f})  (no geom){mark}")
            continue
        print(f"idx {idx:2d} @0x{off:05x}  T=({tx:6.1f},{ty:6.1f})  "
              f"X[{minv[0]:6.1f},{maxv[0]:6.1f}] Y[{minv[1]:6.1f},{maxv[1]:6.1f}] "
              f"n={n}{mark}")
        if idx in TARGET:
            ux0 = min(ux0, minv[0]); ux1 = max(ux1, maxv[0])
            uy0 = min(uy0, minv[1]); uy1 = max(uy1, maxv[1])

    print()
    print(f"LOGO union  X[{ux0:.2f}, {ux1:.2f}]  Y[{uy0:.2f}, {uy1:.2f}]")
    print(f"  center = ({(ux0 + ux1) / 2:.2f}, {(uy0 + uy1) / 2:.2f})  "
          f"W={ux1 - ux0:.2f}  H={uy1 - uy0:.2f}  "
          f"aspect={(ux1 - ux0) / (uy1 - uy0):.3f}")


if __name__ == "__main__":
    main()
