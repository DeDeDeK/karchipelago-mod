#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Spatial queries over a stage, in world units (the units a live machine
position is in).

    uv run python scripts/hsd/stage.py summary City1
    uv run python scripts/hsd/stage.py ground City1 120 -300
    uv run python scripts/hsd/stage.py at City1 191.8 140 291.2
    uv run python scripts/hsd/stage.py find City1 volcano
    uv run python scripts/hsd/stage.py dump City1 positions --list start
    uv run python scripts/hsd/stage.py spots City1 --near-landmark lighthouse
    uv run python scripts/hsd/stage.py sweep City1 --points 0,60,0 100,90,-200
    uv run --with pillow python scripts/hsd/stage.py render City1 --overlay start
    uv run --with pillow python scripts/hsd/stage.py section City1 -700 0 600 0
    uv run --with dolphin-memory-engine python scripts/hsd/stage.py live

A stage is named by its `Gr*.dat` stem with or without the `Gr` (City1,
GrCity1), or by an alias from `stages`. Headings are degrees from +Z toward +X.
The top-down pictures put +X right and +Z down.
"""

import argparse
import csv
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd import world
from hsd.format import COLL_JOINT_KINDS, SPLINE_TYPES
from hsd.world import (
    CAT_CEILING,
    CAT_NAMES,
    CAT_WALL,
    Raster,
    Stage,
    city_event_name,
    ground_type_name,
    heading_deg,
    load_landmarks,
)

SPLINE_FAMILIES = ["course", "cpu", "conveyor", "rail", "heavy"]

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "out", "stage")


def shown(path):
    """`path` relative to the working directory when it is under it."""
    rel = os.path.relpath(path)
    return path if rel.startswith("..") else rel


def fmt(p):
    return "(" + ", ".join(f"{v:.1f}" for v in p) + ")"


def parse_point(text, n=3):
    vals = [float(v) for v in text.replace(" ", "").split(",")]
    if len(vals) != n:
        raise argparse.ArgumentTypeError(f"expected {n} comma-separated numbers, got {text!r}")
    return tuple(vals)


def load(name):
    try:
        return Stage(world.stage_file(name))
    except FileNotFoundError as e:
        sys.exit(f"{e}; `stages` lists the known names")


def all_positions(stage, lists=None):
    for name, plist in stage.positions.items():
        if lists and name not in lists:
            continue
        yield from plist


def nearest_positions(stage, p, n=3):
    pts = sorted(all_positions(stage), key=lambda q: _dist(q.pos, p))
    return pts[:n]


def _dist(a, b):
    return math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3)))


def position_label(stage, q):
    label = f"{q.list} {q.index}"
    if stage.stem == "GrCity1" and q.list == "event":
        name = city_event_name(q.index)
        if name:
            label += f" ({name})"
    return label


def prop_center(p):
    if p.lo is None:
        return p.pos
    return ((p.lo[0] + p.hi[0]) / 2, p.lo[1], (p.lo[2] + p.hi[2]) / 2)


def polyline_distance(pts, p):
    """(distance, index of the nearest segment) from p to a polyline."""
    best = (math.inf, 0)
    for k, (a, b) in enumerate(zip(pts, pts[1:])):
        ab = [b[i] - a[i] for i in range(3)]
        ap = [p[i] - a[i] for i in range(3)]
        den = sum(v * v for v in ab)
        u = max(0.0, min(1.0, sum(ab[i] * ap[i] for i in range(3)) / den)) if den else 0.0
        q = [a[i] + ab[i] * u for i in range(3)]
        dist = _dist(q, p)
        if dist < best[0]:
            best = (dist, k)
    return best


def find_landmark(stage, text):
    hits = [lm for lm in load_landmarks(stage.stem) if lm.matches(text)]
    if not hits:
        sys.exit(f"no landmark on {stage.stem} matches {text!r} (see `find {stage.stem} ''`)")
    exact = [lm for lm in hits if any(n.lower() == text.lower() for n in lm.names())]
    return (exact or hits)[0]


def cmd_summary(args):
    s = load(args.stage)
    r = Raster(s, 8.0)
    land = [r.center(c) for c, ok in enumerate(r.land) if ok]
    cats = {}
    for t in s.tris:
        cats[t.cat] = cats.get(t.cat, 0) + 1
    print(f"{s.stem}  scale {s.scale:g}  gravity {s.gravity:g}")
    print(f"  out-of-bounds box {fmt(s.oob_min)} .. {fmt(s.oob_max)} (world, unscaled)")
    if land:
        xs, zs = [p[0] for p in land], [p[1] for p in land]
        ys = [r.top_y[c] for c, ok in enumerate(r.land) if ok]
        print(f"  land x {min(xs):.0f}..{max(xs):.0f}  z {min(zs):.0f}..{max(zs):.0f}"
              f"  y {min(ys):.0f}..{max(ys):.0f}  area {len(land) * r.res * r.res:.0f}")
    sea = sum(1 for c in r.top_tri if c >= 0 and s.tris[c].gtype == 29)
    if sea:
        print(f"  sea covers {sea * 100.0 / (r.nx * r.nz):.0f}% of the box from above")
    stacked = sum(1 for c, ok in enumerate(r.land) if ok and r.layers[c] > 1)
    if land:
        print(f"  {stacked * 100.0 / len(land):.0f}% of the land has another floor under its top")
    lid = [t for t in s.tris if t.gtype == 30 and t.cat == CAT_CEILING]
    if lid:
        print(f"  barrier lid at y {max(max(v[1] for v in (t.v0, t.v1, t.v2)) for t in lid):.1f}")
    print(f"  triangles {len(s.tris)} ("
          + ", ".join(f"{CAT_NAMES.get(k, k)} {v}" for k, v in sorted(cats.items())) + ")")
    gt = {}
    for t in s.tris:
        gt[t.gtype] = gt.get(t.gtype, 0) + 1
    print("  ground types " + ", ".join(f"{ground_type_name(k, number=True)} {v}" for k, v in sorted(gt.items())))
    kinds = {}
    for z in s.zones:
        kinds[z.name] = kinds.get(z.name, 0) + 1
    print(f"  zones {len(s.zones)}: " + ", ".join(f"{k} {v}" for k, v in sorted(kinds.items(), key=lambda kv: -kv[1])))
    print("  positions " + ", ".join(f"{k} {len(v)}" for k, v in s.positions.items()))
    lms = load_landmarks(s.stem)
    print(f"  landmarks {len(lms)}")


def cmd_ground(args):
    s = load(args.stage)
    col = s.column(args.x, args.z)
    print(f"{s.stem} ({args.x:g}, {args.z:g}): {len(col)} surface(s), highest first")
    for y, i in col:
        t = s.tris[i]
        face = "up" if t.normal[1] > 0 else "down"
        print(f"  y {y:8.1f}  {CAT_NAMES.get(t.cat, t.cat):7s} faces {face:4s} "
              f"{ground_type_name(t.gtype, number=True):18s} joint {t.bone} "
              f"({COLL_JOINT_KINDS.get(t.jkind, t.jkind).lower()})  tri {i}")
    g = s.ground(args.x, args.z)
    if g:
        print(f"  top standable surface y {g[0]:.1f} ({s.tri_desc(g[1])})")


def describe_point(s, p, raster=None, landmarks=None):
    lines = []
    landmarks = load_landmarks(s.stem) if landmarks is None else landmarks
    near = sorted(landmarks, key=lambda lm: lm.distance(p))
    inside = [lm for lm in near if lm.distance(p) == 0.0]
    for lm in inside:
        lines.append(f"  inside   {lm.name} ({lm.shape}, {lm.extent()})")
    for lm in [lm for lm in near if lm not in inside][:3]:
        c = lm.center
        lines.append(f"  near     {lm.name}: {lm.distance(p):.0f} away "
                     f"(dx {c[0] - p[0]:+.0f}, dy {c[1] - p[1]:+.0f}, dz {c[2] - p[2]:+.0f})")
    g = s.ground(p[0], p[2], below=p[1])
    if g:
        lines.append(f"  floor    y {g[0]:.1f}, {p[1] - g[0]:.1f} below ({s.tri_desc(g[1])})")
    else:
        lines.append("  floor    nothing below")
    c = s.ceiling(*p)
    if c:
        lines.append(f"  ceiling  y {c[0]:.1f}, {c[0] - p[1]:.1f} above ({s.tri_desc(c[1])})")
    others = [y for y, i in s.floors(p[0], p[2]) if y > p[1] + 1e-3]
    if others:
        lines.append(f"  above    {len(others)} more floor(s), the highest at y {max(others):.1f}")
    zones = s.zones_at(p)
    if zones:
        lines.append("  zones    " + ", ".join(f"{z.name} #{z.index}" for z in zones))
    if raster is not None:
        c = raster.index(p[0], p[2])
        if c >= 0:
            if raster.land[c]:
                sea = raster.distance([not v for v in raster.land])
                lines.append(f"  land     {sea[c]:.0f} from the sea")
            elif raster.top_tri[c] >= 0:
                lines.append("  sea      over open water")
    if not s.in_oob(p):
        lines.append("  OUTSIDE the out-of-bounds box")
    props = s.props()
    if props:
        inside = [q for q in props if q.lo and all(q.lo[k] - 1 <= p[k] <= q.hi[k] + 1 for k in range(3))]
        for q in inside:
            lines.append(f"  prop     inside {q.name} {q.instance} (slot {q.data_idx}, joint {q.joint})")
        if not inside:
            q = min(props, key=lambda q: _dist(prop_center(q), p))
            lines.append(f"  prop     {q.name} {q.instance} (slot {q.data_idx}, joint {q.joint}) at "
                         f"{_dist(prop_center(q), p):.0f} {fmt(prop_center(q))}")
    rails = s.splines["rails"]
    if rails:
        dist, r = min((polyline_distance(s.splines["rail"][r.path].polyline(), p)[0], r)
                      for r in rails if 0 <= r.path < len(s.splines["rail"]))
        lines.append(f"  rail     {r.index} at {dist:.0f}")
    for q in nearest_positions(s, p):
        lines.append(f"  position {position_label(s, q)} at {_dist(q.pos, p):.0f} {fmt(q.pos)}")
    return lines


def cmd_at(args):
    s = load(args.stage)
    if args.landmark:
        lm = find_landmark(s, args.landmark)
        p = lm.center
        print(f"{s.stem} {lm.name} {fmt(p)}")
    else:
        if args.y is None:
            g = s.ground(args.x, args.z)
            p = (args.x, (g[0] if g else 0.0) + 1.0, args.z)
        else:
            p = (args.x, args.y, args.z)
        print(f"{s.stem} {fmt(p)}")
    print("\n".join(describe_point(s, p, Raster(s, 4.0))))


def cmd_find(args):
    s_stem = world.stage_file(args.stage) if args.stage != "all" else None
    stem = os.path.splitext(os.path.basename(s_stem))[0] if s_stem else None
    lms = [lm for lm in load_landmarks(stem) if lm.matches(args.text)]
    if args.near:
        lms.sort(key=lambda lm: lm.distance(args.near))
    for lm in lms:
        extra = f"  [{', '.join(lm.aliases)}]" if lm.aliases else ""
        dist = f"  {lm.distance(args.near):.0f} away" if args.near else ""
        print(f"{lm.stage:9s} {lm.name:28s} {fmt(lm.center):26s} {lm.shape} {lm.extent()}"
              f"  ({lm.source}){dist}{extra}")
        if lm.note and args.verbose:
            print(f"          {lm.note}")
    if not lms:
        print("no match")


def cmd_dump(args):
    s = load(args.stage)
    rows = []
    if args.what == "positions":
        for q in all_positions(s, args.list):
            row = dict(list=q.list, index=q.index, x=q.pos[0], y=q.pos[1], z=q.pos[2],
                       heading=heading_deg(q.forward[0], q.forward[2]))
            if s.stem == "GrCity1":
                row["use"] = (city_event_name(q.index) if q.list == "event" else None) or ""
            rows.append(row)
    elif args.what == "props":
        for q in s.props():
            c = prop_center(q)
            rows.append(dict(name=q.name, kind=q.kind, slot=q.data_idx, n=q.instance, joint=q.joint,
                             x=c[0], y=c[1], z=c[2], yaw=q.yaw,
                             sx=(q.hi[0] - q.lo[0]) if q.lo else 0.0,
                             sy=(q.hi[1] - q.lo[1]) if q.lo else 0.0,
                             sz=(q.hi[2] - q.lo[2]) if q.lo else 0.0))
    elif args.what == "rails":
        rail = s.splines["rail"]
        for r in s.splines["rails"]:
            sp = rail[r.path] if 0 <= r.path < len(rail) else None
            a, b = sp.ends() if sp else ((0, 0, 0), (0, 0, 0))
            rows.append(dict(rail=r.index, spline=r.path, length=sp.length if sp else 0.0,
                             x0=a[0], y0=a[1], z0=a[2], x1=b[0], y1=b[1], z1=b[2], radius=r.radius,
                             next=",".join(str(n) for n in r.next if n >= 0) or "-",
                             prev=r.prev, twin=r.twin))
    elif args.what == "splines":
        fams = args.family or SPLINE_FAMILIES
        for fam in fams:
            items = s.splines[fam]
            for i, sp in enumerate(items):
                pairs = [("left", sp.left), ("right", sp.right)] if fam == "cpu" else [("", sp)]
                for side, one in pairs:
                    if one is None:
                        continue
                    a, b = one.ends()
                    rows.append(dict(family=fam + (f" {side}" if side else ""), index=i,
                                     type=SPLINE_TYPES.get(one.type, one.type), points=one.n,
                                     length=one.length, x0=a[0], y0=a[1], z0=a[2], x1=b[0], y1=b[1],
                                     z1=b[2]))
    elif args.what == "zones":
        for z in s.zones:
            if args.kind and z.name.lower() not in [k.lower() for k in args.kind]:
                continue
            c = [(z.lo[k] + z.hi[k]) / 2 for k in range(3)]
            rows.append(dict(index=z.index, kind=z.kind, name=z.name, param=z.param_index,
                             bone=z.bone, x=c[0], y=c[1], z=c[2], sx=z.hi[0] - z.lo[0],
                             sy=z.hi[1] - z.lo[1], sz=z.hi[2] - z.lo[2]))
    elif args.what == "joints":
        for j, cj in enumerate(s.coll_joints):
            ts = s.tris[cj.face_start:cj.face_start + cj.face_num]
            if not ts:
                continue
            pts = [v for t in ts for v in (t.v0, t.v1, t.v2)]
            lo = [min(p[k] for p in pts) for k in range(3)]
            hi = [max(p[k] for p in pts) for k in range(3)]
            rows.append(dict(joint=j, bone=cj.bone, kind=COLL_JOINT_KINDS.get(cj.kind, cj.kind).lower(),
                             moving="yes" if cj.moving else "", tris=cj.face_num, x=(lo[0] + hi[0]) / 2, y=lo[1], z=(lo[2] + hi[2]) / 2,
                             sx=hi[0] - lo[0], sy=hi[1] - lo[1], sz=hi[2] - lo[2]))
    elif args.what == "landmarks":
        for lm in load_landmarks(s.stem):
            rows.append(dict(name=lm.name, shape=lm.shape, x=lm.center[0], y=lm.center[1],
                             z=lm.center[2], size=lm.extent(), source=lm.source))
    emit(rows, args.csv)


def emit(rows, as_csv):
    if not rows:
        print("nothing to list")
        return
    keys = list(rows[0])
    if as_csv:
        w = csv.DictWriter(sys.stdout, fieldnames=keys, lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow({k: (f"{v:.2f}" if isinstance(v, float) else v) for k, v in r.items()})
        return
    cells = [[(f"{r[k]:.1f}" if isinstance(r[k], float) else str(r[k])) for k in keys] for r in rows]
    widths = [max(len(k), *(len(c[i]) for c in cells)) for i, k in enumerate(keys)]
    print("  ".join(k.rjust(w) for k, w in zip(keys, widths)))
    for c in cells:
        print("  ".join(v.rjust(w) for v, w in zip(c, widths)))


def cmd_sweep(args):
    s = load(args.stage)
    pts = list(args.points or [])
    if args.path_file:
        pts += read_path(args.path_file)
    if len(pts) < 2:
        sys.exit("sweep needs at least two points (--points or --path-file)")
    if args.step:
        pts = densify(pts, args.step)
    worst = None
    hit_any = None
    for k, p in enumerate(pts):
        g = s.ground(p[0], p[2], below=p[1])
        c = s.ceiling(*p)
        clear = p[1] - g[0] if g else math.inf
        if worst is None or clear < worst[0]:
            worst = (clear, k, p, g)
        if k and hit_any is None:
            h = s.raycast(pts[k - 1], p, two_sided=True)
            if h:
                hit_any = (k, h)
        if args.verbose:
            print(f"  {k:4d} {fmt(p)}  above ground {clear:7.1f}"
                  + (f"  ceiling {c[0] - p[1]:.1f} above" if c else ""))
    print(f"{s.stem}: {len(pts)} points, {sum(_dist(a, b) for a, b in zip(pts, pts[1:])):.0f} long")
    if worst:
        clear, k, p, g = worst
        desc = s.tri_desc(g[1]) if g else "nothing below"
        print(f"  lowest   point {k} {fmt(p)}, {clear:.1f} above ground ({desc})")
    if hit_any:
        k, (_, hp, i) = hit_any
        print(f"  BLOCKED  segment {k - 1}->{k} hits {fmt(hp)} ({s.tri_desc(i)}), tri {i}")
    else:
        print("  clear    no segment crosses collision")


def read_path(path):
    out = []
    with open(path, newline="") as f:
        for row in csv.reader(f):
            try:
                out.append(tuple(float(v) for v in row[:3]))
            except ValueError:
                continue
    return out


def densify(pts, step):
    out = [pts[0]]
    for a, b in zip(pts, pts[1:]):
        n = max(1, math.ceil(_dist(a, b) / step))
        out += [tuple(a[k] + (b[k] - a[k]) * i / n for k in range(3)) for i in range(1, n + 1)]
    return out


def cmd_spots(args):
    s = load(args.stage)
    if args.near_landmark:
        lm = find_landmark(s, args.near_landmark)
        target = (lm.center[0], lm.center[2])
    elif args.near:
        target = args.near
    else:
        target = None
    res = args.res
    bounds = None
    if target:
        rad = args.radius + args.wall_gap + res * 2
        bounds = (target[0] - rad, target[1] - rad, target[0] + rad, target[1] + rad)
    r = Raster(s, res, bounds=bounds)
    n = r.nx * r.nz
    wall = [False] * n
    bx0, bz0 = r.x0 - res, r.z0 - res
    bx1, bz1 = r.x0 + r.nx * res, r.z0 + r.nz * res
    for t in s.tris:
        if t.gtype == 30 or (t.cat != CAT_WALL and abs(t.normal[1]) > 0.6):
            continue
        if max(t.v0[0], t.v1[0], t.v2[0]) < bx0 or min(t.v0[0], t.v1[0], t.v2[0]) > bx1 \
                or max(t.v0[2], t.v1[2], t.v2[2]) < bz0 or min(t.v0[2], t.v1[2], t.v2[2]) > bz1:
            continue
        ylo = min(t.v0[1], t.v1[1], t.v2[1])
        yhi = max(t.v0[1], t.v1[1], t.v2[1])
        for p in _tri_samples(t, res * 0.5):
            c = r.index(p[0], p[2])
            if c >= 0 and r.top_y[c] is not None and yhi > r.top_y[c] + args.step_height \
                    and ylo < r.top_y[c] + args.clear:
                wall[c] = True
    wall_d = r.distance(wall)
    flat_cos = math.cos(math.radians(args.max_slope))
    cands = []
    k = max(1, round(args.footprint / res))
    for c in range(n):
        if not r.land[c] or wall_d[c] < args.wall_gap:
            continue
        x, z = r.center(c)
        if target and math.hypot(x - target[0], z - target[1]) > args.radius:
            continue
        t = s.tris[r.top_tri[c]]
        if t.normal[1] < flat_cos or t.gtype in args.skip_type:
            continue
        y = r.top_y[c]
        i, j = c % r.nx, c // r.nx
        ok = True
        for dj in (-k, 0, k):
            for di in (-k, 0, k):
                ii, jj = i + di, j + dj
                if not (0 <= ii < r.nx and 0 <= jj < r.nz):
                    ok = False
                    continue
                yy = r.top_y[jj * r.nx + ii]
                if yy is None or abs(yy - y) > args.step_height:
                    ok = False
        if not ok:
            continue
        ceil = s.ceiling(x, y + 0.5, z)
        if ceil and ceil[0] - y < args.clear:
            continue
        if args.no_zones and s.zones_at((x, y + 2.0, z)):
            continue
        score = math.hypot(x - target[0], z - target[1]) if target else -wall_d[c]
        cands.append((score, x, y, z, wall_d[c], t, ceil))
    cands.sort(key=lambda q: q[0])
    picked = []
    for q in cands:
        if all(math.hypot(q[1] - p[1], q[3] - p[3]) >= args.min_gap for p in picked):
            picked.append(q)
            if len(picked) >= args.count:
                break
    where = f"within {args.radius:g} of ({target[0]:g}, {target[1]:g})" if target else "anywhere"
    print(f"{s.stem}: {len(cands)} open cells {where}; flat to {args.max_slope:g} deg, "
          f"{args.wall_gap:g} from walls, {args.clear:g} headroom")
    for q in picked:
        _, x, y, z, wd, t, ceil = q
        head = f"{ceil[0] - y:.0f}" if ceil else "open sky"
        print(f"  ({x:7.1f}, {y:6.1f}, {z:7.1f})  {ground_type_name(t.gtype):14s} "
              f"walls {wd:4.0f} away  headroom {head}")


def _tri_samples(t, step):
    a, b, c = t.v0, t.v1, t.v2
    n = max(1, int(max(_dist(a, b), _dist(b, c), _dist(c, a)) / step))
    for i in range(n + 1):
        for j in range(n + 1 - i):
            u, v = i / n, j / n
            w = 1.0 - u - v
            yield tuple(a[k] * w + b[k] * u + c[k] * v for k in range(3))


def cmd_render(args):
    from hsd import stage_render as R

    s = load(args.stage)
    if args.center:
        cx, cz = args.center
        rad = args.radius
        window = (cx - rad, cz - rad, cx + rad, cz + rad)
    elif args.window:
        window = tuple(args.window)
    elif args.landmark:
        lm = find_landmark(s, args.landmark)
        rad = args.radius
        window = (lm.center[0] - rad, lm.center[2] - rad, lm.center[0] + rad, lm.center[2] + rad)
    else:
        window = R.land_window(s, Raster(s, 8.0, max_y=args.max_y))
    span = max(window[2] - window[0], window[3] - window[1])
    res = args.res or (4.0 if span > 900 else 2.0 if span > 300 else 1.0 if span > 120 else 0.5)
    raster = Raster(s, res, bounds=window, max_y=args.max_y)
    view = R.View(window, args.size / span)
    img, legend = R.base_image(s, raster, view, args.layer)
    cv = R.Canvas(img, view)
    cv.grid(args.grid)
    keys = []
    if not args.no_barrier:
        segs = R.barrier_segments(s, view)
        cv.segments(segs, R.BARRIER)
        if segs:
            keys.append(("barrier walls (30)", R.BARRIER))
    over = set(args.overlay or [])
    if "walls" in over:
        cv.segments(R.wall_segments(s, view), (20, 20, 20))
    if "zones" in over or args.zone_kind:
        kinds = [k.lower() for k in (args.zone_kind or [])]
        for z in s.zones:
            if kinds and z.name.lower() not in kinds:
                continue
            if z.name == "LightArea" and not kinds:
                continue
            cv.rect(z.lo, z.hi, (0, 255, 255), f"{z.name} {z.index}")
    if "joints" in over:
        for cj in s.coll_joints:
            bone, kind = cj.bone, cj.kind
            ts = s.tris[cj.face_start:cj.face_start + cj.face_num]
            if not ts or (kind == 0 and bone == 0):
                continue
            pts = [v for t in ts for v in (t.v0, t.v1, t.v2)]
            cx = (min(p[0] for p in pts) + max(p[0] for p in pts)) / 2
            cz = (min(p[2] for p in pts) + max(p[2] for p in pts)) / 2
            cv.marker(cx, cz, (255, 255, 255) if kind == 3 else (0, 255, 0), f"j{bone}",
                      shape="square", size=3)
    sp = None
    if over & {"course", "rails", "cpu", "conveyor", "heavy"}:
        sp = s.splines
    if "cpu" in over:
        for pair in sp["cpu"]:
            for one in (pair.left, pair.right):
                if one:
                    cv.polyline([(p[0], p[2]) for p in one.polyline()], R.CPU, width=1)
        keys.append(("CPU road edges", R.CPU))
    if "course" in over:
        for i, piece in enumerate(sp["course"]):
            main = i in sp["route"]
            cv.polyline([(p[0], p[2]) for p in piece.polyline()], R.COURSE if main else R.BRANCH,
                        width=3 if main else 2, label=f"c{i}" if args.labels else None)
        keys += [("course main route", R.COURSE), ("course branch", R.BRANCH)]
        if sp["route"]:
            a = sp["course"][sp["route"][0]].point(0.0)
            cv.marker(a[0], a[2], (255, 255, 255), "start", shape="square", size=5, font=cv.font)
    for fam, col in (("conveyor", (0, 255, 160)), ("heavy", (255, 120, 0))):
        if fam in over:
            for i, one in enumerate(sp[fam]):
                cv.polyline([(p[0], p[2]) for p in one.polyline()], col, width=2,
                            label=f"{fam[0]}{i}" if args.labels else None)
    if "rails" in over:
        for r in sp["rails"]:
            if 0 <= r.path < len(sp["rail"]):
                pts = sp["rail"][r.path].polyline()
                cv.polyline([(p[0], p[2]) for p in pts], R.RAIL, width=2, label=f"r{r.index}")
        keys.append(("rails", R.RAIL))
    if "props" in over:
        for q in s.props():
            c = prop_center(q)
            cv.marker(c[0], c[2], (255, 255, 255), q.name if args.labels else None, shape="square", size=3)
    for name in over & set(s.positions):
        col = R.OVERLAY_COLORS.get(name, (255, 255, 255))
        for q in s.positions[name]:
            cv.marker(q.pos[0], q.pos[2], col, f"{name[0]}{q.index}" if args.labels else None, size=3)
    if not args.no_landmarks:
        for lm in load_landmarks(s.stem):
            x, _, z = lm.center
            if lm.shape in ("sphere", "cylinder") and 6 < lm.r * view.ppu < view.w / 4:
                cv.circle(x, z, lm.r, (255, 255, 255))
            cv.marker(x, z, (255, 255, 255), lm.name, shape="x", font=cv.font)
    for m in args.mark or []:
        parts = m.split(",")
        cv.marker(float(parts[0]), float(parts[1]), (255, 40, 40),
                  ",".join(parts[2:]) or None, shape="x", size=6, font=cv.font)
    paths = []
    if args.path:
        paths.append(args.path)
    for f in args.path_file or []:
        paths.append(read_path(f))
    for pts in paths:
        cv.polyline([(p[0], p[2]) for p in pts], (255, 220, 0), width=3)
    title = f"{s.stem} {args.layer}" + (f" below y {args.max_y:g}" if args.max_y is not None else "")
    cv.legend(title + "  (+x right, +z down)", legend + keys)
    out = args.out or os.path.join(OUT_DIR, f"{s.stem}-{args.layer}.png")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    img.save(out)
    print(f"wrote {shown(out)} ({img.width}x{img.height}, window "
          f"x {window[0]:.0f}..{window[2]:.0f} z {window[1]:.0f}..{window[3]:.0f})")


def cmd_section(args):
    from hsd import stage_render as R

    s = load(args.stage)
    path = list(args.path or [])
    if args.path_file:
        path += read_path(args.path_file)
    out = args.out or os.path.join(OUT_DIR, f"{s.stem}-section.png")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    R.section(s, (args.x0, args.z0), (args.x1, args.z1), out, path=path or None)
    print(f"wrote {shown(out)}")


def cmd_course(args):
    s = load(args.stage)
    sp = s.splines
    if not sp["course"]:
        sys.exit(f"{s.stem} has no course splines")
    laps = default_laps(s.stem)
    print(f"{s.stem} ({s.label}): lap {s.lap_length():.1f} world units over {len(sp['route'])} pieces"
          + (", loops" if sp["loop"] else "") + (f", {laps} laps by default" if laps else ""))
    start = sp["course"][sp["route"][0]].point(0.0) if sp["route"] else None
    if start:
        print(f"  starts at {fmt(start)}")
    done = 0.0
    for i in sp["route"]:
        piece = sp["course"][i]
        a, b = piece.ends()
        print(f"  piece {i:2d}  {piece.length:8.1f}  at {done:8.1f}  {fmt(a)} -> {fmt(b)}")
        done += piece.length
    alt = [i for i in range(len(sp["course"])) if i not in sp["route"]]
    if alt:
        print("  branches: " + ", ".join(f"{i} ({sp['course'][i].length:.0f})" for i in alt))
    print(f"  {len(sp['cpu'])} CPU road-edge pairs, {len(s.positions.get('start', []))} start positions")


def default_laps(stem):
    """Laps a race on this ground runs by default: Stage.dat stData rows are
    0x58 bytes, ground kind at +0x00 and laps at +0x34; the first row naming
    the ground wins."""
    from hsd.archive import Archive, s32

    gk = next((row[1] for row in world.STAGES if row[0] == stem), None)
    path = os.path.join(world.ISO_FILES, "Stage.dat")
    if gk is None or not os.path.exists(path):
        return None
    a = Archive(path)
    base = a.publics.get("stData")
    if base is None:
        return None
    if base in a.reloc_set:
        base = a.ptr(base)
    rows = (len(a.data) - base) // 0x58
    for k in range(min(rows, 64)):
        r = base + 0x58 * k
        if s32(a.data, r) == gk:
            return s32(a.data, r + 0x34)
    return None


def cmd_live(args):
    from hsd import stage_live

    stage_live.main(args)


def cmd_stages(args):
    rows = []
    for stem, gk, label, aliases in world.STAGES:
        row = dict(file=stem, ground_kind=gk, name=label)
        if args.detail or args.csv:
            s = load(stem)
            r = Raster(s, 8.0)
            land = [r.center(c) for c, ok in enumerate(r.land) if ok]
            sp = s.splines
            row.update(scale=f"{s.scale:g}",
                       oob=f"{fmt(s.oob_min)}..{fmt(s.oob_max)}",
                       land_x=f"{min(p[0] for p in land):.0f}..{max(p[0] for p in land):.0f}" if land else "",
                       land_z=f"{min(p[1] for p in land):.0f}..{max(p[1] for p in land):.0f}" if land else "",
                       route=round(s.lap_length(), 1) if sp["route"] else "",
                       loop="yes" if sp["loop"] else "no",
                       laps=default_laps(stem) or "",
                       tris=len(s.tris), zones=len(s.zones),
                       starts=len(s.positions.get("start", [])))
        else:
            row["aliases"] = ", ".join(aliases)
        rows.append(row)
    emit(rows, args.csv)


def cmd_ground_types(args):
    """GrCommon.dat's ground table: one 0x1C-byte row per ground type, read
    through grGetTractionFromTriangleID (+0x00), the rough-ground speed
    multipliers (+0x08, +0x0C), grGetDustFromGroundType (+0x10) and the sound
    index into VcCommon.dat's machine sound rows (+0x14)."""
    from hsd.archive import Archive, f32, s32

    a = Archive(os.path.join(world.ISO_FILES, "GrCommon.dat"))
    table = a.ptr(a.publics["grDataCommon"])
    rows = []
    for g in range(len(world.GROUND_TYPE_NAMES)):
        r = table + 0x1C * g
        rows.append(dict(type=g, name=ground_type_name(g), traction=round(f32(a.data, r), 3),
                         x04=round(f32(a.data, r + 0x04), 3), rough_a=round(f32(a.data, r + 0x08), 3),
                         rough_b=round(f32(a.data, r + 0x0C), 3), dust=s32(a.data, r + 0x10),
                         sound=s32(a.data, r + 0x14), bob=s32(a.data, r + 0x18)))
    emit(rows, args.csv)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("summary", help="scale, extents, sea, layers, counts")
    p.add_argument("stage")
    p.set_defaults(fn=cmd_summary)

    p = sub.add_parser("ground", help="every surface on the vertical line at x z")
    p.add_argument("stage")
    p.add_argument("x", type=float)
    p.add_argument("z", type=float)
    p.set_defaults(fn=cmd_ground)

    p = sub.add_parser("at", help="what is at a point: landmarks, floor, ceiling, zones, positions")
    p.add_argument("stage")
    p.add_argument("x", type=float, nargs="?")
    p.add_argument("y", type=float, nargs="?")
    p.add_argument("z", type=float, nargs="?")
    p.add_argument("--landmark", help="describe a landmark's center instead")
    p.set_defaults(fn=cmd_at)

    p = sub.add_parser("find", help="landmarks whose name or alias contains TEXT ('' for all)")
    p.add_argument("stage", help="a stage, or 'all'")
    p.add_argument("text")
    p.add_argument("--near", type=parse_point, help="x,y,z to sort by distance from")
    p.add_argument("-v", "--verbose", action="store_true", help="print notes")
    p.set_defaults(fn=cmd_find)

    p = sub.add_parser("dump", help="world-space tables")
    p.add_argument("stage")
    p.add_argument("what", choices=["positions", "zones", "joints", "props", "rails", "splines",
                                    "landmarks"])
    p.add_argument("--family", action="append", choices=SPLINE_FAMILIES, help="spline families to list")
    p.add_argument("--list", action="append", help="position list(s) to keep (start, item, ...)")
    p.add_argument("--kind", action="append", help="zone kind name(s) to keep")
    p.add_argument("--csv", action="store_true")
    p.set_defaults(fn=cmd_dump)

    p = sub.add_parser("sweep", help="clearance along a path and the first collision it crosses")
    p.add_argument("stage")
    p.add_argument("--points", type=parse_point, nargs="+")
    p.add_argument("--path-file", help="CSV of x,y,z rows")
    p.add_argument("--step", type=float, default=4.0, help="resample spacing (0 to keep points)")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_sweep)

    p = sub.add_parser("spots", help="flat, open, dry places to put something")
    p.add_argument("stage")
    p.add_argument("--near", type=lambda t: parse_point(t, 2), help="x,z to search around")
    p.add_argument("--near-landmark")
    p.add_argument("--radius", type=float, default=150.0)
    p.add_argument("--count", type=int, default=8)
    p.add_argument("--min-gap", type=float, default=30.0, help="spacing between results")
    p.add_argument("--footprint", type=float, default=12.0, help="half-width that must be flat")
    p.add_argument("--max-slope", type=float, default=12.0, help="degrees")
    p.add_argument("--step-height", type=float, default=2.0, help="height change still called flat")
    p.add_argument("--wall-gap", type=float, default=20.0)
    p.add_argument("--clear", type=float, default=40.0, help="headroom required")
    p.add_argument("--skip-type", type=int, action="append", default=[29, 30],
                   help="ground types to avoid (sea and barrier always)")
    p.add_argument("--no-zones", action="store_true", help="skip cells inside any zone")
    p.add_argument("--res", type=float, default=2.0)
    p.set_defaults(fn=cmd_spots)

    p = sub.add_parser("render", help="top-down PNG (needs pillow)")
    p.add_argument("stage")
    p.add_argument("--layer", choices=["height", "kind", "layers"], default="height")
    p.add_argument("--center", type=lambda t: parse_point(t, 2))
    p.add_argument("--landmark", help="center on a landmark")
    p.add_argument("--radius", type=float, default=200.0)
    p.add_argument("--window", type=float, nargs=4, metavar=("X0", "Z0", "X1", "Z1"))
    p.add_argument("--max-y", type=float, help="drop surfaces above this height")
    p.add_argument("--overlay", action="append",
                   help="position list (start, item, event, vehicle, ...), zones, joints, walls, "
                        "props, course, rails, cpu, conveyor or heavy")
    p.add_argument("--zone-kind", action="append", help="draw only these zone kinds")
    p.add_argument("--labels", action="store_true", help="label position markers")
    p.add_argument("--mark", action="append", help="x,z[,label]")
    p.add_argument("--path", type=parse_point, nargs="+", help="x,y,z points to draw")
    p.add_argument("--path-file", action="append", help="CSV of x,y,z rows (repeatable)")
    p.add_argument("--no-landmarks", action="store_true")
    p.add_argument("--no-barrier", action="store_true")
    p.add_argument("--grid", type=float)
    p.add_argument("--res", type=float)
    p.add_argument("--size", type=int, default=1100, help="pixels across the longer side")
    p.add_argument("-o", "--out")
    p.set_defaults(fn=cmd_render)

    p = sub.add_parser("section", help="vertical cut along x0,z0 -> x1,z1 (needs pillow)")
    p.add_argument("stage")
    for k in ("x0", "z0", "x1", "z1"):
        p.add_argument(k, type=float)
    p.add_argument("--path", type=parse_point, nargs="+")
    p.add_argument("--path-file")
    p.add_argument("-o", "--out")
    p.set_defaults(fn=cmd_section)

    p = sub.add_parser("course", help="lap length, route pieces and branches of a race course")
    p.add_argument("stage")
    p.set_defaults(fn=cmd_course)

    p = sub.add_parser("live", help="what is under each player in a running Dolphin")
    p.add_argument("--watch", type=float, metavar="SECONDS", help="repeat every SECONDS")
    p.add_argument("--capture", metavar="NAME", help="add player 1's spot as a landmark")
    p.add_argument("--player", type=int, default=0, help="player slot for --capture")
    p.add_argument("--shape", choices=["point", "sphere", "cylinder", "box"], default="sphere")
    p.add_argument("--r", type=float, default=5.0, help="radius, or half-width of a box")
    p.add_argument("--hy", type=float, default=0.0)
    p.add_argument("--note", default="")
    p.set_defaults(fn=cmd_live)

    p = sub.add_parser("stages", help="known stages and their aliases")
    p.add_argument("--detail", action="store_true", help="scale, extents, lap and counts per stage")
    p.add_argument("--csv", action="store_true")
    p.set_defaults(fn=cmd_stages)

    p = sub.add_parser("ground-types", help="every ground type's name, traction and sound row")
    p.add_argument("--csv", action="store_true")
    p.set_defaults(fn=cmd_ground_types)

    args = ap.parse_args(argv)
    if args.cmd == "at" and not args.landmark and (args.x is None or args.y is None):
        ap.error("at needs x y z, x z (on the ground), or --landmark")
    if args.cmd == "at" and args.z is None and args.y is not None:
        args.x, args.y, args.z = args.x, None, args.y
    args.fn(args)


if __name__ == "__main__":
    main()
