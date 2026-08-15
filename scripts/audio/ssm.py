"""HAL .ssm sound-sample banks (iso/files/audio/jp/*.ssm).

    0x00 u32 table_size     bytes of sound entries, from 0x10
    0x04 u32 data_size      bytes of ADPCM data, a multiple of 32
    0x08 u32 sound_count
    0x0c u32 base_index     global sound index of this bank's sound 0
    0x10      sound entries, 8 + 0x40 * channels each
              the ADPCM data block, starting at the next 32-byte boundary

A sound entry is a channel count, a sample rate, then one 0x40 block per
channel. Every address in a channel block is a nibble offset from the start of
the data block, not from the start of the channel's own samples, so a sound's
first frame header sits at data[(ca / 16) * 8] and equals its stored `ips`.

FGM_LoadBankCallback (0x80447ea4) reads the data block with data_size straight
out of the header, and File_Read asserts that a read size is a multiple of 32,
so the data block is padded out to that and the file ends with it.
"""

import struct

ENTRY_FIXED = 8
CHANNEL_SIZE = 0x40
DATA_ALIGN = 32


class Channel:
    def __init__(self, **kw):
        self.loop = kw.get('loop', 0)
        self.fmt = kw.get('fmt', 0)
        self.sa = kw.get('sa', 0)          # loop start, nibble
        self.ea = kw.get('ea', 0)          # last playable nibble
        self.ca = kw.get('ca', 0)          # first nibble of the sample
        self.coef = list(kw.get('coef', [0] * 16))
        self.gain = kw.get('gain', 0)
        self.ips = kw.get('ips', 0)        # predictor/scale of the first frame
        self.yn1 = kw.get('yn1', 0)
        self.yn2 = kw.get('yn2', 0)
        self.lps = kw.get('lps', 0)        # predictor/scale of the loop frame
        self.lyn1 = kw.get('lyn1', 0)
        self.lyn2 = kw.get('lyn2', 0)
        self.pad = kw.get('pad', 0)

    def pack(self):
        return (struct.pack('>HHIII', self.loop, self.fmt, self.sa, self.ea, self.ca)
                + struct.pack('>16h', *self.coef)
                + struct.pack('>HHhhHhhH', self.gain, self.ips, self.yn1, self.yn2,
                              self.lps, self.lyn1, self.lyn2, self.pad))

    @classmethod
    def unpack(cls, b, o):
        loop, fmt, sa, ea, ca = struct.unpack_from('>HHIII', b, o)
        coef = list(struct.unpack_from('>16h', b, o + 16))
        gain, ips, yn1, yn2, lps, lyn1, lyn2, pad = struct.unpack_from('>HHhhHhhH', b, o + 48)
        return cls(loop=loop, fmt=fmt, sa=sa, ea=ea, ca=ca, coef=coef, gain=gain,
                   ips=ips, yn1=yn1, yn2=yn2, lps=lps, lyn1=lyn1, lyn2=lyn2, pad=pad)


class Sound:
    def __init__(self, sample_rate, channels):
        self.sample_rate = sample_rate
        self.channels = channels

    @property
    def data_span(self):
        lo = min(c.ca for c in self.channels) // 2
        hi = max(c.ea for c in self.channels) // 2 + 1
        return lo, hi


class SSM:
    def __init__(self, base_index=0, sounds=None, data=b''):
        self.base_index = base_index
        self.sounds = sounds or []
        self.data = data

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            b = f.read()
        table_size, data_size, count, base = struct.unpack_from('>4I', b, 0)
        data_off = 0x10 + table_size + -(0x10 + table_size) % DATA_ALIGN
        sounds, o = [], 0x10
        for _ in range(count):
            nch, rate = struct.unpack_from('>2I', b, o)
            chans = [Channel.unpack(b, o + ENTRY_FIXED + i * CHANNEL_SIZE) for i in range(nch)]
            sounds.append(Sound(rate, chans))
            o += ENTRY_FIXED + nch * CHANNEL_SIZE
        if o - 0x10 != table_size:
            raise ValueError(f"{path}: entry table is {o - 0x10:#x}, header says {table_size:#x}")
        return cls(base, sounds, b[data_off:data_off + data_size])

    def pack(self):
        table = b''.join(struct.pack('>2I', len(s.channels), s.sample_rate)
                         + b''.join(c.pack() for c in s.channels) for s in self.sounds)
        pad = -(0x10 + len(table)) % DATA_ALIGN
        data = self.data + b'\0' * (-len(self.data) % DATA_ALIGN)
        return (struct.pack('>4I', len(table), len(data),
                            len(self.sounds), self.base_index)
                + table + b'\0' * pad + data)

    def save(self, path):
        with open(path, 'wb') as f:
            f.write(self.pack())

    def sound_bytes(self, index):
        s = self.sounds[index]
        lo, hi = s.data_span
        return self.data[lo:hi]


def _channel_copy(self):
    c = Channel()
    for k in ('loop', 'fmt', 'sa', 'ea', 'ca', 'gain', 'ips',
              'yn1', 'yn2', 'lps', 'lyn1', 'lyn2', 'pad'):
        setattr(c, k, getattr(self, k))
    c.coef = list(self.coef)
    return c


Channel.copy = _channel_copy
