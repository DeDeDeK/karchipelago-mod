"""16-bit PCM WAV I/O and the resampler the pitch-shifted stubs use."""

import math
import struct
import wave


def read(path):
    """Returns (samples, sample_rate). Stereo is mixed down to mono."""
    with wave.open(path, 'rb') as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"{path}: need 16-bit PCM, got {w.getsampwidth() * 8}-bit")
        nch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    vals = list(struct.unpack('<%dh' % (len(raw) // 2), raw))
    if nch == 1:
        return vals, rate
    return [sum(vals[i:i + nch]) // nch for i in range(0, len(vals), nch)], rate


def write(path, samples, rate):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack('<%dh' % len(samples), *samples))


def resample(samples, ratio, taps=16):
    """Windowed-sinc resample to len/ratio samples. ratio < 1 lowers the pitch."""
    if abs(ratio - 1.0) < 1e-9:
        return list(samples)
    n = len(samples)
    out_n = int(n / ratio)
    cutoff = min(1.0, ratio)
    out = []
    for i in range(out_n):
        pos = i * ratio
        c = int(math.floor(pos))
        acc = 0.0
        norm = 0.0
        for k in range(c - taps + 1, c + taps + 1):
            x = (pos - k) * cutoff
            if abs(x) < 1e-9:
                h = 1.0
            else:
                h = math.sin(math.pi * x) / (math.pi * x)
            # Blackman window over the tap span.
            t = (k - (c - taps + 1)) / (2.0 * taps - 1)
            h *= 0.42 - 0.5 * math.cos(2 * math.pi * t) + 0.08 * math.cos(4 * math.pi * t)
            if 0 <= k < n:
                acc += samples[k] * h
            norm += h
        v = int(round(acc / norm)) if norm else 0
        out.append(-32768 if v < -32768 else (32767 if v > 32767 else v))
    return out
