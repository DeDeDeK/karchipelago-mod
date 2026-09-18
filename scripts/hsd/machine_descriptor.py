r"""Custom machine descriptors: the `customMachine` public of a machine archive.

A custom machine archive exports its engine vcData public and `customMachine`, a
CustomMachineDesc (mods/custom_machines/src/custom_machine_desc.h). This module is
the Python side of that struct. `read` parses one into a dict, `append` writes one
onto the end of a data section, and `rewrite` replaces the one in an archive. A
descriptor's strings, rows and generator programs are written right after it,
so a rewrite drops that tail and appends a fresh one; the machine's own data never moves.

The descriptor carries everything the engine keeps per machine outside a machine
archive:

- Stat rows, each a (low, high) pair - low weighed by a stat below zero patches, high
  by one above - in the machine's class's order:

      star  top_speed_ground, top_speed_air         multipliers on the two top speeds
            weight_air_impulse, glide_air_impulse   added to handling air_impulse
            weight_air_recover, glide_air_recover   added to handling air_recover_len
      bike  top_speed_ground, top_speed_air         multipliers on the two top speeds
            turn_074 .. turn_0a0                    multipliers on eight fields of the
                                                    bike class's shared attribute block

- CPU rows (CustomMachineCpu), what CPU riders make of the machine.
- Generators, the vehicle particle bank programs the machine brings. Its animation
  bank names the k-th one as GENERATOR_BASE + k.
- The assembly cinematic: an archive, its vsData-shaped public, and the vanilla legendary
  it runs under.
- The blip height and radar drop, the two per-kind heights the engine scales by 0.175.

As a command it prints a descriptor, or edits its fields in place:

    uv run python scripts/hsd/machine_descriptor.py \
        mods/ap_star/assets/machines/VcStarAp.dat
    uv run python scripts/hsd/machine_descriptor.py \
        mods/ap_star/assets/machines/VcStarAp.dat \
        --row glide_air_recover=-800,1200 --cpu swap_score=15
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd import ptcl
from hsd.archive import Archive, build_archive

# Must match mods/custom_machines/src/custom_machine_desc.h.
CUSTOM_MACHINE_MAGIC = 0x434D4348  # 'CMCH'
CUSTOM_MACHINE_DESC_VERSION = 13
DESC_SIZE = 0x4C
GENERATOR_BASE = 52
GENERATOR_MAX = 4
GENERATOR_SIZE = 256

# CustomMachineStatRow and CustomMachineBikeStatRow order, by is_bike.
STAT_ROW_NAMES = (
    (
        "top_speed_ground",
        "top_speed_air",
        "weight_air_impulse",
        "weight_air_recover",
        "glide_air_impulse",
        "glide_air_recover",
    ),
    (
        "top_speed_ground",
        "top_speed_air",
        "turn_074",
        "turn_078",
        "turn_084",
        "turn_088",
        "turn_094",
        "turn_098",
        "turn_09c",
        "turn_0a0",
    ),
)
CLASS_NAMES = ("star", "bike")
STAR_KIND_NUM = 19
KIND_NUM = 26

CINE_STRINGS = ("file", "symbol")

# CustomMachineCpu, in field order.
CPU_FORMAT = ">i4B12f"
CPU_FIELDS = (
    "swap_score",
    "flags",
    "stick_pitch",
    "has_charge_hold_gate",
    "has_release_level",
    "charge_release",
    "charge_hold_gate_a",
    "charge_hold_gate_b",
    "release_level",
    "align_cos_near",
    "align_cos_far",
    "turn_tolerance",
    "stuck_angle",
    "air_glider_pitch",
    "air_glider_min_len",
    "high_jump_pitch",
    "high_jump_min_len",
)
CPU_INT_FIELDS = CPU_FIELDS[:5]
PITCH_NAMES = ("none", "climb", "dive")

# Each class's animation bank particle slots, by is_bike: where they start, and what each
# is. Every slot is a vehicle bank generator id or -1.
PARTICLE_SLOTS = (
    (0x30, ("unk", "unk", "moving", "moving", "boost", "boost", "boost")),
    (0x20, ("cruise", "boost")),
)


def _cstr(data, off):
    end = data.index(b"\0", off)
    return data[off:end].decode("ascii")


def particle_slots(arc, symbol, is_bike):
    """The machine's animation bank particle slots, in its class's PARTICLE_SLOTS order."""
    start, names = PARTICLE_SLOTS[is_bike]
    anim = arc.deref(arc.publics[symbol] + 0x18)
    return list(struct.unpack_from(f">{len(names)}i", arc.data, anim + start))


def read(arc):
    """The archive's descriptor as a dict, the shape `append` takes."""
    if "customMachine" not in arc.publics:
        raise SystemExit(f"{arc.path}: no 'customMachine' public")
    d = arc.publics["customMachine"]
    data = arc.data

    magic, version = struct.unpack_from(">IH", data, d)
    if magic != CUSTOM_MACHINE_MAGIC:
        raise SystemExit(f"{arc.path}: bad descriptor magic {magic:#010x}")
    if version != CUSTOM_MACHINE_DESC_VERSION:
        raise SystemExit(
            f"{arc.path}: descriptor v{version}, expected v{CUSTOM_MACHINE_DESC_VERSION}"
        )

    def ptr(off):
        return arc.ptr(d + off)

    def i32(off):
        return struct.unpack_from(">i", data, d + off)[0]

    def f32(off):
        return struct.unpack_from(">f", data, d + off)[0]

    fields = {
        "name": _cstr(data, ptr(0x08)),
        "symbol": _cstr(data, ptr(0x0C)),
        "is_bike": i32(0x10),
        "wants_character": bool(i32(0x14)),
        "rider_kind": i32(0x18),
        "audio_kind": i32(0x1C),
        "spawn_weight": f32(0x20),
        "description": _cstr(data, ptr(0x24)) if ptr(0x24) else "",
        "generators": [],
        "cpu": None,
        "cinematic": None,
        "stat_rows": None,
        "blip_height": f32(0x44),
        "radar_drop": f32(0x48),
    }
    if fields["is_bike"] not in (0, 1):
        raise SystemExit(f"{arc.path}: is_bike {fields['is_bike']} names no machine class")

    table = ptr(0x2C)
    for i in range(i32(0x28) if table else 0):
        size = struct.unpack_from(">H", data, table + i * 8)[0]
        program = arc.ptr(table + i * 8 + 4)
        fields["generators"].append(bytes(data[program : program + size]))

    if ptr(0x30):
        fields["cpu"] = dict(zip(CPU_FIELDS, struct.unpack_from(CPU_FORMAT, data, ptr(0x30))))

    if all(ptr(0x34 + i * 4) for i in range(len(CINE_STRINGS))):
        cine = {k: _cstr(data, ptr(0x34 + i * 4)) for i, k in enumerate(CINE_STRINGS)}
        cine["machine_index"] = i32(0x3C)
        fields["cinematic"] = cine

    if ptr(0x40):
        fields["stat_rows"] = [
            struct.unpack_from(">ff", data, ptr(0x40) + i * 8)
            for i in range(len(STAT_ROW_NAMES[fields["is_bike"]]))
        ]
    return fields


