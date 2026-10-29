# SPDX-License-Identifier: GPL-3.0-only
"""Top-down and cross-section pictures of a stage, drawn from a `world.Stage`.

The top-down view puts +X to the right and +Z down, world units throughout,
with a coordinate grid so a point read off the picture can be typed straight
back into a query. Needs Pillow (`uv run --with pillow python ...`).
"""

import math

from PIL import Image, ImageDraw, ImageFont

from .world import CAT_FLOOR, CAT_WALL, Raster, ground_type_name

# A terrain ramp from low ground to peaks, as (fraction, RGB) stops.
HEIGHT_RAMP = [
    (0.00, (40, 110, 60)),
    (0.25, (110, 160, 70)),
    (0.45, (215, 200, 110)),
    (0.65, (170, 120, 70)),
    (0.85, (140, 130, 125)),
    (1.00, (250, 250, 250)),
]
SEA = (40, 80, 150)
BARRIER = (190, 80, 230)
COURSE = (255, 255, 255)
BRANCH = (0, 220, 255)
RAIL = (255, 90, 40)
CPU = (120, 120, 255)
VOID = (16, 16, 20)
PALETTE = [
    (230, 159, 0), (86, 180, 233), (0, 158, 115), (240, 228, 66), (0, 114, 178),
    (213, 94, 0), (204, 121, 167), (150, 150, 150), (120, 70, 30), (170, 220, 120),
    (255, 140, 180), (90, 60, 160), (200, 60, 60), (60, 200, 200), (240, 200, 160),
    (100, 140, 60), (255, 255, 255), (130, 0, 60), (0, 80, 60), (200, 180, 0),
]
OVERLAY_COLORS = {
    "start": (255, 60, 60),
    "vehicle": (60, 120, 255),
    "item": (255, 210, 0),
    "event": (255, 120, 255),
    "item_area": (255, 160, 60),
    "vehicle_area": (120, 200, 255),
    "enemy": (255, 255, 255),
    "global_dead": (0, 0, 0),
}


def _font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def nice_step(span, ticks=8):
    """A 1-2-5 step giving about `ticks` divisions of `span`."""
    raw = max(span, 1e-6) / ticks
    mag = 10 ** math.floor(math.log10(raw))
    for k in (1, 2, 5, 10):
        if raw <= k * mag:
            return k * mag
    return 10 * mag


def _ramp(f):
    f = min(max(f, 0.0), 1.0)
    for (f0, c0), (f1, c1) in zip(HEIGHT_RAMP, HEIGHT_RAMP[1:]):
        if f <= f1:
            u = (f - f0) / (f1 - f0) if f1 > f0 else 0.0
            return tuple(int(c0[k] + (c1[k] - c0[k]) * u) for k in range(3))
    return HEIGHT_RAMP[-1][1]


class View:
    """Maps world XZ to pixels for a window (x0, z0, x1, z1) at `ppu` pixels
    per world unit."""

    def __init__(self, window, ppu):
        self.x0, self.z0, self.x1, self.z1 = window
        self.ppu = ppu
        self.w = max(1, round((self.x1 - self.x0) * ppu))
        self.h = max(1, round((self.z1 - self.z0) * ppu))

    def px(self, x, z):
        return (x - self.x0) * self.ppu, (z - self.z0) * self.ppu


def land_window(stage, raster, margin=60.0):
    xs, zs = [], []
    for c, land in enumerate(raster.land):
        if land:
            x, z = raster.center(c)
            xs.append(x)
            zs.append(z)
    if not xs:
        return (stage.oob_min[0], stage.oob_min[2], stage.oob_max[0], stage.oob_max[2])
    return (min(xs) - margin, min(zs) - margin, max(xs) + margin, max(zs) + margin)


