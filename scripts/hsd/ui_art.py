# SPDX-License-Identifier: GPL-3.0-only
"""The four art roles a character-indexed UI bank asks for, derived from renders.

Twenty-one TexAnims across eight archives take a machine picture, but they hold
only four distinct (width, height, format) combinations between them, and the
geometry is what says which role a bank plays. Every bank of a role wants the same
image, so two renders of a machine - a three-quarter hero view and a straight
top-down - produce all four.

| Role | Geometry | Where it is drawn |
|---|---|---|
| portrait | 64x64 CMPR | the character-select grid tile |
| picture | 80x48 C8 | the large art beside the CSS cursor and on the results screens |
| silhouette | 80x48 I4 | a soft white bloom drawn under the picture, same size and place |
| icon | 40x40 C4 | the time-attack board, and beside a player on the results and stadium-select screens |

The formats above are the donor frames'. An appended frame carries its own format,
so the encoders here emit RGB5A3 for the three color roles and I4 for the
silhouette, and neither a CMPR encoder nor a quantizer is needed.
"""

from PIL import Image, ImageFilter

from hsd.gx import GX_TF_CMPR, GX_TF_I4

GX_TF_C4 = 8
GX_TF_C8 = 9

BANK_ROLE = {
    (64, 64, GX_TF_CMPR): "portrait",
    (80, 48, GX_TF_C8): "picture",
    (80, 48, GX_TF_I4): "silhouette",
    (40, 40, GX_TF_C4): "icon",
}

ROLE_SIZE = {
    "portrait": (64, 64),
    "picture": (80, 48),
    "silhouette": (80, 48),
    "icon": (40, 40),
}

# Measured off frame 4 of the portrait banks: a flat warm gray darkening along the
# bottom edge, where a soft contact shadow pools under the machine.
PORTRAIT_BACK = (106, 107, 102)
PORTRAIT_SHADOW = (79, 80, 84)

# How far a pixel travels from a keyed backdrop before it counts as fully
# covered. The pastels a machine is painted in clear it several times over, so
# only a glow's own falloff lands inside it.
MATTE_SOFTNESS = 64

# The silhouette is the picture's own alpha under a small blur. Fitting
# dilate/blur/gain against frame 4 lands on no dilation and this sigma, which both
# matches the vanilla look and keeps the two layers registered.
SILHOUETTE_SIGMA = 1.5


def key_matte(im, back):
    """Lift a flat viewport backdrop to alpha.

    A render taken off a model viewer carries no alpha of its own, and the part
    that needs it most - the additive glow around a machine's pods - fades into
    the backdrop rather than ending at an edge. Coverage is taken from how far a
    pixel travels from the backdrop colour, and the backdrop's share is then
    divided back out, which recovers that falloff instead of the hard cut a
    colour-equality key leaves.
    """
    px = im.load()
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    dst = out.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, _ = px[x, y]
            a = max(abs(r - back[0]), abs(g - back[1]), abs(b - back[2]))
            a = min(255, round(a * 255 / MATTE_SOFTNESS))
            if not a:
                continue
            f = a / 255.0
            dst[x, y] = tuple(
                min(255, max(0, round((c - (1.0 - f) * bc) / f)))
                for c, bc in zip((r, g, b), back)
            ) + (a,)
    return out


def trim(im, threshold=8):
    """`im` cropped to what it actually draws, so framing sees the subject and
    not the empty margin a render is taken with."""
    mask = im.getchannel("A").point(lambda v: 255 if v >= threshold else 0)
    box = mask.getbbox()
    return im.crop(box) if box else im


def contain(im, w, h, margin=0.0):
    """`im` scaled to fit (w, h) with its aspect kept, centred on a clear canvas.

    `margin` is the fraction of each axis left empty around it, so 0 lets the art
    touch the edges and 0.1 insets it by a tenth."""
    box_w = max(1, round(w * (1.0 - margin)))
    box_h = max(1, round(h * (1.0 - margin)))
    scale = min(box_w / im.width, box_h / im.height)
    size = (max(1, round(im.width * scale)), max(1, round(im.height * scale)))

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.paste(im.resize(size, Image.LANCZOS), ((w - size[0]) // 2, (h - size[1]) // 2))
    return out


def _shadow(alpha, w, h):
    """The machine's alpha squashed flat and blurred, pooled at the bottom edge."""
    squashed = alpha.resize((w, max(1, h // 4)), Image.LANCZOS)
    out = Image.new("L", (w, h), 0)
    out.paste(squashed, (0, h - squashed.height))
    return out.filter(ImageFilter.GaussianBlur(2.0))


def portrait(hero, w, h):
    """The grid tile: the machine over a synthesized backdrop, flattened opaque.

    A vanilla tile fits its machine inside the square rather than filling it -
    the Slick Star's spans the width and leaves half the height as backdrop -
    so a wide machine keeps all of itself instead of being cropped to a detail."""
    art = contain(hero, w, h, margin=0.08)
    back = Image.new("RGBA", (w, h), PORTRAIT_BACK + (255,))
    back.paste(
        Image.new("RGBA", (w, h), PORTRAIT_SHADOW + (255,)),
        (0, 0),
        _shadow(art.getchannel("A"), w, h),
    )
    back.alpha_composite(art)
    return back


def silhouette(picture_im, w, h):
    """The bloom under the picture: its alpha blurred, carried as alpha again."""
    blurred = picture_im.getchannel("A").filter(
        ImageFilter.GaussianBlur(SILHOUETTE_SIGMA)
    )
    out = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    out.putalpha(blurred)
    return out


def icon(topdown, w, h):
    """The small machine icon: a hard black outline inside a white halo."""
    art = contain(topdown, w, h, margin=0.25)
    alpha = art.getchannel("A")
    outline = alpha.filter(ImageFilter.MaxFilter(3))
    halo = outline.filter(ImageFilter.MaxFilter(3))

    out = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    out.paste(Image.new("RGBA", (w, h), (255, 255, 255, 255)), (0, 0), halo)
    out.paste(Image.new("RGBA", (w, h), (0, 0, 0, 255)), (0, 0), outline)
    out.alpha_composite(art)
    return out


def role_images(hero, topdown):
    """Every role's image at its target size, from a hero and a top-down render."""
    hero, topdown = trim(hero), trim(topdown)
    pw, ph = ROLE_SIZE["picture"]
    picture_im = contain(hero, pw, ph)
    return {
        "portrait": portrait(hero, *ROLE_SIZE["portrait"]),
        "picture": picture_im,
        "silhouette": silhouette(picture_im, pw, ph),
        "icon": icon(topdown, *ROLE_SIZE["icon"]),
    }


def contact_sheet(images, zoom=6, back=(128, 128, 132)):
    """The four role images magnified side by side over a mid gray, for eyeballing
    what a build is about to ship at 40x40 and 64x64."""
    pad = zoom * 2
    tiles = [
        images[r].resize(
            (images[r].width * zoom, images[r].height * zoom), Image.NEAREST
        )
        for r in ("portrait", "picture", "silhouette", "icon")
    ]
    out = Image.new(
        "RGBA",
        (
            sum(t.width for t in tiles) + pad * (len(tiles) + 1),
            max(t.height for t in tiles) + pad * 2,
        ),
        back + (255,),
    )
    x = pad
    for t in tiles:
        out.alpha_composite(t, (x, pad))
        x += t.width + pad
    return out
