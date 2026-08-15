"""Nintendo DSP-ADPCM codec.

Frames are 8 bytes: one predictor/scale byte then 14 four-bit samples. The
decoder is exact - the console runs this same recurrence in the DSP - so an
encode/decode round trip is the only quality knob here.
"""

import struct


def clamp16(x):
    return -32768 if x < -32768 else (32767 if x > 32767 else x)


def nibbles_to_bytes(nib):
    """Byte offset of a nibble address, DSP-style (nibble 2 = byte 1)."""
    return nib // 2


def samples_to_nibbles(n):
    """Nibble count holding n samples, including the per-frame header nibbles."""
    frames, rem = divmod(n, 14)
    return frames * 16 + (rem + 2 if rem else 0)


def sample_to_nibble(n):
    """Nibble address of sample n within a stream that starts at nibble 0."""
    frames, rem = divmod(n, 14)
    return frames * 16 + rem + 2


def decode(data, coef, ps=None, yn1=0, yn2=0, count=None):
    """Decode ADPCM bytes to a list of s16. `coef` is 16 s16 in Q11."""
    out = []
    hist1, hist2 = yn1, yn2
    total = count if count is not None else len(data) // 8 * 14
    pos = 0
    while len(out) < total and pos + 1 <= len(data):
        header = data[pos] if ps is None or pos else ps
        pos += 1
        scale = 1 << (header & 0xF)
        ci = (header >> 4) & 0x7
        c1, c2 = coef[ci * 2], coef[ci * 2 + 1]
        for i in range(14):
            if len(out) >= total:
                break
            b = data[pos + i // 2] if pos + i // 2 < len(data) else 0
            nib = (b >> 4) if i % 2 == 0 else (b & 0xF)
            if nib >= 8:
                nib -= 16
            pred = (nib * scale << 11) + c1 * hist1 + c2 * hist2
            val = clamp16((pred + 1024) >> 11)
            out.append(val)
            hist2, hist1 = hist1, val
        pos += 7
    return out


def _encode_frame(pcm, coef, hist1, hist2):
    """Best (header, 7 bytes, hist1, hist2) for up to 14 samples."""
    best = None
    for ci in range(8):
        c1, c2 = coef[ci * 2], coef[ci * 2 + 1]
        # Smallest scale that keeps every residual inside a signed nibble.
        worst = 0
        h1, h2 = hist1, hist2
        for s in pcm:
            resid = (s << 11) - c1 * h1 - c2 * h2
            worst = max(worst, abs(resid))
            h2, h1 = h1, s
        scale = 0
        while scale < 15 and worst > 7 * (1 << scale) * 2048 + 2047:
            scale += 1
        for trial in range(max(0, scale - 1), min(15, scale + 2) + 1):
            step = 1 << trial
            h1, h2 = hist1, hist2
            nibs, err = [], 0
            for s in pcm:
                resid = (s << 11) - c1 * h1 - c2 * h2
                q = int(round(resid / (step * 2048)))
                q = -8 if q < -8 else (7 if q > 7 else q)
                val = clamp16(((q * step << 11) + c1 * h1 + c2 * h2 + 1024) >> 11)
                err += (val - s) ** 2
                nibs.append(q & 0xF)
                h2, h1 = h1, val
            if best is None or err < best[0]:
                best = (err, (ci << 4) | trial, list(nibs), h1, h2)
    err, header, nibs, h1, h2 = best
    nibs += [0] * (14 - len(nibs))
    body = bytes((nibs[i] << 4) | nibs[i + 1] for i in range(0, 14, 2))
    return err, bytes([header]) + body, h1, h2


def encode(pcm, coef, yn1=0, yn2=0):
    """Encode s16 samples with a 16-entry Q11 coefficient book."""
    out = bytearray()
    hist1, hist2 = yn1, yn2
    frames = []
    for i in range(0, len(pcm), 14):
        err, blk, hist1, hist2 = _encode_frame(pcm[i:i + 14], coef, hist1, hist2)
        out += blk
        frames.append(blk[0])
    return bytes(out), frames


def _normal_eq(frame, h1, h2):
    """Autocorrelation terms of one frame against its two-sample history."""
    r00 = r01 = r11 = b0 = b1 = 0.0
    a, b = h1, h2
    for s in frame:
        r00 += a * a
        r01 += a * b
        r11 += b * b
        b0 += s * a
        b1 += s * b
        b, a = a, s
    return r00, r01, r11, b0, b1


def _solve(r00, r01, r11, b0, b1):
    det = r00 * r11 - r01 * r01
    if abs(det) < 1e-6:
        return None
    return ((b0 * r11 - b1 * r01) / det, (b1 * r00 - b0 * r01) / det)


def make_book(pcm, iterations=16):
    """Eight (a1, a2) predictor pairs in Q11, clustered from per-frame LPC.

    Nintendo's encoder builds this with a split vector quantizer. Clustering the
    per-frame least-squares solutions and then re-solving each cluster over its
    pooled autocorrelation lands within a couple of dB of it and is far easier
    to verify.
    """
    frames = []
    h1 = h2 = 0
    for i in range(0, len(pcm), 14):
        frame = pcm[i:i + 14]
        eq = _normal_eq(frame, h1, h2)
        sol = _solve(*eq)
        if sol and abs(sol[0]) < 4 and abs(sol[1]) < 4:
            frames.append((sol, eq))
        if frame:
            h2 = frame[-2] if len(frame) > 1 else h1
            h1 = frame[-1]
    if not frames:
        return [0] * 16
    pts = sorted(s for s, _ in frames)
    cent = [pts[min(len(pts) - 1, (i * len(pts)) // 8)] for i in range(8)]
    for _ in range(iterations):
        pooled = [[0.0] * 5 for _ in range(8)]
        for sol, eq in frames:
            k = min(range(8), key=lambda j: (sol[0] - cent[j][0]) ** 2 + (sol[1] - cent[j][1]) ** 2)
            for t in range(5):
                pooled[k][t] += eq[t]
        for j in range(8):
            sol = _solve(*pooled[j])
            if sol and abs(sol[0]) < 4 and abs(sol[1]) < 4:
                cent[j] = sol
    book = []
    for a1, a2 in cent:
        book.append(max(-32768, min(32767, int(round(a1 * 2048)))))
        book.append(max(-32768, min(32767, int(round(a2 * 2048)))))
    return book


def snr(ref, test):
    n = min(len(ref), len(test))
    sig = sum(float(s) * s for s in ref[:n])
    noise = sum((float(a) - b) ** 2 for a, b in zip(ref[:n], test[:n]))
    if noise == 0:
        return float('inf')
    import math
    return 10 * math.log10(sig / noise) if sig else 0.0