def _check(fields):
    is_bike = fields.get("is_bike", 0)
    if is_bike not in (0, 1):
        raise SystemExit(f"is_bike {is_bike} names no machine class")
    rows = STAT_ROW_NAMES[is_bike]
    if not fields.get("stat_rows") or len(fields["stat_rows"]) != len(rows):
        raise SystemExit(f"a {CLASS_NAMES[is_bike]} descriptor needs all {len(rows)} stat rows")
    if not fields.get("cpu"):
        raise SystemExit("a descriptor needs its CPU rows")
    audio_kind = fields["audio_kind"]
    if not 0 <= audio_kind < KIND_NUM or (audio_kind >= STAR_KIND_NUM) != bool(is_bike):
        raise SystemExit(f"audio kind {audio_kind} is not a {CLASS_NAMES[is_bike]} MachineKind")

    generators = fields.get("generators") or []
    if len(generators) > GENERATOR_MAX:
        raise SystemExit(f"a machine brings at most {GENERATOR_MAX} generators")
    for i, desc in enumerate(generators):
        if len(desc) > GENERATOR_SIZE or ptcl.length(desc) != len(desc):
            raise SystemExit(
                f"generator {i} does not end on its terminator within {GENERATOR_SIZE} bytes"
            )


def append(data, relocs, fields):
    """Append a descriptor and everything it points at to `data`, registering every
    pointer slot in `relocs`, and return the descriptor's offset."""
    _check(fields)

    def align():
        data.extend(b"\0" * (-len(data) & 3))

    def blob(payload):
        align()
        off = len(data)
        data.extend(payload)
        return off

    def string(text):
        off = len(data)
        data.extend(text.encode("ascii") + b"\0")
        return off

    align()
    desc = len(data)
    data.extend(bytes(DESC_SIZE))

    def put_ptr(at, target):
        struct.pack_into(">I", data, at, target)
        relocs.append(at)

    put_ptr(desc + 0x08, string(fields["name"]))
    put_ptr(desc + 0x0C, string(fields["symbol"]))
    if fields.get("description"):
        put_ptr(desc + 0x24, string(fields["description"]))
    cine = fields.get("cinematic")
    if cine:
        for i, key in enumerate(CINE_STRINGS):
            put_ptr(desc + 0x34 + i * 4, string(cine[key]))

    struct.pack_into(">IHH", data, desc, CUSTOM_MACHINE_MAGIC, CUSTOM_MACHINE_DESC_VERSION, 0)
    struct.pack_into(
        ">iiiif",
        data,
        desc + 0x10,
        fields.get("is_bike", 0),
        1 if fields.get("wants_character") else 0,
        fields.get("rider_kind", 0),
        fields["audio_kind"],
        fields.get("spawn_weight", 0.0),
    )

    generators = list(fields.get("generators") or [])
    struct.pack_into(">i", data, desc + 0x28, len(generators))
    if generators:
        table = blob(bytes(8 * len(generators)))
        put_ptr(desc + 0x2C, table)
        for i, program in enumerate(generators):
            struct.pack_into(">HH", data, table + i * 8, len(program), 0)
            put_ptr(table + i * 8 + 4, blob(program))

    cpu = fields["cpu"]
    put_ptr(desc + 0x30, blob(struct.pack(CPU_FORMAT, *(cpu[k] for k in CPU_FIELDS))))

    struct.pack_into(">i", data, desc + 0x3C, cine["machine_index"] if cine else 0)

    rows = b"".join(struct.pack(">ff", lo, hi) for lo, hi in fields["stat_rows"])
    put_ptr(desc + 0x40, blob(rows))

    struct.pack_into(
        ">ff", data, desc + 0x44, fields.get("blip_height", 0.0), fields.get("radar_drop", 0.0)
    )
    return desc


