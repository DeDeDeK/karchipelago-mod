"""Custom item archive carver.

Carves an item model subtree out of `iso/files/Item.dat` and packs it into a
minimal standalone archive that exports a single public `customItem` (a
`CustomItemDesc`, see mods/custom_items/include/custom_items_api.h). The
descriptor's `model` field is relocated to point at the carved JOBJDesc root, so
the custom_items mod can splice the model into the live item table.

`Item.dat` exposes one public `itData`: an array of 69 itData structs (0x18
stride). For kind K, `itData[K].model` (at +0x08) points at a `{JOBJ* j; int flags}`
pair whose `j` is the model's JOBJDesc root (and whose `flags` we carry into the
descriptor's model_flag). We walk that subtree (reusing the backdrop walker),
then emit:

    new_data[0x00 .. 0x33] : CustomItemDesc
    new_data[0x34 ..      ] : name string, then the carved model ranges

The descriptor's `model` (and optional `effect_info`) become synthetic
relocations exactly as carve_backdrop.py synthesizes its ModelSection/pp slots.

Usage:
    uv run python scripts/hsd/carve_custom_item.py \
        iso/files/Item.dat <source_kind> <out.dat> <name> \
        [--base-kind K] [--group {bad,good,fake}] \
        [--weight-blue N] [--weight-green N] [--weight-red N]

`source_kind` selects the model to carve (e.g. 3 = Boost patch, 28 = Bomb copy
panel). `--base-kind` is the vanilla kind the custom item clones behavior from
(default: the source kind), so by default the carved item behaves like the kind
it was carved from, just under a new kind number with author-set spawn weights.
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, HSD_HEADER, u32, u16

from hsd.walker import Walker, merge_intervals

# Must match include/custom_items_api.h.
CUSTOM_ITEM_MAGIC = 0x4349544D  # 'CITM'
CUSTOM_ITEM_DESC_VERSION = 2
DESC_SIZE = 0x34
ITDATA_STRIDE = 0x18
GROUPS = {"bad": 0, "good": 1, "fake": 2}
GX_TF_RGB5A3 = 5
GX_FORMATS = {0: "I4", 1: "I8", 2: "IA4", 3: "IA8", 4: "RGB565",
              5: "RGB5A3", 6: "RGBA8", 8: "C4", 9: "C8", 10: "C14X2", 14: "CMPR"}


def fit_image(im, tw, th, fit):
    """Resize `im` to (tw, th). fit=stretch distorts to fill; fit=cover scales to
    fill then center-crops (no distortion, may clip); fit=contain scales to fit
    inside and pads the remainder transparent (no distortion, no clip)."""
    from PIL import Image
    if fit == "stretch":
        return im.resize((tw, th), Image.LANCZOS)
    sw, sh = im.size
    if fit == "cover":
        scale = max(tw / sw, th / sh)
        rs = im.resize((max(1, round(sw * scale)), max(1, round(sh * scale))), Image.LANCZOS)
        left, top = (rs.width - tw) // 2, (rs.height - th) // 2
        return rs.crop((left, top, left + tw, top + th))
    # contain
    scale = min(tw / sw, th / sh)
    rs = im.resize((max(1, round(sw * scale)), max(1, round(sh * scale))), Image.LANCZOS)
    out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    out.paste(rs, ((tw - rs.width) // 2, (th - rs.height) // 2))
    return out


def encode_rgb5a3(im):
    """Encode an RGBA PIL image to GX_TF_RGB5A3 (16bpp, 4x4 tiles, big-endian).
    Opaque pixels (alpha >= 0xe0) use RGB555 (top bit set); others use the
    ARGB3444 form."""
    w, h = im.size
    px = im.load()
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, a = px[x, y]
                    if a >= 0xE0:
                        val = 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
                    else:
                        val = ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
                    out += struct.pack(">H", val)
    return bytes(out)


def carve(item_dat, source_kind, out_path, name, base_kind, group, weight_box,
          weight_event, texture_png, texture_index, texture_fit):
    arc = Archive(item_dat)
    if "itData" not in arc.publics:
        raise SystemExit("public symbol 'itData' not found - is this Item.dat?")

    itdata_base = arc.publics["itData"]
    # itData[source_kind].model -> {JOBJ* j; int flags}; j is the JOBJDesc root,
    # flags is the render flag we carry forward so skinned models set up right.
    model_slot = itdata_base + source_kind * ITDATA_STRIDE + 0x08
    model_pair = u32(arc.data, model_slot)
    if model_pair == 0:
        raise SystemExit(f"kind {source_kind} has no model")
    root_jobj = u32(arc.data, model_pair)
    model_flag = u32(arc.data, model_pair + 4)
    if root_jobj == 0:
        raise SystemExit(f"kind {source_kind} model JOBJDesc is NULL")
    print(f"  kind {source_kind}: model pair={model_pair:#x} -> JOBJDesc={root_jobj:#x} flag={model_flag:#010x}")

    visited = Walker(arc).walk(root_jobj, "JOBJDesc")

    # Enumerate the model's textures (ImageDescs), in data order, so a custom
    # texture can target a specific slot on multi-material models.
    img_descs = sorted(o for o, (t, _) in visited.items() if t == "ImageDesc")
    for idx, o in enumerate(img_descs):
        tw, th = u16(arc.data, o + 0x04), u16(arc.data, o + 0x06)
        fmt = u32(arc.data, o + 0x08)
        print(f"  texture[{idx}]: {tw}x{th} {GX_FORMATS.get(fmt, fmt)}")

    # Optional custom texture: re-encode a PNG (RGB5A3) into the chosen ImageDesc.
    # The old texture data is dropped from the kept ranges; the new blob is
    # appended below and that one ImageDesc repointed/reformatted at it (the
    # remaining materials keep their original textures/formats).
    img_desc_off = None
    new_blob = None
    if texture_png is not None:
        if not img_descs:
            raise SystemExit("model has no ImageDesc to replace")
        if texture_index < 0 or texture_index >= len(img_descs):
            raise SystemExit(f"--texture-index {texture_index} out of range "
                             f"(model has {len(img_descs)} texture(s))")
        img_desc_off = img_descs[texture_index]
        blob_off = u32(arc.data, img_desc_off + 0x00)
        tw = u16(arc.data, img_desc_off + 0x04)
        th = u16(arc.data, img_desc_off + 0x06)
        from PIL import Image
        im = fit_image(Image.open(texture_png).convert("RGBA"), tw, th, texture_fit)
        new_blob = encode_rgb5a3(im)
        del visited[blob_off]
        print(f"  replacing texture[{texture_index}] -> {tw}x{th} RGB5A3 "
              f"({texture_fit}, {len(new_blob)} bytes)")

    intervals = merge_intervals([(off, off + sz) for off, (_, sz) in visited.items()])
    kept = sum(e - s for s, e in intervals)
    print(f"  reached {len(visited)} objects, kept {len(intervals)} ranges, {kept / 1024:.1f} KB")

    # Layout: descriptor, then name string (4-aligned), then carved ranges.
    name_bytes = name.encode("ascii") + b"\0"
    name_off = DESC_SIZE
    prefix = name_off + len(name_bytes)
    prefix = (prefix + 3) & ~3  # 4-align before the model ranges

    remap = {}
    cursor = prefix
    new_data = bytearray(prefix)
    new_data[name_off:name_off + len(name_bytes)] = name_bytes

    for s, e in intervals:
        # Preserve each byte's source-relative 32-byte alignment (GX needs
        # textures / display lists / vertex arrays cache-line aligned).
        pad = ((s & 31) - (cursor & 31)) & 31
        if pad:
            new_data.extend(b"\0" * pad)
            cursor += pad
        for o in range(s, e):
            remap[o] = cursor + (o - s)
        new_data.extend(arc.data[s:e])
        cursor += e - s

    # Fill in the descriptor (big-endian, matching CustomItemDesc).
    struct.pack_into(">I", new_data, 0x00, CUSTOM_ITEM_MAGIC)
    struct.pack_into(">H", new_data, 0x04, CUSTOM_ITEM_DESC_VERSION)
    struct.pack_into(">H", new_data, 0x06, 0)
    struct.pack_into(">I", new_data, 0x08, name_off)            # name (reloc)
    struct.pack_into(">i", new_data, 0x0C, base_kind)
    struct.pack_into(">i", new_data, 0x10, group)
    struct.pack_into(">I", new_data, 0x14, remap[root_jobj])    # model (reloc)
    struct.pack_into(">I", new_data, 0x18, 0)                   # effect_info (inherit base)
    struct.pack_into(">HHH", new_data, 0x1C, *weight_box)       # weight_box[3]
    struct.pack_into(">H", new_data, 0x22, 0)                   # weight_free (reserved)
    struct.pack_into(">HHHHHH", new_data, 0x24, *weight_event)  # weight_event[6]
    struct.pack_into(">I", new_data, 0x30, model_flag)          # model_flag (v2)

    new_relocs = [0x08, 0x14]  # name, model
    dropped = 0
    for src in arc.relocs:
        if src not in remap:
            continue
        if src == img_desc_off:
            continue  # ImageDesc image pointer - repointed to the appended blob below
        tgt = u32(arc.data, src)
        new_src = remap[src]
        if tgt in remap:
            struct.pack_into(">I", new_data, new_src, remap[tgt])
            new_relocs.append(new_src)
        else:
            struct.pack_into(">I", new_data, new_src, 0)  # dangling pointer in slop
            dropped += 1

    # Append the new texture and repoint/reformat the ImageDesc.
    if new_blob is not None:
        pad = (-len(new_data)) & 31  # 32-align the texture for GX
        new_data.extend(b"\0" * pad)
        blob_new_off = len(new_data)
        new_data.extend(new_blob)
        id_new = remap[img_desc_off]
        struct.pack_into(">I", new_data, id_new + 0x00, blob_new_off)   # image ptr (reloc)
        struct.pack_into(">I", new_data, id_new + 0x08, GX_TF_RGB5A3)   # format
        new_relocs.append(id_new + 0x00)

    new_relocs.sort()
    if dropped:
        print(f"  dropped {dropped} relocs to out-of-range targets (zeroed in slop)")

    data_bytes = bytes(new_data)
    reloc_bytes = b"".join(struct.pack(">I", r) for r in new_relocs)
    public_bytes = struct.pack(">II", 0, 0)  # one public at data_off 0, name_off 0
    string_bytes = b"customItem\0"
    file_size = HSD_HEADER + len(data_bytes) + len(reloc_bytes) + len(public_bytes) + len(string_bytes)
    header = struct.pack(">IIIII4s8x", file_size, len(data_bytes), len(new_relocs), 1, 0, arc.version)

    with open(out_path, "wb") as f:
        f.write(header + data_bytes + reloc_bytes + public_bytes + string_bytes)
    print(f"  wrote {out_path} ({file_size / 1024:.1f} KB, public 'customItem')")


def main(argv):
    p = argparse.ArgumentParser(description="Carve a custom item .dat from Item.dat")
    p.add_argument("item_dat")
    p.add_argument("source_kind", type=int, help="ItemKind whose model to carve")
    p.add_argument("out_path")
    p.add_argument("name", help="display name")
    p.add_argument("--base-kind", type=int, default=None,
                   help="vanilla kind to clone behavior from (default: source_kind)")
    p.add_argument("--group", choices=GROUPS, default="good")
    p.add_argument("--weight-blue", type=int, default=10)
    p.add_argument("--weight-green", type=int, default=0)
    p.add_argument("--weight-red", type=int, default=0)
    # Per-event-source drop weights (Tac, meteor, broken structures, etc.).
    p.add_argument("--ev-dyna", type=int, default=0, help="Dyna Blade drop weight")
    p.add_argument("--ev-tac", type=int, default=0, help="Tac drop weight")
    p.add_argument("--ev-meteor", type=int, default=0, help="meteor drop weight")
    p.add_argument("--ev-destructible", type=int, default=0,
                   help="broken-structure drop weight (star pole / pillar / walls)")
    p.add_argument("--ev-chamber", type=int, default=0, help="secret-chamber drop weight")
    p.add_argument("--ev-ufo", type=int, default=0, help="UFO drop weight")
    p.add_argument("--texture", default=None,
                   help="PNG to re-encode (RGB5A3) into a model texture slot")
    p.add_argument("--texture-index", type=int, default=0,
                   help="which ImageDesc to replace on multi-texture models (default 0)")
    p.add_argument("--texture-fit", choices=("stretch", "cover", "contain"), default="stretch",
                   help="how to fit a non-matching aspect: stretch (distort), "
                        "cover (center-crop), contain (letterbox)")
    args = p.parse_args(argv[1:])

    base_kind = args.base_kind if args.base_kind is not None else args.source_kind
    weight_event = (args.ev_dyna, args.ev_tac, args.ev_meteor,
                    args.ev_destructible, args.ev_chamber, args.ev_ufo)
    print(f"Carving custom item '{args.name}' from {args.item_dat} kind {args.source_kind}:")
    carve(args.item_dat, args.source_kind, args.out_path, args.name, base_kind,
          GROUPS[args.group], (args.weight_blue, args.weight_green, args.weight_red),
          weight_event, args.texture, args.texture_index, args.texture_fit)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
