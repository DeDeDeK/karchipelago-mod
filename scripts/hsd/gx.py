# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""GX texture-format tables, sizing, and texel encoders.

Shared by the .dat tools so the format tables and the RGB5A3 / I4
encoders live in one place. No PIL dependency at import time - the
encoders take an already-loaded PIL image but reference no PIL API, so
walker.py can import the sizing helpers without pulling in Pillow.
"""

import struct

# GX texture format -> (block_w, block_h, bpp). Textures are stored in
# blocks; a level is padded up to whole blocks.
FORMAT_BLOCK = {
    0: (8, 8, 4),  # I4
    1: (8, 4, 8),  # I8
    2: (8, 4, 8),  # IA4
    3: (4, 4, 16),  # IA8
    4: (4, 4, 16),  # RGB565
    5: (4, 4, 16),  # RGB5A3
    6: (4, 4, 32),  # RGBA8
    8: (8, 8, 4),  # C4
    9: (8, 4, 8),  # C8
    10: (4, 4, 16),  # C14X2
    14: (8, 8, 4),  # CMPR
}

# GX texture format -> short name (for display).
FORMAT_NAME = {
    0: "I4",
    1: "I8",
    2: "IA4",
    3: "IA8",
    4: "RGB565",
    5: "RGB5A3",
    6: "RGBA8",
    8: "C4",
    9: "C8",
    10: "C14X2",
    14: "CMPR",
}

GX_TF_I4 = 0
GX_TF_RGB5A3 = 5
GX_TF_RGBA8 = 6
GX_TF_CMPR = 14

# Alpha >= this encodes as opaque (RGB555 with the top bit set); below it,
# the ARGB3444 form is used.
OPAQUE_ALPHA = 0xE0


def image_size(width, height, fmt, mipmap=False):
    """Bytes for a GX texture, padded up to the format's block size.
    Mipmaps add ~33% for the geometric pyramid; rounded to 1.4x for slack."""
    bw, bh, bpp = FORMAT_BLOCK.get(fmt, (4, 4, 16))
    pw = ((width + bw - 1) // bw) * bw
    ph = ((height + bh - 1) // bh) * bh
    base = pw * ph * bpp // 8
    return int(base * 1.4) if mipmap else base


def align32(n):
    """Padding bytes needed to 32-align a buffer of length n (GX requires
    textures / display lists / vertex arrays cache-line aligned)."""
    return (-n) & 31


def rgb5a3(r, g, b, a):
    """One RGBA pixel -> a GX_TF_RGB5A3 u16."""
    if a >= OPAQUE_ALPHA:
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)


def encode_rgb5a3(im):
    """Encode an RGBA PIL image to GX_TF_RGB5A3 (16bpp, 4x4 tiles, big-endian).
    Dimensions that are not a multiple of the 4x4 block edge-replicate into the
    padding texels, which the GPU never samples but the blob still stores."""
    w, h = im.size
    px = im.load()
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    out += struct.pack(">H", rgb5a3(*px[min(x, w - 1), min(y, h - 1)]))
    return bytes(out)


def encode_rgba8(im):
    """Encode an RGBA PIL image to GX_TF_RGBA8 (32bpp, full 8-bit color).
    Each 4x4 tile is 64 bytes: the 16 texels' AR pairs (row-major) followed by
    their GB pairs."""
    w, h = im.size
    px = im.load()
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, a = px[x, y]
                    out.append(a)
                    out.append(r)
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, a = px[x, y]
                    out.append(g)
                    out.append(b)
    return bytes(out)


def colorize(r, g, b, tint):
    """Map one RGB triple onto `tint`, preserving its luminance: dark texels
    scale the tint toward black, bright ones toward white. Keeps the source's
    shading and contrast while forcing its hue."""
    lum = (299 * r + 587 * g + 114 * b) // 1000
    tr, tg, tb = tint
    if lum <= 128:
        f = lum / 128.0
        return int(tr * f), int(tg * f), int(tb * f)
    f = (lum - 128) / 127.0
    return (
        int(tr + (255 - tr) * f),
        int(tg + (255 - tg) * f),
        int(tb + (255 - tb) * f),
    )


def tint_cmpr(blob, tint):
    """Recolor a CMPR texture by rewriting the two RGB565 endpoints of each
    8-byte sub-block, leaving the 2-bit index words untouched.

    A sub-block whose first endpoint compares greater than its second is
    opaque; otherwise its index 3 is transparent. Colorizing is monotonic in
    luminance, so that ordering almost always survives - the clamps below
    restore it in the corner cases where quantization collapses the two
    endpoints onto each other."""
    out = bytearray(blob)
    for off in range(0, len(out) - 7, 8):
        c0, c1 = struct.unpack_from(">HH", out, off)
        n0 = _tint565(c0, tint)
        n1 = _tint565(c1, tint)
        if c0 > c1 and n0 <= n1:
            n0 = min(0xFFFF, n1 + 1)
        elif c0 <= c1 and n0 > n1:
            n0 = n1
        struct.pack_into(">HH", out, off, n0, n1)
    return bytes(out)


def _tint565(v, tint):
    r = ((v >> 11) & 0x1F) << 3
    g = ((v >> 5) & 0x3F) << 2
    b = (v & 0x1F) << 3
    r, g, b = colorize(r, g, b, tint)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def gray_cmpr(blob, floor):
    """Flatten a CMPR texture to gray, its own luminance range stretched onto
    [floor, 1.0]. A map rewritten this way shades a surface without tinting it,
    so a TEV stage can modulate a material color by it and keep the hue; the
    stretch is what lets the brightest texel still reach the material's full
    color however dark the source was."""
    out = bytearray(blob)
    lums = [
        _lum565(v)
        for off in range(0, len(out) - 7, 8)
        for v in struct.unpack_from(">HH", out, off)
    ]
    lo, hi = (min(lums), max(lums)) if lums else (0, 0)
    for off in range(0, len(out) - 7, 8):
        c0, c1 = struct.unpack_from(">HH", out, off)
        n0, n1 = _gray565(c0, floor, lo, hi), _gray565(c1, floor, lo, hi)
        if c0 > c1 and n0 <= n1:
            n0 = min(0xFFFF, n1 + 1)
        elif c0 <= c1 and n0 > n1:
            n0 = n1
        struct.pack_into(">HH", out, off, n0, n1)
    return bytes(out)


def _lum565(v):
    r = ((v >> 11) & 0x1F) << 3
    g = ((v >> 5) & 0x3F) << 2
    b = (v & 0x1F) << 3
    return (299 * r + 587 * g + 114 * b) // 1000


def _gray565(v, floor, lo, hi):
    t = (_lum565(v) - lo) / (hi - lo) if hi > lo else 1.0
    y = min(255, max(0, int(255.0 * (floor + (1.0 - floor) * t))))
    return ((y >> 3) << 11) | ((y >> 2) << 5) | (y >> 3)


def solid_cmpr(width, height, color):
    """A flat, fully opaque CMPR image. Every 2-bit index selects the first
    endpoint, which is opaque under both of CMPR's block modes, so the second
    can stay black and the block decodes to one color at alpha 1."""
    r, g, b = color
    c0 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack(">HHI", c0, 0, 0) * (image_size(width, height, GX_TF_CMPR) // 8)


def encode_i4_alpha(im):
    """Encode the alpha channel of an RGBA PIL image to GX_TF_I4 (4bpp, 8x8
    tiles, 2px/byte). Intensity acts as alpha at runtime, so the shape comes
    from the source alpha and the tint is applied by the material."""
    w, h = im.size
    a = im.split()[3].load()
    out = bytearray()
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for y in range(ty, ty + 8):
                for x in range(tx, tx + 8, 2):
                    out.append(((a[x, y] >> 4) << 4) | (a[x + 1, y] >> 4))
    return bytes(out)


def encode_cmpr(im):
    """Encode an opaque PIL image to GX_TF_CMPR (4bpp). Tiles are 8x8, each
    holding four 4x4 sub-blocks; a sub-block stores two RGB565 endpoints and
    sixteen 2-bit palette indices. The endpoints are the corners of the block's
    color bounding box, ordered c0 > c1 so the block decodes in the four-color
    opaque mode; a flat block collapses onto one endpoint, which is opaque under
    either mode."""
    w, h = im.size
    px = im.load()
    out = bytearray()
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for sy in (0, 4):
                for sx in (0, 4):
                    out += _cmpr_block(px, w, h, tx + sx, ty + sy)
    return bytes(out)


def _cmpr_block(px, w, h, bx, by):
    texels = [
        px[min(bx + x, w - 1), min(by + y, h - 1)][:3]
        for y in range(4)
        for x in range(4)
    ]
    c0 = _pack565([max(t[i] for t in texels) for i in range(3)])
    c1 = _pack565([min(t[i] for t in texels) for i in range(3)])
    if c0 == c1:
        return struct.pack(">HHI", c0, 0, 0)
    pal = _cmpr_palette(c0, c1)
    bits = 0
    for i, t in enumerate(texels):
        best = min(
            range(4), key=lambda k: sum((t[j] - pal[k][j]) ** 2 for j in range(3))
        )
        bits |= best << (30 - 2 * i)
    return struct.pack(">HHI", c0, c1, bits)


def _pack565(rgb):
    return ((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3)


def _cmpr_palette(c0, c1):
    """The four colors a CMPR (DXT1) sub-block interpolates between."""

    def unpack(v):
        return (
            ((v >> 11) & 31) * 255 // 31,
            ((v >> 5) & 63) * 255 // 63,
            (v & 31) * 255 // 31,
        )

    a, b = unpack(c0), unpack(c1)
    if c0 > c1:
        return [
            a + (255,),
            b + (255,),
            tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
            tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,),
        ]
    return [
        a + (255,),
        b + (255,),
        tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,),
        (0, 0, 0, 0),
    ]


def decode_cmpr(blob, width, height):
    """GX_TF_CMPR blob -> RGBA Pillow image. CMPR tiles 8x8, each tile holding
    four 4x4 DXT1 sub-blocks in row-major order."""
    from PIL import Image

    im = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    px = im.load()
    off = 0
    for ty in range(0, height, 8):
        for tx in range(0, width, 8):
            for sy in (0, 4):
                for sx in (0, 4):
                    c0, c1 = struct.unpack_from(">HH", blob, off)
                    bits = struct.unpack_from(">I", blob, off + 4)[0]
                    off += 8
                    pal = _cmpr_palette(c0, c1)
                    for y in range(4):
                        for x in range(4):
                            idx = (bits >> (30 - 2 * (y * 4 + x))) & 3
                            X, Y = tx + sx + x, ty + sy + y
                            if X < width and Y < height:
                                px[X, Y] = pal[idx]
    return im