def base_image(stage, raster, view, layer="height"):
    """The background for `layer` (height, kind, layers) sampled into the view,
    plus the legend entries it needs."""
    land_y = sorted(y for y, land in zip(raster.top_y, raster.land) if land)
    lo, hi = 0.0, 1.0
    if land_y:
        lo = land_y[int(len(land_y) * 0.01)]
        hi = land_y[min(len(land_y) - 1, int(len(land_y) * 0.995))]
    kinds = {}
    img = Image.new("RGB", (view.w, view.h), VOID)
    pix = img.load()
    res = raster.res
    for py in range(view.h):
        z = view.z0 + (py + 0.5) / view.ppu
        for px in range(view.w):
            x = view.x0 + (px + 0.5) / view.ppu
            c = raster.index(x, z)
            if c < 0 or raster.top_tri[c] < 0:
                continue
            t = stage.tris[raster.top_tri[c]]
            y = raster.top_y[c]
            if layer == "kind":
                if t.gtype == 29:
                    col = SEA
                else:
                    col = PALETTE[t.gtype % len(PALETTE)]
                kinds[t.gtype] = kinds.get(t.gtype, 0) + 1
            elif layer == "layers":
                n = raster.layers[c]
                col = SEA if t.gtype == 29 and n == 1 else _ramp((n - 1) / 4.0)
            else:
                col = SEA if t.gtype == 29 else _ramp(_height_frac(y, lo, hi))
            if t.gtype != 29:
                col = _shade(raster, c, col, res)
            pix[px, py] = col
    legend = []
    if layer == "height":
        legend = [(f"{y:.0f}", _ramp(_height_frac(y, lo, hi)))
                  for y in _height_ticks(lo, hi)] + [("sea", SEA)]
    elif layer == "kind":
        legend = [
            (ground_type_name(k), SEA if k == 29 else PALETTE[k % len(PALETTE)])
            for k, _ in sorted(kinds.items(), key=lambda kv: -kv[1])
        ]
    elif layer == "layers":
        legend = [(f"{n} layer{'s' if n > 1 else ''}", _ramp((n - 1) / 4.0)) for n in range(1, 6)]
    return img, legend


# Heights are mostly near the ground with a few towers, so the ramp is stretched
# toward the low end.
HEIGHT_GAMMA = 0.55


def _height_frac(y, lo, hi):
    f = min(max((y - lo) / (hi - lo or 1.0), 0.0), 1.0)
    return f ** HEIGHT_GAMMA


def _height_ticks(lo, hi, n=5):
    return [lo + (hi - lo) * (k / (n - 1)) ** (1 / HEIGHT_GAMMA) for k in range(n)]


def _shade(raster, c, col, res):
    i, j = c % raster.nx, c // raster.nx

    def h(ii, jj):
        if 0 <= ii < raster.nx and 0 <= jj < raster.nz:
            v = raster.top_y[jj * raster.nx + ii]
            if v is not None:
                return v
        return raster.top_y[c]

    dx = (h(i + 1, j) - h(i - 1, j)) / (2 * res)
    dz = (h(i, j + 1) - h(i, j - 1)) / (2 * res)
    f = 1.0 + max(-0.45, min(0.35, (-dx - dz) * 0.35))
    return tuple(min(255, max(0, int(v * f))) for v in col)


