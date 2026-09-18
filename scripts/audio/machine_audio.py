#!/usr/bin/env python3
"""Build and inspect a drop-in machine audio bank.

A drop-in machine's sounds live in a .ssm next to its .dat in the disc's
machines/ folder, same basename. It is an ordinary HAL sound bank holding
exactly one entry per MachineAudioParams sound slot, in that struct's order; an
entry whose sample rate is 0 is absent and the machine keeps the sound its
descriptor's audio_kind uses. Entries may share data - two roles pointing at one sample cost
one copy - because a channel's addresses are arbitrary offsets into the bank's
shared data block.

  roles                          list the slot order
  info   BANK                    describe a bank
  dump   BANK OUTDIR             write every sound to a .wav
  build  OUT --engine a.wav ...  build a bank from .wav files
"""

import argparse
import os
import struct
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from audio import dsp, wav
from audio.bank import encode_sound, sound_pcm
from audio.ssm import SSM, Channel, Sound

# Vanilla uses global sound indices 0..614. A drop-in bank is assigned its real
# base by the mod as it loads; what is written here only has to stay clear of them.
DROPIN_BASE_INDEX = 615

# MachineAudioParams sound slots, in struct order.
ROLES = [
    ("engine", "engine loop", True),
    ("charge1", "charge gauge, first third", True),
    ("charge2", "charge gauge, second third", True),
    ("charge3", "charge gauge, final third", True),
    ("boost-l", "boost release, loud", False),
    ("boost-m", "boost release, medium", False),
    ("boost-s", "boost release, quiet", False),
    ("surface", "surface / run noise loop", True),
    ("rumble", "rumble loop", True),
    ("spin", "quick spin", False),
    ("engine-start", "engine start", True),
    ("surface-start", "surface start", True),
    ("overheat", "overheat, one shot on auto-discharge", False),
]
ROLE_NAMES = [r[0] for r in ROLES]


def build_bank(entries, base_index=DROPIN_BASE_INDEX, quiet=False):
    """entries is one (pcm, rate, loop) or None per role. Equal PCM shares data."""
    data = bytearray()
    sounds = []
    cache = {}
    for (name, _, _), item in zip(ROLES, entries):
        if item is None:
            sounds.append(Sound(0, [Channel()]))
            continue
        pcm, rate, loop = item
        key = (bytes(memoryview(struct.pack(">%dh" % len(pcm), *pcm))), loop)
        if key in cache:
            sounds.append(Sound(rate, [cache[key].copy()]))
            if not quiet:
                print(f"  {name:14s} shares the sound above")
            continue
        ch, adpcm = encode_sound(pcm, loop)
        shift = len(data) * 2
        ch.ca += shift
        ch.sa += shift
        ch.ea += shift
        data += adpcm
        cache[key] = ch
        sounds.append(Sound(rate, [ch]))
        if not quiet:
            back = dsp.decode(adpcm, ch.coef, count=len(pcm))
            print(
                f"  {name:14s} {len(pcm):7d} smp @{rate:5d} "
                f"{'loop' if loop is not None else 'one shot':8s} "
                f"{len(adpcm) / 1024:7.1f} KB  SNR {dsp.snr(pcm, back):.1f} dB"
            )
    return SSM(base_index=base_index, sounds=sounds, data=bytes(data))


def cmd_roles(args):
    for i, (name, desc, looped) in enumerate(ROLES):
        print(f"{i:2d}  --{name:<14s} {'loop    ' if looped else 'one shot'}  {desc}")


def cmd_info(args):
    bank = SSM.load(args.bank)
    print(
        f"{args.bank}: {len(bank.sounds)} sounds, base index {bank.base_index}, "
        f"{len(bank.data)} bytes of ADPCM"
    )
    for i, snd in enumerate(bank.sounds):
        label = ROLE_NAMES[i] if len(bank.sounds) == len(ROLES) else str(i)
        if snd.sample_rate == 0:
            print(f"  {i:2d} {label:14s} absent")
            continue
        c = snd.channels[0]
        pcm, rate, loop = sound_pcm(bank, i)
        print(
            f"  {i:2d} {label:14s} {len(pcm):7d} smp @{rate:5d} "
            f"{'loop@%d' % loop if loop is not None else 'one shot':>12s} "
            f"{(c.ea - c.ca) // 2 // 1024:4d} KB"
        )


def cmd_dump(args):
    bank = SSM.load(args.bank)
    os.makedirs(args.outdir, exist_ok=True)
    for i, snd in enumerate(bank.sounds):
        if snd.sample_rate == 0:
            continue
        pcm, rate, loop = sound_pcm(bank, i)
        label = ROLE_NAMES[i] if len(bank.sounds) == len(ROLES) else f"{i:03d}"
        path = os.path.join(args.outdir, f"{i:03d}_{label}.wav")
        wav.write(path, pcm, rate)
        print(
            f"  {path}  {len(pcm)} smp @{rate}"
            + (f" loop@{loop}" if loop is not None else "")
        )


def cmd_build(args):
    entries = []
    for name, _, looped in ROLES:
        path = getattr(args, name.replace("-", "_"))
        if not path:
            entries.append(None)
            continue
        pcm, rate = wav.read(path)
        loop = 0 if looped else None
        override = (args.loop or {}).get(name)
        if override is not None:
            loop = None if override < 0 else override
        entries.append((pcm, rate, loop))
    if all(e is None for e in entries):
        raise SystemExit("no sounds given; see --help")
    bank = build_bank(entries)
    bank.save(args.out)
    print(f"wrote {args.out} ({os.path.getsize(args.out)} bytes)")


def main(argv=None):
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("roles").set_defaults(func=cmd_roles)

    q = sub.add_parser("info")
    q.add_argument("bank")
    q.set_defaults(func=cmd_info)

    q = sub.add_parser("dump")
    q.add_argument("bank")
    q.add_argument("outdir")
    q.set_defaults(func=cmd_dump)

    q = sub.add_parser("build")
    q.add_argument("out")
    for name, desc, _ in ROLES:
        q.add_argument(f"--{name}", metavar="WAV", help=desc)
    q.add_argument(
        "--loop",
        action=_LoopAction,
        default={},
        metavar="ROLE=SAMPLE",
        help="loop point for a role, or -1 for one shot; repeatable",
    )
    q.set_defaults(func=cmd_build)

    args = p.parse_args(argv)
    return args.func(args)


class _LoopAction(argparse.Action):
    def __call__(self, parser, ns, value, option_string=None):
        role, _, at = value.partition("=")
        if role not in ROLE_NAMES:
            parser.error(f"unknown role {role!r}")
        getattr(ns, self.dest).setdefault(role, int(at))


if __name__ == "__main__":
    main()
