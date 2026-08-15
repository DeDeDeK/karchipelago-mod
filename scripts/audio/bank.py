"""Decode and re-encode .ssm sounds as PCM.

Every nibble address in a channel is relative to the bank's data block, so a
sound's own samples are numbered from the start of the frame that holds its
`ca`, which is what nibble_to_sample / sample_to_nibble convert between.
"""

from . import dsp


def _frame_base(ca):
    return (ca // 16) * 16


def nibble_to_sample(nib, ca):
    rel = nib - _frame_base(ca)
    return (rel // 16) * 14 + (rel % 16) - 2


def sample_to_nibble(index, ca):
    return _frame_base(ca) + (index // 14) * 16 + (index % 14) + 2


def sound_pcm(bank, index, channel=0):
    """Decode one sound. Returns (samples, sample_rate, loop_start or None)."""
    snd = bank.sounds[index]
    c = snd.channels[channel]
    raw = bank.data[_frame_base(c.ca) // 2:(c.ea // 16) * 8 + 8]
    first = nibble_to_sample(c.ca, c.ca)
    count = nibble_to_sample(c.ea, c.ca) - first + 1
    pcm = dsp.decode(raw, c.coef, count=first + count)[first:]
    return pcm, snd.sample_rate, (nibble_to_sample(c.sa, c.ca) if c.loop else None)


def encode_sound(pcm, loop_start=None, coef=None):
    """Encode PCM into (Channel, adpcm bytes). Addresses are relative to nibble 0."""
    from .ssm import Channel
    book = coef if coef is not None else dsp.make_book(pcm)
    adpcm, headers = dsp.encode(pcm, book)
    ch = Channel(loop=1 if loop_start is not None else 0, coef=book)
    ch.ca = 2
    ch.ea = sample_to_nibble(len(pcm) - 1, 2)
    ch.ips = headers[0] if headers else 0
    if loop_start is None:
        ch.sa = 2
    else:
        loop_start = max(0, min(len(pcm) - 1, loop_start))
        ch.sa = sample_to_nibble(loop_start, 2)
        ch.lps = headers[loop_start // 14]
        # The history the DSP resumes with is what it decoded, not the source.
        prev = dsp.decode(adpcm, book, count=loop_start)
        ch.lyn1 = prev[-1] if len(prev) >= 1 else 0
        ch.lyn2 = prev[-2] if len(prev) >= 2 else 0
    return ch, adpcm
