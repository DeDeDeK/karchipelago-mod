# SPDX-License-Identifier: GPL-3.0-only
"""Each player's position in a running Dolphin, described against the stage
data (`stage.py live`). Read-only. Needs dolphin-memory-engine
(`uv run --with dolphin-memory-engine python ...`).

Air Ride, City Trial and the stadiums keep players in `stc_playerdata`
(0x8055A9F0, five 0x90C-byte PlayerData slots). Each slot names its rider
GObj (+0x3C); the rider names the machine it is on (RiderData+0x3F4, NULL on
foot). Top Ride keeps its own Kirby manager and a 2D course frame, which this
reader reports as is. Positions are world units, the frame `world.Stage`
bakes the stage into.
"""

import math
import os
import struct
import time

R13 = 0x805DD0E0
STC_GROBJ = R13 + 0x5EC  # GrObj *, NULL outside a 3D scene
STC_TOPRIDE_KIRBYMGR = R13 + 0xA64
GR_FILE_TABLE = 0x804A2FFC  # per GroundKind, 0x14 bytes, word 0 the file name
STC_PLAYERDATA = 0x8055A9F0
PLAYERDATA_SIZE = 0x90C
PLAYER_SLOTS = 5
PKIND_HMN, PKIND_NONE = 0, 4
TOPRIDE_CPU_READER_VTABLE = 0x804D8710


def _ok(p):
    return 0x80000000 <= p < 0x81800000


def _s32(d, a):
    return struct.unpack(">i", d.read_bytes(a, 4))[0]


def _v3(d, a):
    return struct.unpack(">3f", d.read_bytes(a, 12))


def _yaw(f):
    return math.degrees(math.atan2(f[0], f[2]))


def _userdata(d, gobj):
    """A GObj's user data (+0x2C), only when it points back at the GObj."""
    if not _ok(gobj):
        return 0
    u = d.read_word(gobj + 0x2C)
    return u if _ok(u) and d.read_word(u) == gobj else 0


def read_stage(d):
    gr = d.read_word(STC_GROBJ)
    if not _ok(gr):
        return None
    kind = _s32(d, gr + 0x04)  # GrObj.gr_kind
    name_ptr = d.read_word(GR_FILE_TABLE + kind * 0x14)
    name = d.read_bytes(name_ptr, 0x20).split(b"\0")[0].decode() if _ok(name_ptr) else None
    return dict(ground_kind=kind, stem=name and os.path.splitext(name)[0])


def read_players(d):
    mgr = d.read_word(STC_TOPRIDE_KIRBYMGR)
    if _ok(mgr):
        out = []
        for i in range(4):
            k = d.read_word(mgr + 0x08 + 4 * i)  # kirbys[i]
            if not _ok(k):
                continue
            reader = d.read_word(k + 0x48)
            out.append(dict(slot=d.read_byte(k + 0x0C), topride=True,
                            cpu=_ok(reader) and d.read_word(reader) == TOPRIDE_CPU_READER_VTABLE,
                            on_machine=True, pos=_v3(d, k + 0x88), yaw=_yaw(_v3(d, k + 0x94))))
        return out
    if not _ok(d.read_word(STC_GROBJ)):
        return []
    out = []
    for slot in range(PLAYER_SLOTS):
        pd = STC_PLAYERDATA + slot * PLAYERDATA_SIZE
        pkind = _s32(d, pd)  # PlayerData.player_kind
        rd = _userdata(d, d.read_word(pd + 0x3C))  # rider_gobj
        if pkind == PKIND_NONE or not rd:
            continue
        md = _userdata(d, d.read_word(rd + 0x3F4))  # RiderData.machine_gobj
        p = dict(slot=slot, topride=False, cpu=pkind != PKIND_HMN, on_machine=bool(md))
        if md:
            p.update(pos=_v3(d, md + 0x3E8), yaw=_yaw(_v3(d, md + 0x418)),  # MachineData pos, forward
                     ground_tri=_s32(d, md + 0x714), ground_type=_s32(d, md + 0x734))
        else:
            p.update(pos=_v3(d, rd + 0x300), yaw=_yaw(_v3(d, rd + 0x324)),  # RiderData pos, forward
                     ground_tri=_s32(d, rd + 0x640), ground_type=_s32(d, rd + 0x644))
        out.append(p)
    return out


def report(dme, stages):
    from hsd.stage import describe_point, fmt
    from hsd.world import Stage

    info = read_stage(dme)
    if not info or not info["stem"]:
        print("no 3D stage loaded")
        return None, []
    stem = info["stem"]
    if stem not in stages:
        stages[stem] = Stage(stem)
    s = stages[stem]
    players = read_players(dme)
    print(f"{stem} (ground kind {info['ground_kind']}), {len(players)} player(s)")
    for p in players:
        who = f"P{p['slot'] + 1}" + (" cpu" if p["cpu"] else "")
        mode = "machine" if p["on_machine"] else "on foot"
        print(f"{who} {mode} {fmt(p['pos'])} heading {p['yaw']:.0f}"
              + (f" ground type {p['ground_type']} tri {p['ground_tri']}" if "ground_type" in p else ""))
        if not p["topride"]:
            print("\n".join(describe_point(s, p["pos"])))
    return s, players


def main(args):
    import dolphin_memory_engine as dme

    from hsd.world import append_landmark

    dme.hook()
    if not dme.is_hooked():
        status = dme.get_status()
        raise SystemExit(f"Dolphin not hooked ({getattr(status, "name", status)}): start the game in Dolphin first")
    stages = {}
    while True:
        s, players = report(dme, stages)
        if args.capture:
            if s is None:
                raise SystemExit("nothing captured: no stage loaded")
            p = next((q for q in players if q["slot"] == args.player), None)
            if p is None:
                raise SystemExit(f"nothing captured: no player in slot {args.player}")
            x, y, z = p["pos"]
            append_landmark(dict(stage=s.stem, name=args.capture, shape=args.shape,
                                 x=f"{x:.1f}", y=f"{y:.1f}", z=f"{z:.1f}",
                                 r=f"{args.r:g}" if args.shape in ("sphere", "cylinder") else "",
                                 hx=f"{args.r:g}" if args.shape == "box" else "",
                                 hy=f"{args.hy:g}" if args.shape in ("cylinder", "box") else "",
                                 hz=f"{args.r:g}" if args.shape == "box" else "",
                                 source="live", note=args.note))
            print(f"added landmark {args.capture!r} at ({x:.1f}, {y:.1f}, {z:.1f})")
            return
        if not args.watch:
            return
        time.sleep(args.watch)
        print()
