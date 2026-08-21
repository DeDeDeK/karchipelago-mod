#!/usr/bin/env python3
"""Build and inspect a drop-in machine audio bank.

A drop-in machine's sounds live in a .ssm next to its .dat in the disc's
machines/ folder, same basename. It is an ordinary HAL sound bank holding
exactly one entry per MachineAudioParams sound slot, in that struct's order; an
entry whose sample rate is 0 is absent and the machine keeps the sound its
clone_kind uses. Entries may share data - two roles pointing at one sample cost
one copy - because a channel's addresses are arbitrary offsets into the bank's
shared data block.

  roles                          list the slot order
  info   BANK                    describe a bank
  dump   BANK OUTDIR             write every sound to a .wav
  donors MACHINE OUTDIR          write the sample behind each of a machine's roles
  clone  MACHINE OUT             build a bank from a vanilla machine's sounds
  build  OUT --engine a.wav ...  build a bank from .wav files

A star's own row leaves roles at -1 where the kind has no such sound - every
star but Wagon has no boost release of its own, for one. `donors` and `clone`
take those from --fallback's row instead, so a drop-in can fill a slot its
clone kind leaves empty.
"""

import argparse
import os
import struct
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from audio import dsp, wav
from audio.bank import encode_sound, sound_pcm
from audio.sem import DEFAULT_NAMES, DEFAULT_SEM, SEM, NameTable
from audio.ssm import SSM, Channel, Sound

AUDIO_DIR = 'iso/files/audio/jp'
VC_COMMON = 'iso/files/VcCommon.dat'

# Vanilla uses global sound indices 0..614. A drop-in bank is assigned its real
# base by the mod as it loads; what is written here only has to stay clear of them.
DROPIN_BASE_INDEX = 615

# MachineAudioParams sound slots, in struct order.
ROLES = [
    ('engine', 'engine loop', True),
    ('charge1', 'charge gauge, first third', True),
    ('charge2', 'charge gauge, second third', True),
    ('charge3', 'charge gauge, final third', True),
    ('boost-l', 'boost release, loud', False),
    ('boost-m', 'boost release, medium', False),
    ('boost-s', 'boost release, quiet', False),
    ('surface', 'surface / run noise loop', True),
    ('rumble', 'rumble loop', True),
    ('spin', 'quick spin', False),
    ('engine-start', 'engine start', True),
    ('surface-start', 'surface start', True),
    ('overheat', 'overheat, one shot on auto-discharge', False),
]
ROLE_NAMES = [r[0] for r in ROLES]

# Star class slots, which for a star equal its MachineKind. The last two are the
# flying riders, who have no engine of their own.
STAR_MACHINES = [
    'warp', 'compact', 'winged', 'shadow', 'hydra', 'bulk', 'slick', 'formula',
    'dragoon', 'wagon', 'rocket', 'swerve', 'turbo', 'jet', 'flight', 'free',
    'steer', 'wing-kirby', 'wing-metaknight',
]


def load_rows(root=''):
    """The 19 star MachineAudioParams rows from VcCommon.dat, sound ids only."""
    sys.path.append(os.path.join(root or '.', 'scripts'))
    from hsd.archive import Archive, u32
    a = Archive(os.path.join(root, VC_COMMON))
    star = u32(a.data, u32(a.data, a.publics['vcDataCommon'] + 0x10))
    return [[struct.unpack_from('>i', a.data, star + k * 0x94 + j * 4)[0] for j in range(13)]
            for k in range(19)]


class Vanilla:
    """The vanilla banks, indexed the way a script's opcode 0x01 indexes them."""

    def __init__(self, root=''):
        self.root = root
        self.sem = SEM(os.path.join(root, DEFAULT_SEM))
        self.names = NameTable(os.path.join(root, DEFAULT_NAMES))
        self._banks = {}

    def bank(self, index):
        if index not in self._banks:
            stem = self.names.banks[index]['name']
            self._banks[index] = SSM.load(os.path.join(self.root, AUDIO_DIR, stem + '.ssm'))
        return self._banks[index]

    def sound(self, global_index):
        """(bank, local index) for a global sound index."""
        for b in sorted(self.names.banks):
            bank = self.bank(b)
            if bank.base_index <= global_index < bank.base_index + len(bank.sounds):
                return bank, global_index - bank.base_index
        raise KeyError(global_index)

    def role_pcm(self, sfx_id):
        """(pcm, rate, loop_start) for the sample an FGM id plays, or None."""
        if sfx_id == -1:
            return None
        sounds = self.sem.sounds(sfx_id)
        if not sounds:
            return None
        bank, local = self.sound(sounds[0])
        return sound_pcm(bank, local)


def resolve_row(rows, kind, fallback=None):
    """A star's 13 FGM ids, with -1 roles taken from the fallback star's row.

    Returns one (sfx_id, source_kind) per role.
    """
    row = rows[kind]
    if fallback is None:
        return [(sfx, kind) for sfx in row]
    alt = rows[fallback]
    return [(sfx, kind) if sfx >= 0 else (alt[i], fallback) for i, sfx in enumerate(row)]


def star_index(name):
    if name not in STAR_MACHINES:
        raise SystemExit(f"unknown machine {name!r}; one of {', '.join(STAR_MACHINES)}")
    return STAR_MACHINES.index(name)


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
        key = (bytes(memoryview(struct.pack('>%dh' % len(pcm), *pcm))), loop)
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
            print(f"  {name:14s} {len(pcm):7d} smp @{rate:5d} "
                  f"{'loop' if loop is not None else 'one shot':8s} "
                  f"{len(adpcm) / 1024:7.1f} KB  SNR {dsp.snr(pcm, back):.1f} dB")
    return SSM(base_index=base_index, sounds=sounds, data=bytes(data))


