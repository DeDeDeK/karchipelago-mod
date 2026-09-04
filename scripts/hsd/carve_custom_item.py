"""Custom item archive carver.

Carves an item model subtree out of `iso/files/Item.dat` and packs it into a
minimal standalone archive that exports a single public `customItem` (a
`CustomItemDesc`, see mods/custom_items/include/custom_items_api.h). The
descriptor's `model` field is relocated to point at the carved JOBJDesc root, so
the custom_items mod can splice the model into the live item table.

`Item.dat` exposes one public `itData`: an array of 69 itData structs (0x18
stride). For kind K, `itData[K].model` (at +0x08) points at a `{JOBJ* j; int flags}`
pair whose `j` is the model's JOBJDesc root (and whose `flags` we carry into the
descriptor's model_flag). We walk that subtree with the type-aware walker, then
emit:

    new_data[0x00 .. 0x37] : CustomItemDesc
    new_data[0x38 ..      ] : name string, then the carved model ranges

The descriptor's `model` (and optional `effect_info`) are synthetic relocations:
they point into the carved ranges but have no source pointer in the donor.

Usage:
    uv run python scripts/hsd/carve_custom_item.py \
        iso/files/Item.dat <source_kind> <out.dat> <name> \
        [--base-kind K] [--scale F] [--no-effect] \
        [--weight-blue N] [--weight-green N] [--weight-red N]

`source_kind` selects the model to carve (e.g. 3 = Boost patch, 28 = Bomb copy
panel). `--base-kind` is the vanilla kind the custom item clones behavior from
(default: the source kind), so by default the carved item behaves like the kind
it was carved from, just under a new kind number with author-set spawn weights.
`--scale` multiplies the render size when the carved model's native size differs
from the base kind's (e.g. a legendary model on a flat-panel base). The item's
BAD/GOOD/FAKE group follows base_kind, so choose a base_kind in the intended
family rather than setting a group directly. `--no-effect` overrides the base
kind's stat grant with an empty effect record, keeping the look and feel of a
patch without any of its effect.
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, build_archive, u16, u32
from hsd.gx import FORMAT_NAME, GX_TF_RGB5A3, align32, encode_rgb5a3
from hsd.walker import Walker, carve_ranges

# Must match include/custom_items_api.h.
CUSTOM_ITEM_MAGIC = 0x4349544D  # 'CITM'
CUSTOM_ITEM_DESC_VERSION = 3
DESC_SIZE = 0x38
ITDATA_STRIDE = 0x18


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
        rs = im.resize(
            (max(1, round(sw * scale)), max(1, round(sh * scale))), Image.LANCZOS
        )
        left, top = (rs.width - tw) // 2, (rs.height - th) // 2
        return rs.crop((left, top, left + tw, top + th))
    # contain
    scale = min(tw / sw, th / sh)
    rs = im.resize(
        (max(1, round(sw * scale)), max(1, round(sh * scale))), Image.LANCZOS
    )
    out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    out.paste(rs, ((tw - rs.width) // 2, (th - rs.height) // 2))
    return out


def carve(
    item_dat,
    source_kind,
    out_path,
    name,
    base_kind,
    scale,
    weight_box,
    weight_event,
    texture_png,
    texture_index,
    texture_fit,
    effect_group,
):
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
    print(
        f"  kind {source_kind}: model pair={model_pair:#x} -> JOBJDesc={root_jobj:#x} flag={model_flag:#010x}"
    )

    visited = Walker(arc).walk(root_jobj, "JOBJDesc")

    # Enumerate the model's textures (ImageDescs), in data order, so a custom
    # texture can target a specific slot on multi-material models.
    img_descs = sorted(o for o, (t, _) in visited.items() if t == "ImageDesc")
    for idx, o in enumerate(img_descs):
        tw, th = u16(arc.data, o + 0x04), u16(arc.data, o + 0x06)
        fmt = u32(arc.data, o + 0x08)
        print(f"  texture[{idx}]: {tw}x{th} {FORMAT_NAME.get(fmt, fmt)}")

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
            raise SystemExit(
                f"--texture-index {texture_index} out of range "
                f"(model has {len(img_descs)} texture(s))"
            )
        img_desc_off = img_descs[texture_index]
        blob_off = u32(arc.data, img_desc_off + 0x00)
        tw = u16(arc.data, img_desc_off + 0x04)
        th = u16(arc.data, img_desc_off + 0x06)
        from PIL import Image

        im = fit_image(Image.open(texture_png).convert("RGBA"), tw, th, texture_fit)
        new_blob = encode_rgb5a3(im)
        del visited[blob_off]
        print(
            f"  replacing texture[{texture_index}] -> {tw}x{th} RGB5A3 "
            f"({texture_fit}, {len(new_blob)} bytes)"
        )

    # Layout: descriptor, then name string (4-aligned), then carved ranges.
    name_bytes = name.encode("ascii") + b"\0"
    name_off = DESC_SIZE
    prefix_len = (name_off + len(name_bytes) + 3) & ~3  # 4-align before the ranges
    prefix = bytearray(prefix_len)
    prefix[name_off : name_off + len(name_bytes)] = name_bytes

    # If a texture is being replaced, its ImageDesc image pointer is
    # rewritten below - keep carve_ranges from translating it here.
    skip = (img_desc_off,) if img_desc_off is not None else ()
    res = carve_ranges(arc, visited, prefix, base_relocs=(0x08, 0x14), skip_relocs=skip)
    new_data, remap = res.data, res.remap
    print(
        f"  reached {len(visited)} objects, kept {len(res.intervals)} ranges, "
        f"{len(remap) / 1024:.1f} KB"
    )
    if res.dropped:
        print(
            f"  dropped {res.dropped} relocs to out-of-range targets (zeroed in slop)"
        )

    # Fill in the descriptor (big-endian, matching CustomItemDesc).
    struct.pack_into(">I", new_data, 0x00, CUSTOM_ITEM_MAGIC)
    struct.pack_into(">H", new_data, 0x04, CUSTOM_ITEM_DESC_VERSION)
    struct.pack_into(">H", new_data, 0x06, 0)
    struct.pack_into(">I", new_data, 0x08, name_off)  # name (reloc)
    struct.pack_into(">i", new_data, 0x0C, base_kind)
    struct.pack_into(">i", new_data, 0x10, 0)  # reserved (group follows base_kind)
    struct.pack_into(">I", new_data, 0x14, remap[root_jobj])  # model (reloc)
    struct.pack_into(">I", new_data, 0x18, 0)  # effect_info (inherit base)
    struct.pack_into(">HHH", new_data, 0x1C, *weight_box)  # weight_box[3]
    struct.pack_into(">H", new_data, 0x22, 0)  # weight_free (reserved)
    struct.pack_into(">HHHHHH", new_data, 0x24, *weight_event)  # weight_event[6]
    struct.pack_into(">I", new_data, 0x30, model_flag)  # model_flag (v2)
    struct.pack_into(">f", new_data, 0x34, scale)  # scale (v3)

    new_relocs = res.relocs

    # A zero-entry PatchEffectInfo: Machine_OnTouchItem applies grants by walking
    # this record, so an empty one grants nothing while the item keeps the base
    # kind's model class, state script, pickup reaction and SFX.
    if effect_group is not None:
        new_data.extend(b"\0" * (-len(new_data) & 3))
        effect_off = len(new_data)
        new_data.extend(struct.pack(">iii", 0, 0, effect_group))
        struct.pack_into(">I", new_data, 0x18, effect_off)
        new_relocs.append(0x18)
        print(f"  effect override: 0 entries, group {effect_group}")

    # Append the new texture and repoint/reformat the ImageDesc.
    if new_blob is not None:
        new_data.extend(b"\0" * align32(len(new_data)))  # 32-align the texture for GX
        blob_new_off = len(new_data)
        new_data.extend(new_blob)
        id_new = remap[img_desc_off]
        struct.pack_into(
            ">I", new_data, id_new + 0x00, blob_new_off
        )  # image ptr (reloc)
        struct.pack_into(">I", new_data, id_new + 0x08, GX_TF_RGB5A3)  # format
        new_relocs.append(id_new + 0x00)

    out = build_archive(new_data, new_relocs, [("customItem", 0)], arc.version)
    with open(out_path, "wb") as f:
        f.write(out)
    print(f"  wrote {out_path} ({len(out) / 1024:.1f} KB, public 'customItem')")


def main(argv):
    p = argparse.ArgumentParser(description="Carve a custom item .dat from Item.dat")
    p.add_argument("item_dat")
    p.add_argument("source_kind", type=int, help="ItemKind whose model to carve")
    p.add_argument("out_path")
    p.add_argument("name", help="display name")
    p.add_argument(
        "--base-kind",
        type=int,
        default=None,
        help="vanilla kind to clone behavior from (default: source_kind)",
    )
    p.add_argument(
        "--scale",
        type=float,
        default=1.0,
        help="render-scale multiplier over the base kind's native size "
        "(1.0 = inherit; raise if a carved model renders too small)",
    )
    p.add_argument("--weight-blue", type=int, default=10)
    p.add_argument("--weight-green", type=int, default=0)
    p.add_argument("--weight-red", type=int, default=0)
    # Per-event-source drop weights (Tac, meteor, broken structures, etc.).
    p.add_argument("--ev-dyna", type=int, default=0, help="Dyna Blade drop weight")
    p.add_argument("--ev-tac", type=int, default=0, help="Tac drop weight")
    p.add_argument("--ev-meteor", type=int, default=0, help="meteor drop weight")
    p.add_argument(
        "--ev-destructible",
        type=int,
        default=0,
        help="broken-structure drop weight (star pole / pillar / walls)",
    )
    p.add_argument(
        "--ev-chamber", type=int, default=0, help="secret-chamber drop weight"
    )
    p.add_argument("--ev-ufo", type=int, default=0, help="UFO drop weight")
    p.add_argument(
        "--no-effect",
        action="store_true",
        help="override the base kind's stat grant with an empty "
        "PatchEffectInfo, so pickup grants nothing",
    )
    p.add_argument(
        "--effect-group",
        type=int,
        default=1,
        choices=(0, 1, 2),
        help="ItemGroup carried by --no-effect's record: 0 BAD, 1 GOOD, 2 FAKE",
    )
    p.add_argument(
        "--texture",
        default=None,
        help="PNG to re-encode (RGB5A3) into a model texture slot",
    )
    p.add_argument(
        "--texture-index",
        type=int,
        default=0,
        help="which ImageDesc to replace on multi-texture models (default 0)",
    )
    p.add_argument(
        "--texture-fit",
        choices=("stretch", "cover", "contain"),
        default="stretch",
        help="how to fit a non-matching aspect: stretch (distort), "
        "cover (center-crop), contain (letterbox)",
    )
    args = p.parse_args(argv[1:])

    base_kind = args.base_kind if args.base_kind is not None else args.source_kind
    weight_event = (
        args.ev_dyna,
        args.ev_tac,
        args.ev_meteor,
        args.ev_destructible,
        args.ev_chamber,
        args.ev_ufo,
    )
    # Box/sky pool chances are stored as u8 in the engine, so they saturate at
    # 255 (weights are relative - values well under 255 are the norm).
    for label, w in (
        ("blue", args.weight_blue),
        ("green", args.weight_green),
        ("red", args.weight_red),
    ):
        if w > 255:
            print(
                f"  warning: --weight-{label} {w} exceeds 255; the engine clamps box weights to 255"
            )
    print(
        f"Carving custom item '{args.name}' from {args.item_dat} kind {args.source_kind}:"
    )
    carve(
        args.item_dat,
        args.source_kind,
        args.out_path,
        args.name,
        base_kind,
        args.scale,
        (args.weight_blue, args.weight_green, args.weight_red),
        weight_event,
        args.texture,
        args.texture_index,
        args.texture_fit,
        args.effect_group if args.no_effect else None,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
