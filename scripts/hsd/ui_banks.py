# SPDX-License-Identifier: GPL-3.0-only
"""Character-indexed UI banks: finding them, and growing their ramps.

The select-screen portraits, name plates, results art, the small machine icon and
the time-attack board are all TexAnims whose animation frame *is* the
CharacterKind: the AObj carries one CON key per frame whose value is the frame
number, so frame N selects image N, and a hold key at frame 300 pins the last one.
The frame count is baked into the TexAnim header, so a 21st character has no art
until every such bank grows.

Three shapes of bank qualify. Most hold one image per character, so an image count
equal to the roster finds them. Some hold one image per character *colour* - the
engine diverts King Dedede to frame 20 + colour and Meta Knight to 30 + colour - so
they are wider than the roster and are found by those diverts sitting on their ramp
instead. The last shape stops two short of the roster: the small machine icon on the
results screens and the stadium select is reached through an accessor that never
hands back King Dedede's kind or Meta Knight's, so those banks cover only the
eighteen kinds with a machine of their own.

The Sicon models size their quad per character - the pictures are not all the same
shape, King Dedede's being 56x80 and Meta Knight's 104x52 against Slick Star's
80x48 - through a pair of AnimJoint scale tracks on the same timeline, which
`joint_ramp_offsets` finds by the same divert shape.
"""

import struct

from .archive import u16, u32
from .fobj import (
    FMT_S8,
    FMT_S16,
    FMT_U8,
    FMT_U16,
    OP_CON,
    FObjError,
    Key,
    decode,
    encode,
)
from .gx import GX_TF_I4, GX_TF_RGB5A3, encode_i4_alpha, encode_rgb5a3
from .schema import array_length, root_for
from .symbols import classify_symbol
from .walker import Walker

GX_TF_C4 = 8
GX_TF_C8 = 9
GX_TF_CMPR = 14

# The two CharacterKinds the engine keeps out of the roster's own frame run, and
# the frames it diverts them to instead - each plus colour.
DEDEDE_KIND = 18
METAKNIGHT_KIND = 19
DEDEDE_FRAME = 20
METAKNIGHT_FRAME = 30

# The roster's own frames run 0..ROSTER_FRAME-1. An appended CharacterKind's frame
# is the kind itself, so appended frames start here and the diverts move up.
ROSTER_FRAME = 20

# The CharacterKinds with a machine of their own, which run up to King Dedede's.
MACHINE_KIND_NUM = DEDEDE_KIND

TEXANIM_AOBJ = 0x08
TEXANIM_IMAGES = 0x0C
TEXANIM_TLUTS = 0x10
TEXANIM_N_IMAGES = 0x14
TEXANIM_N_TLUTS = 0x16

AOBJ_TRACKS = 0x08
ANIMJOINT_AOBJ = 0x08

FOBJ_NEXT = 0x00
FOBJ_LENGTH = 0x04
FOBJ_TRACK = 0x0C
FOBJ_VALUE_FLAG = 0x0D
FOBJ_TAN_FLAG = 0x0E
FOBJ_BUFFER = 0x10

# HSD_A_T_TIMG / HSD_A_T_TCLT: the image-index and TLUT-index tracks.
TRACK_TIMG = 1
TRACK_TCLT = 10


def has_divert_shape(data, fobj):
    """Whether the ramp is keyed by CharacterKind through the colour diverts: a key
    for each character up to King Dedede, none on his kind or Meta Knight's, and one
    on each of the frames they are diverted to. Plenty of ramps key frames 20 and 30
    without being anyone's, so the whole shape has to match."""
    frames = {int(k.frame) for k in ramp_keys(data, fobj)}
    return (
        frames.issuperset(range(DEDEDE_KIND))
        and frames.isdisjoint((DEDEDE_KIND, METAKNIGHT_KIND))
        and frames.issuperset((DEDEDE_FRAME, METAKNIGHT_FRAME))
    )


def has_prefix_shape(data, fobj, n_images):
    """Whether the ramp is one key per image, each selecting the image its own frame
    number names. On its own this fits any strip animation, so the caller pairs it
    with an image count that only a machine icon bank holds."""
    real = ramp_keys(data, fobj)[:-1]
    return len(real) == n_images and all(
        int(k.frame) == i and int(k.value) == i for i, k in enumerate(real)
    )


def is_character_bank(data, tex, n_frames):
    """Whether the bank is indexed by CharacterKind. Either it holds one image per
    character, or one per character colour - which makes it wider than the roster and
    leaves the diverts' shape on its image-index ramp - or one per kind with a machine
    of its own, which is what the small machine icon banks hold."""
    n_images = u16(data, tex + TEXANIM_N_IMAGES)
    if n_images == n_frames:
        return True
    for fobj in track_chain(data, u32(data, tex + TEXANIM_AOBJ)):
        if data[fobj + FOBJ_TRACK] == TRACK_TIMG:
            return has_divert_shape(data, fobj) or (
                n_images == MACHINE_KIND_NUM and has_prefix_shape(data, fobj, n_images)
            )
    return False


def joint_ramp_offsets(arc):
    """Every joint ramp keyed by CharacterKind.

    The Sicon models scale their quad per character, because the pictures are not
    all the same shape - King Dedede's is 56x80 and Meta Knight's 104x52 against
    Slick Star's 80x48 - and that scale is a pair of AnimJoint tracks sharing the
    image ramp's timeline. They have to grow with it or an appended character draws
    at whatever shape the frame it displaced used to hold."""
    out = []
    for off, (kind, _) in reachable(arc).items():
        if kind != "AnimJoint":
            continue
        for fobj in track_chain(arc.data, u32(arc.data, off + ANIMJOINT_AOBJ)):
            try:
                if has_divert_shape(arc.data, fobj):
                    out.append(fobj)
            except FObjError:
                continue
    return sorted(set(out))


