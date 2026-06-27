# SPDX-License-Identifier: GPL-3.0-only
"""Author the AP checklist textures into a loadable HSD archive.

Builds two GX textures from mods/archipelago/assets/ap-icon.png and packs them,
with their image descriptors, into a minimal standalone .dat that exports two
publics:

  apBannerImg  - _HSD_ImageDesc for the scrolling banner watermark (RGB5A3 248x128)
  apEmblemImg  - _HSD_ImageDesc for the top-right tab emblem    (I4    64x64)

The archipelago mod loads this file (Gm_LoadGameFile "ApChecklistTex") once and
points the checklist's banner / tab-emblem TObjs at these descriptors, so the art
ships as a data file rather than a compiled-in C array.

1. Banner watermark (RGB5A3): the vertically-scrolling per-mode banner is an
   opaque gray panel that backs the checkbox grid with a very subtle logo embossed
   into it (the vanilla City Trial banner has a stdev of only ~6 per channel). The
   AP logo is composited over the panel color and pulled down to CONTRAST of its
   deviation from that color - a faint watermark that keeps the panel opaque (the
   grid needs the backing) while reading as the AP logo.

2. Tab emblem (I4): the mode emblem in the top-right tab indicator is an I4
   intensity quad whose material carries the per-mode tint. The AP version is an
   I4 intensity map of the logo (shape from the source alpha), so it renders
   through the same path (intensity acts as alpha -> transparent background) and
   takes the blue tint the checklist recolor applies.

Run from the repo root:
    uv run --with pillow python scripts/hsd/make_checklist_textures.py
"""
import os
import struct
from PIL import Image

# --- Banner watermark ------------------------------------------------------
BANNER_W = 248
BANNER_H = 128
PANEL = (121, 124, 131)   # gray panel color (~vanilla banner mean)
CONTRAST = 0.20           # fraction of the logo's deviation from the panel to keep
# On the banner quad a texel renders ~1.61x TALLER than it is wide (measured live
# with a checkerboard of known-square texels). So a logo that is square in texels
# comes out stretched vertically on screen; ASPECT pre-compensates by widening the
# logo's texel width by that factor, so it renders square. LOGO_H_FRAC sets the
# logo's height as a fraction of the texture height: < 1 leaves a gray margin so the
# vertically-tiled copies stay separated (no edge-to-edge "double vision"); larger
# is bigger on screen but closer together.
ASPECT = 1.61             # a texel renders this much taller than wide (measured)
LOGO_H_FRAC = 0.56        # logo height as a fraction of the 128px texture height

# --- Tab emblem ------------------------------------------------------------
EMBLEM_W = 64
EMBLEM_H = 64

# --- Archive ---------------------------------------------------------------
HSD_HEADER = 0x20
GX_TF_I4 = 0
GX_TF_RGB5A3 = 5
ARCHIVE_VERSION = b"001B"          # matches the vanilla game archives
IMAGEDESC_SIZE = 0x18              # _HSD_ImageDesc {ptr,u16 w,u16 h,u32 fmt,u32 mip,f32,f32}
BANNER_SYMBOL = "apBannerImg"
EMBLEM_SYMBOL = "apEmblemImg"

OPAQUE_THRESHOLD = 0xE0

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "mods", "archipelago", "assets", "ap-icon.png")
OUT_DAT = os.path.join(ROOT, "mods", "archipelago", "assets", "ApChecklistTex.dat")


def rgb5a3(r, g, b, a):
    if a >= OPAQUE_THRESHOLD:
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)


