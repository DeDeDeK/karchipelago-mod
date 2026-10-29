# SPDX-License-Identifier: GPL-3.0-only
"""HSD_Spline evaluation, the way the game's `splGetSplinePoint` (0x80414fc0)
and `splArcLengthGetParameter` (0x80415758) do it.

A spline is {u8 type, u8, s16 n, float tension, Vec3 *cv, float total_length,
float *seg_length (n cumulative entries, 0..1), float (*seg_poly)[5]}. It has
n - 1 segments whatever its type; u in [0, 1] maps uniformly onto them, not by
arc length. The CV count depends on the type:

    LINEAR   n        segment i lerps cv[i] -> cv[i+1]
    BEZIER   3n - 2   segment i is the cubic over cv[3i .. 3i+3]
    BSPLINE  n + 2    uniform cubic B-spline over cv[i .. i+3]
    TENSION  n + 2    cardinal spline with a = tension from cv[i+1] to cv[i+2];
                      cv[0] and the last CV only shape the ends

Stage splines are stored unscaled; the stage load multiplies every CV and
total_length by the stage scale in place, so pass the stage scale to get
world units.
"""

import math
import struct

LINEAR, BEZIER, BSPLINE, TENSION = 0, 1, 2, 3


class Spline:
    def __init__(self, arc, off, scale=1.0):
        d = arc.data

        def ptr(o):
            return struct.unpack_from(">I", d, o)[0] if o in arc.reloc_set else 0

        self.off = off
        self.type = d[off]
        self.n = struct.unpack_from(">h", d, off + 2)[0]
        self.tension = struct.unpack_from(">f", d, off + 4)[0]
        ncv = {LINEAR: self.n, BEZIER: 3 * self.n - 2}.get(self.type, self.n + 2)
        cv = ptr(off + 0x08)
        self.cv = [
            tuple(scale * c for c in struct.unpack_from(">3f", d, cv + 12 * i)) for i in range(ncv)
        ]
        self.length = scale * struct.unpack_from(">f", d, off + 0x0C)[0]
        seg = ptr(off + 0x10)
        self.seg = [struct.unpack_from(">f", d, seg + 4 * i)[0] for i in range(self.n)] if seg else []

    def eval(self, i, t):
        """The point at fraction t of segment i."""
        cv = self.cv
        if self.type == LINEAR:
            return tuple(a + t * (b - a) for a, b in zip(cv[i], cv[i + 1]))
        s, t2, t3, a = 1.0 - t, t * t, t * t * t, self.tension
        if self.type == BEZIER:
            w = (s ** 3, 3 * t * s * s, 3 * t2 * s, t3)
            p = cv[3 * i:3 * i + 4]
        elif self.type == BSPLINE:
            w = (s ** 3 / 6, (3 * t3 - 6 * t2 + 4) / 6, (3 * (t + t2 - t3) + 1) / 6, t3 / 6)
            p = cv[i:i + 4]
        else:
            w = (a * (-t3 + 2 * t2 - t), 1 + (2 - a) * t3 + (a - 3) * t2,
                 a * t + (a - 2) * t3 + (3 - 2 * a) * t2, a * (t3 - t2))
            p = cv[i:i + 4]
        return tuple(sum(w[j] * p[j][c] for j in range(4)) for c in range(3))

    def point(self, u):
        """The point at parameter u, uniform per segment."""
        m = self.n - 1
        if m < 1:
            return self.cv[0]
        if u >= 1.0:
            return self.eval(m - 1, 1.0)
        f = max(u, 0.0) * m
        return self.eval(int(f), f - int(f))

    def point_at(self, dist):
        """The point `dist` along the curve from its start."""
        if self.length <= 0 or len(self.seg) < 2:
            return self.point(0.0)
        s = min(max(dist / self.length, 0.0), 1.0)
        i = 0
        while i < self.n - 2 and self.seg[i + 1] < s:
            i += 1
        want = (s - self.seg[i]) * self.length
        prev, acc, steps = self.eval(i, 0.0), 0.0, 256
        for j in range(1, steps + 1):
            cur = self.eval(i, j / steps)
            step = math.dist(prev, cur)
            if acc + step >= want or j == steps:
                t = (j - 1 + (want - acc) / step) / steps if step else j / steps
                return self.eval(i, min(t, 1.0))
            acc, prev = acc + step, cur
        return self.eval(i, 1.0)

    def polyline(self, per_segment=8):
        if self.type == LINEAR:
            return list(self.cv)
        out = [self.eval(0, 0.0)]
        for i in range(self.n - 1):
            out += [self.eval(i, j / per_segment) for j in range(1, per_segment + 1)]
        return out

    def ends(self):
        return self.point(0.0), self.point(1.0)