def cmd_roles(args):
    for i, (name, desc, looped) in enumerate(ROLES):
        print(f"{i:2d}  --{name:<14s} {'loop    ' if looped else 'one shot'}  {desc}")


def cmd_info(args):
    bank = SSM.load(args.bank)
    print(f"{args.bank}: {len(bank.sounds)} sounds, base index {bank.base_index}, "
          f"{len(bank.data)} bytes of ADPCM")
    for i, snd in enumerate(bank.sounds):
        label = ROLE_NAMES[i] if len(bank.sounds) == len(ROLES) else str(i)
        if snd.sample_rate == 0:
            print(f"  {i:2d} {label:14s} absent")
            continue
        c = snd.channels[0]
        pcm, rate, loop = sound_pcm(bank, i)
        print(f"  {i:2d} {label:14s} {len(pcm):7d} smp @{rate:5d} "
              f"{'loop@%d' % loop if loop is not None else 'one shot':>12s} "
              f"{(c.ea - c.ca) // 2 // 1024:4d} KB")


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
        print(f"  {path}  {len(pcm)} smp @{rate}"
              + (f" loop@{loop}" if loop is not None else ""))


def cmd_donors(args):
    kind = star_index(args.machine)
    fallback = star_index(args.fallback) if args.fallback else None
    rows = load_rows(args.root)
    v = Vanilla(args.root)
    os.makedirs(args.outdir, exist_ok=True)
    for i, ((name, _, looped), (sfx, src)) in enumerate(zip(ROLES, resolve_row(rows, kind, fallback))):
        note = '' if src == kind else f" (from {STAR_MACHINES[src]})"
        got = v.role_pcm(sfx)
        if got is None:
            print(f"  {i:2d} {name:14s} {v.names.name(sfx)}{note}  no sample")
            continue
        pcm, rate, loop = got
        if args.pitch != 1.0:
            pcm = wav.resample(pcm, args.pitch)
            if loop is not None:
                loop = int(loop / args.pitch)
        path = os.path.join(args.outdir, f"{i:03d}_{name}.wav")
        wav.write(path, pcm, rate)
        print(f"  {i:2d} {name:14s} {v.names.name(sfx)}{note}  -> {path}  "
              f"{len(pcm)} smp @{rate}"
              + (f" loop@{loop}" if loop is not None else " one shot"))


def cmd_clone(args):
    kind = star_index(args.machine)
    fallback = star_index(args.fallback) if args.fallback else None
    row = resolve_row(load_rows(args.root), kind, fallback)
    v = Vanilla(args.root)
    want = set(args.roles.split(',')) if args.roles else set(ROLE_NAMES)
    print(f"cloning {args.machine} (star slot {kind}) at pitch {args.pitch}")
    entries = []
    for (name, _, _), (sfx, src) in zip(ROLES, row):
        if name not in want:
            entries.append(None)
            continue
        got = v.role_pcm(sfx)
        if got is None:
            entries.append(None)
            continue
        pcm, rate, loop = got
        if args.pitch != 1.0:
            pcm = wav.resample(pcm, args.pitch)
            if loop is not None:
                loop = int(loop / args.pitch)
        entries.append((pcm, rate, loop))
    bank = build_bank(entries)
    bank.save(args.out)
    print(f"wrote {args.out} ({os.path.getsize(args.out)} bytes)")


def cmd_build(args):
    entries = []
    for name, _, looped in ROLES:
        path = getattr(args, name.replace('-', '_'))
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
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--root', default='', help='repo root holding iso/files (default: cwd)')
    sub = p.add_subparsers(dest='cmd', required=True)

    sub.add_parser('roles').set_defaults(func=cmd_roles)

    q = sub.add_parser('info')
    q.add_argument('bank')
    q.set_defaults(func=cmd_info)

    q = sub.add_parser('dump')
    q.add_argument('bank')
    q.add_argument('outdir')
    q.set_defaults(func=cmd_dump)

    q = sub.add_parser('donors')
    q.add_argument('machine', help=', '.join(STAR_MACHINES))
    q.add_argument('outdir')
    q.add_argument('--pitch', type=float, default=1.0,
                   help='resample ratio; below 1.0 lowers the pitch and lengthens the sound')
    q.add_argument('--fallback', help='star whose row fills the roles this one leaves at -1')
    q.set_defaults(func=cmd_donors)

    q = sub.add_parser('clone')
    q.add_argument('machine', help=', '.join(STAR_MACHINES))
    q.add_argument('out')
    q.add_argument('--pitch', type=float, default=1.0,
                   help='resample ratio; below 1.0 lowers the pitch and lengthens the sound')
    q.add_argument('--roles', help='comma separated subset to take from the donor')
    q.add_argument('--fallback', help='star whose row fills the roles this one leaves at -1')
    q.set_defaults(func=cmd_clone)

    q = sub.add_parser('build')
    q.add_argument('out')
    for name, desc, _ in ROLES:
        q.add_argument(f'--{name}', metavar='WAV', help=desc)
    q.add_argument('--loop', action=_LoopAction, default={}, metavar='ROLE=SAMPLE',
                   help='loop point for a role, or -1 for one shot; repeatable')
    q.set_defaults(func=cmd_build)

    args = p.parse_args(argv)
    return args.func(args)


class _LoopAction(argparse.Action):
    def __call__(self, parser, ns, value, option_string=None):
        role, _, at = value.partition('=')
        if role not in ROLE_NAMES:
            parser.error(f"unknown role {role!r}")
        getattr(ns, self.dest).setdefault(role, int(at))


if __name__ == '__main__':
    main()