def make_banner_panel(src):
    """Opaque gray panel with the logo composited in, pre-distorted so the quad
    renders it square. Height is LOGO_H_FRAC of the panel; width is that height times
    ASPECT (widened to cancel the quad's vertical stretch). Centered, leaving a gray
    margin so the vertically-tiled copies stay separated."""
    lh = max(1, round(LOGO_H_FRAC * BANNER_H))
    lw = max(1, round(lh * ASPECT))
    rs = src.resize((lw, lh), Image.LANCZOS)
    out = Image.new("RGBA", (BANNER_W, BANNER_H), PANEL + (255,))
    out.paste(rs, ((BANNER_W - lw) // 2, (BANNER_H - lh) // 2), rs)
    return out, lw, lh


def encode_banner(src):
    """RGB5A3 (16bpp, 4x4 tiles, big-endian): opaque panel + faint logo watermark."""
    img, lw, lh = make_banner_panel(src)
    px = img.load()
    out = bytearray()
    for ty in range(0, BANNER_H, 4):
        for tx in range(0, BANNER_W, 4):
            for iy in range(4):
                for ix in range(4):
                    r, g, b, a = px[tx + ix, ty + iy]
                    # Pull each pixel toward the panel: faint, opaque watermark.
                    r = int(PANEL[0] + (r - PANEL[0]) * CONTRAST)
                    g = int(PANEL[1] + (g - PANEL[1]) * CONTRAST)
                    b = int(PANEL[2] + (b - PANEL[2]) * CONTRAST)
                    out += struct.pack(">H", rgb5a3(r, g, b, 255))
    return bytes(out), lw, lh


def encode_emblem(src):
    """I4 (4bpp, 8x8 tiles, 2px/byte): shape from the source alpha; tinted at runtime."""
    rs = src.resize((EMBLEM_W, EMBLEM_H), Image.LANCZOS)
    a = rs.split()[3].load()
    out = bytearray()
    for ty in range(0, EMBLEM_H, 8):
        for tx in range(0, EMBLEM_W, 8):
            for iy in range(8):
                for ix in range(0, 8, 2):
                    p0 = a[tx + ix, ty + iy] >> 4
                    p1 = a[tx + ix + 1, ty + iy] >> 4
                    out.append((p0 << 4) | p1)
    return bytes(out)


def align32(n):
    return (-n) & 31


def build_archive(banner_blob, emblem_blob):
    """Pack two image descriptors + their texel blobs into an HSD archive.

    Layout:
        data[0x00 .. 0x17]  apBannerImg descriptor (img_ptr reloc -> banner blob)
        data[0x18 .. 0x2F]  apEmblemImg descriptor (img_ptr reloc -> emblem blob)
        ... 32-align ...    banner texel blob (GX needs textures cache-line aligned)
        ... 32-align ...    emblem texel blob
    """
    banner_desc_off = 0x00
    emblem_desc_off = IMAGEDESC_SIZE

    data = bytearray(2 * IMAGEDESC_SIZE)

    data.extend(b"\0" * align32(len(data)))
    banner_blob_off = len(data)
    data.extend(banner_blob)

    data.extend(b"\0" * align32(len(data)))
    emblem_blob_off = len(data)
    data.extend(emblem_blob)

    def put_desc(off, blob_off, w, h, fmt):
        # _HSD_ImageDesc: img_ptr(reloc), u16 width, u16 height, u32 format,
        # u32 mipmap, f32 minLOD, f32 maxLOD.
        struct.pack_into(">IHHIIff", data, off, blob_off, w, h, fmt, 0, 0.0, 0.0)

    put_desc(banner_desc_off, banner_blob_off, BANNER_W, BANNER_H, GX_TF_RGB5A3)
    put_desc(emblem_desc_off, emblem_blob_off, EMBLEM_W, EMBLEM_H, GX_TF_I4)

    relocs = [banner_desc_off + 0x00, emblem_desc_off + 0x00]  # both img_ptr slots

    # Public table {u32 data_off, u32 name_off} + a packed string table.
    strings = bytearray()
    publics = []
    for sym, doff in ((BANNER_SYMBOL, banner_desc_off), (EMBLEM_SYMBOL, emblem_desc_off)):
        publics.append((doff, len(strings)))
        strings.extend(sym.encode("ascii") + b"\0")

    data_bytes = bytes(data)
    reloc_bytes = b"".join(struct.pack(">I", r) for r in relocs)
    public_bytes = b"".join(struct.pack(">II", doff, noff) for doff, noff in publics)
    file_size = (HSD_HEADER + len(data_bytes) + len(reloc_bytes)
                 + len(public_bytes) + len(strings))
    header = struct.pack(">IIIII4s8x", file_size, len(data_bytes),
                         len(relocs), len(publics), 0, ARCHIVE_VERSION)
    return header + data_bytes + reloc_bytes + public_bytes + bytes(strings)


def main():
    src = Image.open(SRC).convert("RGBA")
    banner_blob, lw, lh = encode_banner(src)
    emblem_blob = encode_emblem(src)
    archive = build_archive(banner_blob, emblem_blob)
    with open(OUT_DAT, "wb") as f:
        f.write(archive)
    print(f"banner: {BANNER_W}x{BANNER_H} RGB5A3 ({len(banner_blob)} bytes, "
          f"logo {lw}x{lh}, {int(CONTRAST*100)}% watermark)")
    print(f"emblem: {EMBLEM_W}x{EMBLEM_H} I4 ({len(emblem_blob)} bytes)")
    print(f"wrote {OUT_DAT} ({len(archive) / 1024:.1f} KB, "
          f"publics '{BANNER_SYMBOL}' / '{EMBLEM_SYMBOL}')")


if __name__ == "__main__":
    main()
