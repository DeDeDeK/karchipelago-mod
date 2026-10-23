#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check custom_events' cannon placements against City Trial's collision.

Replays the event's placement for every row of the `placements` table in
`event_cannon.c` - the barrel pose, then Machine Passage's flight turned onto
the row's heading and stretched by its range - and sweeps the machine's path
until the flight anim ends: the barrel mouth plus the flight's change in
position times City Trial's stage scale, which is how the path jump moves it.
A path that crosses collision before the anim ends stops the machine against
it.

    uv run python scripts/authoring/check_cannon_flights.py
    uv run python scripts/authoring/check_cannon_flights.py --scan 15
    uv run --with pillow python scripts/authoring/check_cannon_flights.py --render out.png
"""

import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import make_cannon_manifest as manifest
from hsd.archive import Archive
from hsd.geom_bounds import mat_mul
from hsd.spline import Spline
from hsd.world import Raster, Stage, mtx_srt

SOURCE = os.path.join("mods", "custom_events", "src", "event_cannon.c")
GROUND_TOP = 600.0  # CANNON_GROUND_TOP, where the placement's ground cast starts
SAMPLES = 200


def read_placements(path=SOURCE):
    with open(path) as f:
        src = f.read()
    body = re.search(r"placements\[\]\s*=\s*\{(.*?)\};", src, re.DOTALL).group(1)
    rows = re.findall(r'\{\s*"([^"]*)",\s*([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f\s*\}', body)
    return [(label, float(x), float(z), float(h), float(r)) for label, x, z, h, r in rows]


class Cannon:
    def __init__(self, city):
        self.city = city
        plan = manifest.plan()
        donor = Archive(os.path.join("iso", "files", manifest.STAGE_DONOR))
        _, _, path = manifest.cannon_params(donor)
        jdesc = donor.ptr(donor.ptr(path + 0x08) + 0x0C)  # AnimJoint aobj -> obj_id
        self.flight = Spline(donor, donor.ptr(jdesc + 0x10))
        self.scale = plan["scale"]
        self.anchor_pos, self.anchor_rot = plan["anchor_pos"], plan["anchor_rot"]
        self.floor = plan["floor"]
        self.start = tuple(plan["pivot"][k] + plan["anchor_pos"][k] for k in range(3))
        end = self.flight.cv[-1]
        self.heading = math.atan2(end[0] - self.start[0], end[2] - self.start[2])

    def path(self, x, z, heading, rng):
        city = self.city
        g = city.ground(x, z, below=GROUND_TOP)
        gy = g[0] if g else 0.0
        psi = math.radians(heading) - self.heading
        root = mtx_srt([self.scale] * 3, [0.0, psi, 0.0], (x, gy - self.floor * self.scale, z), None)
        world = mat_mul(root, mtx_srt([1.0] * 3, self.anchor_rot, self.anchor_pos, None))
        mouth = (world[0][3], world[1][3], world[2][3])
        f = self.scale / city.scale * rng
        c, s = math.cos(psi), math.sin(psi)
        sp = Spline.__new__(Spline)
        sp.type, sp.n, sp.tension, sp.seg = self.flight.type, self.flight.n, self.flight.tension, self.flight.seg
        sp.length = self.flight.length * f
        sp.cv = []
        for cv in self.flight.cv:
            r = [cv[k] - self.start[k] for k in range(3)]
            d = (c * r[0] + s * r[2], r[1], -s * r[0] + c * r[2])
            sp.cv.append(tuple(mouth[k] / city.scale + d[k] * f for k in range(3)))
        p0 = sp.point(0.0)
        return [
            tuple(mouth[k] + (sp.point(i / SAMPLES)[k] - p0[k]) * city.scale for k in range(3))
            for i in range(SAMPLES + 1)
        ]


def check(city, raster, pts):
    """(first hit or None, lowest clearance after leaving the barrel, end over land)."""
    hit = None
    for a, b in zip(pts, pts[1:]):
        hit = city.raycast(a, b, two_sided=True)
        if hit:
            break
    clear = math.inf
    for p in pts[SAMPLES // 10:]:
        g = city.ground(p[0], p[2], below=p[1])
        clear = min(clear, p[1] - g[0] if g else math.inf)
    c = raster.index(pts[-1][0], pts[-1][2])
    return hit, clear, c >= 0 and raster.land[c]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scan", type=float, metavar="STEP", help="also try every heading in STEP degrees")
    ap.add_argument("--render", metavar="PNG", help="draw the flights over City Trial (needs pillow)")
    args = ap.parse_args(argv)

    city = Stage("GrCity1")
    raster = Raster(city, 4.0)
    cannon = Cannon(city)
    placements = read_placements()
    bad = 0
    paths = []
    for label, x, z, heading, rng in placements:
        pts = cannon.path(x, z, heading, rng)
        paths.append((label, x, z, pts))
        hit, clear, land = check(city, raster, pts)
        apex = max(p[1] for p in pts)
        e = pts[-1]
        state = "BLOCKED" if hit else "clear"
        print(f"{label}: heading {heading:g} range {rng:g}: {state}, apex y {apex:.0f}, "
              f"lowest {clear:.0f} above ground, anim ends at ({e[0]:.0f}, {e[1]:.0f}, {e[2]:.0f})"
              f" over {'land' if land else 'the sea'}")
        if hit:
            bad += 1
            print(f"  hits {city.tri_desc(hit[2])} at ({hit[1][0]:.1f}, {hit[1][1]:.1f}, {hit[1][2]:.1f})")
        if args.scan:
            ok = []
            h = 0.0
            while h < 360.0:
                hit, clear, land = check(city, raster, cannon.path(x, z, h, rng))
                if not hit:
                    ok.append(f"{h:g}{'' if land else ' (sea)'}")
                h += args.scan
            print("  clear headings: " + ", ".join(ok))
    if args.render:
        from hsd import stage_render as R

        win = R.land_window(city, Raster(city, 8.0))
        span = max(win[2] - win[0], win[3] - win[1])
        view = R.View(win, 1100 / span)
        img, legend = R.base_image(city, R.build_raster(city, win), view)
        cv = R.Canvas(img, view)
        cv.grid()
        for label, x, z, pts in paths:
            cv.polyline([(p[0], p[2]) for p in pts], (255, 220, 0), width=3)
            cv.marker(x, z, (255, 40, 40), label, shape="x", size=6, font=cv.font)
        cv.legend("cannon flights (+x right, +z down)", legend)
        img.save(args.render)
        print(f"wrote {args.render}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
