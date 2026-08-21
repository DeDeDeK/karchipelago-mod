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

ROLE_SIZE = {"portrait": (64, 64), "picture": (80, 48),
             "silhouette": (80, 48), "icon": (40, 40)}

# Measured off frame 4 of the portrait banks: a flat warm gray darkening along the
# bottom edge, where a soft contact shadow pools under the machine.
PORTRAIT_BACK = (106, 107, 102)
PORTRAIT_SHADOW = (79, 80, 84)

# The silhouette is the picture's own alpha under a small blur. Fitting
# dilate/blur/gain against frame 4 lands on no dilation and this sigma, which both
# matches the vanilla look and keeps the two layers registered.
SILHOUETTE_SIGMA = 1.5


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


def cover(im, w, h, zoom=1.0):
    """`im` scaled to fill (w, h), centred and cropped - the vanilla portrait crop,
    which is tight enough to clip the machine on at least one side."""
    scale = max(w / im.width, h / im.height) * zoom
    size = (max(1, round(im.width * scale)), max(1, round(im.height * scale)))
    scaled = im.resize(size, Image.LANCZOS)

    left = (size[0] - w) // 2
    top = (size[1] - h) // 2
    return scaled.crop((left, top, left + w, top + h))


def _shadow(alpha, w, h):
    """The machine's alpha squashed flat and blurred, pooled at the bottom edge."""
    squashed = alpha.resize((w, max(1, h // 4)), Image.LANCZOS)
    out = Image.new("L", (w, h), 0)
    out.paste(squashed, (0, h - squashed.height))
    return out.filter(ImageFilter.GaussianBlur(2.0))


def portrait(hero, w, h):
    """The grid tile: the machine over a synthesized backdrop, flattened opaque."""
    art = cover(hero, w, h, zoom=1.05)
    back = Image.new("RGBA", (w, h), PORTRAIT_BACK + (255,))
    back.paste(Image.new("RGBA", (w, h), PORTRAIT_SHADOW + (255,)),
               (0, 0), _shadow(art.getchannel("A"), w, h))
    back.alpha_composite(art)
    return back


def silhouette(picture_im, w, h):
    """The bloom under the picture: its alpha blurred, carried as alpha again."""
    blurred = picture_im.getchannel("A").filter(
        ImageFilter.GaussianBlur(SILHOUETTE_SIGMA))
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
    pw, ph = ROLE_SIZE["picture"]
    picture_im = contain(hero, pw, ph)
    return {
        "portrait": portrait(hero, *ROLE_SIZE["portrait"]),
        "picture": picture_im,
        "silhouette": silhouette(picture_im, pw, ph),
        "icon": icon(topdown, *ROLE_SIZE["icon"]),
    }
