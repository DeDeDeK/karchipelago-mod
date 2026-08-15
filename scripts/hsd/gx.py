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
    0: (8, 8, 4),   # I4
    1: (8, 4, 8),   # I8
    2: (8, 4, 8),   # IA4
    3: (4, 4, 16),  # IA8
    4: (4, 4, 16),  # RGB565
    5: (4, 4, 16),  # RGB5A3
    6: (4, 4, 32),  # RGBA8
    8: (8, 8, 4),   # C4
    9: (8, 4, 8),   # C8
    10: (4, 4, 16),  # C14X2
    14: (8, 8, 4),  # CMPR
}

# GX texture format -> short name (for display).
FORMAT_NAME = {
    0: "I4", 1: "I8", 2: "IA4", 3: "IA8", 4: "RGB565", 5: "RGB5A3",
    6: "RGBA8", 8: "C4", 9: "C8", 10: "C14X2", 14: "CMPR",
}

GX_TF_I4 = 0
GX_TF_RGB5A3 = 5
GX_TF_RGBA8 = 6

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
    """Encode an RGBA PIL image to GX_TF_RGB5A3 (16bpp, 4x4 tiles, big-endian)."""
    w, h = im.size
    px = im.load()
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    out += struct.pack(">H", rgb5a3(*px[x, y]))
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
    return (int(tr + (255 - tr) * f),
            int(tg + (255 - tg) * f),
            int(tb + (255 - tb) * f))


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