def owned_tail(arc):
    """Where the descriptor's tail starts: the lowest of the descriptor and everything
    reachable from a pointer at or past it. Nothing below may point in."""
    start = arc.publics["customMachine"]
    while True:
        low = min([start] + [arc.ptr(r) for r in arc.relocs if r >= start])
        if low == start:
            break
        start = low
    for r in arc.relocs:
        if r < start and arc.ptr(r) >= start:
            raise SystemExit(f"{arc.path}: {r:#x} points into the descriptor tail at {start:#x}")
    for name, off in arc.publics.items():
        if name != "customMachine" and off >= start:
            raise SystemExit(f"{arc.path}: public '{name}' sits in the descriptor tail")
    return start


def rewrite(path, fields):
    """Replace the archive's descriptor with one built from `fields`."""
    arc = Archive(path)
    start = owned_tail(arc)
    data = bytearray(arc.data[:start])
    relocs = [r for r in arc.relocs if r < start]
    desc = append(data, relocs, fields)
    publics = [
        (name, desc if name == "customMachine" else off) for name, off in arc.publics.items()
    ]
    externs = [(name, off) for off, name in arc.externs]
    out = build_archive(data, relocs, publics, arc.version, externs)
    with open(path, "wb") as f:
        f.write(out)


def show(path, fields, slots):
    is_bike = fields["is_bike"]
    print(
        f"{path}: v{CUSTOM_MACHINE_DESC_VERSION} {CLASS_NAMES[is_bike]} "
        f"'{fields['name']}' ({fields['symbol']})"
    )
    print(
        f"  character {'yes' if fields['wants_character'] else 'no'}, rider kind "
        f"{fields['rider_kind']}, audio kind {fields['audio_kind']}, spawn weight "
        f"{fields['spawn_weight']:g}"
    )
    print(f"  blip height {fields['blip_height']:g}, radar drop {fields['radar_drop']:g}")
    for line in fields["description"].split("\n") if fields["description"] else []:
        print(f"  description: {line}")
    for i, desc in enumerate(fields["generators"]):
        print(f"  generator {i}, id {GENERATOR_BASE + i}: {len(desc)} bytes")
    groups = {}
    for name, gen_id in zip(PARTICLE_SLOTS[is_bike][1], slots):
        groups.setdefault(name, []).append(gen_id)
    print(
        "  particle slots: "
        + ", ".join(f"{name} {' '.join(map(str, ids))}" for name, ids in groups.items())
    )
    for gen_id in slots:
        if gen_id >= GENERATOR_BASE + len(fields["generators"]):
            print(f"  warning: generator id {gen_id} names no generator this machine brings")
    cine = fields["cinematic"]
    if cine:
        print(
            f"  cinematic: {cine['file']} ({cine['symbol']}), machine index {cine['machine_index']}"
        )
    print("  stat rows:")
    for name, (lo, hi) in zip(STAT_ROW_NAMES[is_bike], fields["stat_rows"]):
        print(f"    {name:18s} low {lo:12.6g}  high {hi:12.6g}")
    print("  cpu:")
    for name in CPU_FIELDS:
        value = fields["cpu"][name]
        if name == "flags":
            text = f"{value:#04x}"
        elif name == "stick_pitch":
            text = PITCH_NAMES[value] if value < len(PITCH_NAMES) else str(value)
        elif name in CPU_INT_FIELDS:
            text = str(value)
        else:
            text = f"{value:g}"
        print(f"    {name:20s} {text}")