def ramp_value_at(data, fobj, frame):
    """The ramp's value on `frame`, which has to be one of its own keys."""
    for k in ramp_keys(data, fobj):
        if int(k.frame) == frame:
            return k.value
    raise SystemExit(f"  ramp @ {fobj:#x}: no key on frame {frame}")


def reachable(arc):
    """Every typed offset reachable from any of the archive's publics."""
    walker = Walker(arc)
    for sym, off in arc.publics.items():
        root_type, is_array = root_for(classify_symbol(sym))
        try:
            if is_array:
                for i in range(array_length(arc, off)):
                    walker.walk(u32(arc.data, off + i * 4), root_type)
            else:
                walker.walk(off, root_type)
        except (struct.error, IndexError, RecursionError, KeyError):
            continue
    return walker.visited


def texanim_offsets(arc, n_frames):
    """Every character-indexed TexAnim reachable from a public."""
    return sorted(
        off
        for off, (t, _) in reachable(arc).items()
        if t == "TexAnim" and is_character_bank(arc.data, off, n_frames)
    )


def fit(im, w, h):
    """`im` scaled to fit (w, h) with its aspect kept, centred on a clear canvas."""
    from PIL import Image

    if im.size == (w, h):
        return im
    scale = min(w / im.width, h / im.height)
    size = (max(1, round(im.width * scale)), max(1, round(im.height * scale)))
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.paste(im.resize(size, Image.LANCZOS), ((w - size[0]) // 2, (h - size[1]) // 2))
    return out


def encode_art(im, w, h, fmt):
    """The new frame's pixels, or None when the bank's format has no encoder."""
    scaled = fit(im, w, h)
    if fmt in (GX_TF_CMPR, GX_TF_C4, GX_TF_C8):
        return GX_TF_RGB5A3, encode_rgb5a3(scaled)
    if fmt == GX_TF_I4:
        return GX_TF_I4, encode_i4_alpha(scaled)
    return None, None


def track_chain(data, aobj):
    """The AObj's FOBJDesc offsets, in list order."""
    offs = []
    off = u32(data, aobj + AOBJ_TRACKS) if aobj else 0
    while off:
        offs.append(off)
        off = u32(data, off + FOBJ_NEXT)
    return offs


def ramp_keys(data, fobj):
    """The track's keys, decoded."""
    buf_off = u32(data, fobj + FOBJ_BUFFER)
    return decode(
        bytes(data[buf_off : buf_off + u32(data, fobj + FOBJ_LENGTH)]),
        data[fobj + FOBJ_VALUE_FLAG],
        data[fobj + FOBJ_TAN_FLAG],
    )


def extended_keys(data, fobj, values):
    """The ramp's keys with one key per entry of `values` added at frame ROSTER_FRAME.

    An appended CharacterKind's frame is the kind itself, so the new frames start
    where the roster ends and everything already keyed there has to move out of the
    way: the colour diverts - King Dedede at frame 20 + colour, Meta Knight at
    30 + colour - both slide as far as the frames added, which the mod matches by
    rewriting the two addi immediates that form them. The trailing hold key keeps
    its own frame and takes whatever value ends up before it."""
    appended = len(values)
    keys = ramp_keys(data, fobj)
    hold, real = keys[-1], keys[:-1]

    out = [k for k in real if k.frame < ROSTER_FRAME]
    out += [
        Key(float(ROSTER_FRAME + i), float(v), 0.0, OP_CON)
        for i, v in enumerate(values)
    ]
    out += [
        Key(k.frame + appended, k.value, k.tan, k.op)
        for k in real
        if k.frame >= ROSTER_FRAME
    ]
    if out[-1].frame >= hold.frame:
        raise SystemExit(f"  ramp @ {fobj:#x}: {appended} frames run into the hold key")
    out.append(Key(hold.frame, out[-1].value, hold.tan, hold.op))
    return out


def index_values(first_value, appended):
    """What an image- or TLUT-index ramp's appended keys select: the entries the
    table grew by, in order."""
    return [first_value + i for i in range(appended)]


VALUE_LIMIT = {FMT_S16: 0x7FFF, FMT_U16: 0xFFFF, FMT_S8: 0x7F, FMT_U8: 0xFF}


def fitted_flag(value_flag, keys):
    """The track's value flag with its fixed-point exponent lowered until the keys
    fit. A vanilla ramp is a u8 over a 2^3 divisor, which tops out at 31.875 and so
    cannot hold an image index past 31; the values are whole numbers, so dropping
    the exponent costs nothing and keeps the format - and the buffer length - as
    they were."""
    fmt = value_flag & 0xE0
    limit = VALUE_LIMIT.get(fmt)
    if limit is None:
        return value_flag

    top = max(abs(k.value) for k in keys)
    exp = value_flag & 0x1F
    while exp > 0 and round(top * (1 << exp)) > limit:
        exp -= 1
    if round(top * (1 << exp)) > limit:
        raise SystemExit(f"  ramp value {top:g} does not fit format {fmt:#x}")
    return fmt | exp


def encoded_ramp(data, fobj, values):
    """The extended ramp as an FObj keyframe buffer, with the value flag it needs."""
    keys = extended_keys(data, fobj, values)
    flag = fitted_flag(data[fobj + FOBJ_VALUE_FLAG], keys)
    return encode(keys, flag, data[fobj + FOBJ_TAN_FLAG]), flag