class Canvas:
    def __init__(self, img, view):
        self.img = img
        self.view = view
        self.draw = ImageDraw.Draw(img)
        self.font = _font(max(11, int(img.width / 90)))
        self.small = _font(max(9, int(img.width / 120)))

    def grid(self, step=None):
        v = self.view
        if step is None:
            step = nice_step(max(v.x1 - v.x0, v.z1 - v.z0), 14)
        x = math.ceil(v.x0 / step) * step
        while x <= v.x1:
            px, _ = v.px(x, 0)
            self.draw.line([(px, 0), (px, v.h)], fill=(255, 255, 255, 60), width=1)
            self.draw.text((px + 2, 2), f"{x:g}", fill=(255, 255, 255), font=self.small)
            x += step
        z = math.ceil(v.z0 / step) * step
        while z <= v.z1:
            _, pz = v.px(0, z)
            self.draw.line([(0, pz), (v.w, pz)], fill=(255, 255, 255, 60), width=1)
            self.draw.text((2, pz + 2), f"{z:g}", fill=(255, 255, 255), font=self.small)
            z += step

    def segments(self, segs, color, width=1):
        for (ax, az), (bx, bz) in segs:
            self.draw.line([self.view.px(ax, az), self.view.px(bx, bz)], fill=color, width=width)

    def polyline(self, pts, color, width=2, label=None):
        if len(pts) < 2:
            return
        self.draw.line([self.view.px(x, z) for x, z in pts], fill=color, width=width)
        if label:
            self.label(pts[len(pts) // 2], label, color, self.small)

    def marker(self, x, z, color, label=None, shape="dot", size=4, font=None):
        px, pz = self.view.px(x, z)
        if shape == "x":
            self.draw.line([(px - size, pz - size), (px + size, pz + size)], fill=color, width=2)
            self.draw.line([(px - size, pz + size), (px + size, pz - size)], fill=color, width=2)
        elif shape == "square":
            self.draw.rectangle([px - size, pz - size, px + size, pz + size], outline=color, width=2)
        else:
            self.draw.ellipse([px - size, pz - size, px + size, pz + size], fill=color,
                              outline=(0, 0, 0))
        if label:
            self.label((x, z), label, color, font or self.small, dx=size + 2)

    def circle(self, x, z, r, color):
        px, pz = self.view.px(x, z)
        rp = r * self.view.ppu
        self.draw.ellipse([px - rp, pz - rp, px + rp, pz + rp], outline=color, width=2)

    def rect(self, lo, hi, color, label=None):
        a = self.view.px(lo[0], lo[2])
        b = self.view.px(hi[0], hi[2])
        self.draw.rectangle([min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1])],
                            outline=color, width=1)
        if label:
            self.draw.text((min(a[0], b[0]) + 2, min(a[1], b[1]) + 1), label, fill=color,
                           font=self.small)

    def label(self, xz, text, color, font, dx=3):
        px, pz = self.view.px(*xz)
        pos = (px + dx, pz - font.size // 2 if hasattr(font, "size") else pz - 5)
        self.draw.text(pos, text, fill=color, font=font, stroke_width=2, stroke_fill=(0, 0, 0))

    def legend(self, title, entries):
        f = self.small
        lines = [title] + [e[0] for e in entries]
        lh = (f.size if hasattr(f, "size") else 10) + 4
        w = max(self.draw.textlength(s, font=f) for s in lines) + 26
        h = lh * len(lines) + 8
        x0, y0 = self.img.width - w - 8, self.img.height - h - 8
        self.draw.rectangle([x0, y0, x0 + w, y0 + h], fill=(0, 0, 0))
        self.draw.text((x0 + 6, y0 + 4), title, fill=(255, 255, 255), font=f)
        for k, (text, color) in enumerate(entries):
            yy = y0 + 4 + lh * (k + 1)
            self.draw.rectangle([x0 + 6, yy + 2, x0 + 16, yy + lh - 4], fill=color)
            self.draw.text((x0 + 20, yy), text, fill=(255, 255, 255), font=f)


def barrier_segments(stage, view):
    out = []
    for t in stage.tris:
        if t.gtype != 30 or t.cat != CAT_WALL:
            continue
        pts = [(v[0], v[2]) for v in (t.v0, t.v1, t.v2)]
        a, b = max(((p, q) for p in pts for q in pts), key=lambda pq: _d2(*pq))
        if _visible(view, a) or _visible(view, b):
            out.append((a, b))
    return out


def wall_segments(stage, view, skip_gtypes=(30,)):
    out = []
    for t in stage.tris:
        if t.cat != CAT_WALL or t.gtype in skip_gtypes or abs(t.normal[1]) > 0.7:
            continue
        pts = [(v[0], v[2]) for v in (t.v0, t.v1, t.v2)]
        a, b = max(((p, q) for p in pts for q in pts), key=lambda pq: _d2(*pq))
        if _visible(view, a) or _visible(view, b):
            out.append((a, b))
    return out


def _d2(p, q):
    return (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2


def _visible(view, p):
    return view.x0 <= p[0] <= view.x1 and view.z0 <= p[1] <= view.z1


def section(stage, a, b, out_path, path=None, samples=400, width=1400, height=520):
    """A vertical cut along the line a -> b (XZ): every surface the line
    crosses, by distance along it, with floors, ceilings and walls colored
    apart, and an optional path of (x, y, z) points projected onto the cut."""
    length = math.hypot(b[0] - a[0], b[1] - a[1]) or 1.0
    pts = []
    for k in range(samples + 1):
        u = k / samples
        x, z = a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u
        for y, i in stage.column(x, z):
            t = stage.tris[i]
            if t.gtype == 30 and t.normal[1] < 0:
                continue
            pts.append((u * length, y, t))
    ys = [p[1] for p in pts] + ([p[1] for p in path] if path else [])
    ylo, yhi = (min(ys), max(ys)) if ys else (0.0, 100.0)
    pad = (yhi - ylo) * 0.08 + 5
    ylo, yhi = ylo - pad, yhi + pad
    img = Image.new("RGB", (width, height), (20, 22, 28))
    dr = ImageDraw.Draw(img)
    font = _font(13)
    m = 50

    def to_px(s, y):
        return m + s / length * (width - 2 * m), height - m - (y - ylo) / (yhi - ylo) * (height - 2 * m)

    step = nice_step(yhi - ylo, 7)
    y = math.ceil(ylo / step) * step
    while y <= yhi:
        _, py = to_px(0, y)
        dr.line([(m, py), (width - m, py)], fill=(60, 60, 70))
        dr.text((4, py - 7), f"{y:g}", fill=(200, 200, 200), font=font)
        y += step
    dstep = nice_step(length, 10)
    dist = 0.0
    while dist <= length + 1e-6:
        px, _ = to_px(dist, ylo)
        u = dist / length
        wx, wz = a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u
        dr.line([(px, m), (px, height - m)], fill=(45, 45, 55))
        dr.text((px - 30, height - m + 6), f"{dist:g}", fill=(200, 200, 200), font=font)
        dr.text((px - 30, height - m + 22), f"({wx:.0f}, {wz:.0f})", fill=(140, 140, 150), font=font)
        dist += dstep
    for s, y, t in pts:
        if t.gtype == 29:
            col = SEA
        elif t.cat == CAT_FLOOR and t.normal[1] > 0:
            col = (120, 220, 120)
        elif t.normal[1] < 0:
            col = (230, 120, 90)
        else:
            col = (180, 180, 180)
        px, py = to_px(s, y)
        dr.rectangle([px - 1, py - 1, px + 1, py + 1], fill=col)
    if path:
        dx, dz = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        line = [to_px((p[0] - a[0]) * dx + (p[2] - a[1]) * dz, p[1]) for p in path]
        dr.line(line, fill=(255, 220, 0), width=2)
    dr.text((m, 8), f"{stage.stem} section ({a[0]:g}, {a[1]:g}) -> ({b[0]:g}, {b[1]:g}), "
            f"{length:.0f} units; green floor, red underside, grey wall, yellow path",
            fill=(255, 255, 255), font=font)
    img.save(out_path)


def build_raster(stage, window, res=None):
    if res is None:
        span = max(window[2] - window[0], window[3] - window[1])
        res = 4.0 if span > 900 else 2.0 if span > 300 else 1.0
    return Raster(stage, res, bounds=window)