def parse_row(spec, is_bike):
    names = STAT_ROW_NAMES[is_bike]
    name, _, value = spec.partition("=")
    if name not in names:
        raise SystemExit(
            f"unknown {CLASS_NAMES[is_bike]} row '{name}', expected one of {', '.join(names)}"
        )
    try:
        lo, hi = (float(v) for v in value.split(","))
    except ValueError:
        raise SystemExit(f"--row {spec}: expected NAME=LOW,HIGH")
    return names.index(name), (lo, hi)


def parse_cpu(spec):
    name, _, value = spec.partition("=")
    if name not in CPU_FIELDS:
        raise SystemExit(f"unknown CPU field '{name}', expected one of {', '.join(CPU_FIELDS)}")
    if name == "stick_pitch" and value in PITCH_NAMES:
        return name, PITCH_NAMES.index(value)
    try:
        return name, int(value, 0) if name in CPU_INT_FIELDS else float(value)
    except ValueError:
        raise SystemExit(f"--cpu {spec}: bad value")


def main(argv):
    p = argparse.ArgumentParser(description="Show or edit a custom machine descriptor")
    p.add_argument("archive", help="custom machine archive with a customMachine public")
    p.add_argument("--audio-kind", type=int, help="same-class MachineKind whose sounds it uses")
    p.add_argument("--spawn-weight", type=float, help="City Trial spawn weight")
    p.add_argument("--blip-height", type=float, help="City Trial field blip height")
    p.add_argument("--radar-drop", type=float, help="stat radar screen model drop")
    p.add_argument(
        "--row",
        action="append",
        default=[],
        metavar="NAME=LOW,HIGH",
        help="set one stat row pair; repeat for each",
    )
    p.add_argument(
        "--cpu",
        action="append",
        default=[],
        metavar="FIELD=VALUE",
        help="set one CPU field; repeat for each",
    )
    args = p.parse_args(argv[1:])

    arc = Archive(args.archive)
    fields = read(arc)
    edited = False
    for key in ("audio_kind", "spawn_weight", "blip_height", "radar_drop"):
        value = getattr(args, key)
        if value is not None:
            fields[key] = value
            edited = True
    if args.row:
        rows = list(fields["stat_rows"])
        for spec in args.row:
            index, pair = parse_row(spec, fields["is_bike"])
            rows[index] = pair
        fields["stat_rows"] = rows
        edited = True
    for spec in args.cpu:
        name, value = parse_cpu(spec)
        fields["cpu"][name] = value
        edited = True

    if edited:
        rewrite(args.archive, fields)
        arc = Archive(args.archive)
        fields = read(arc)
    show(args.archive, fields, particle_slots(arc, fields["symbol"], fields["is_bike"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
