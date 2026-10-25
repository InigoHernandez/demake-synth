#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Demake's sample bank (docs/plan.md, 2.4 and 2.5): the owner's 64 recordings, made with their own pedals, cut, looped
and encoded as 4-bit block ADPCM into firmware/gen/dmk_samples.h, with a row each in assets/samples/PROCESSING.md.

  .venv/bin/python tools/gen_samples.py [--wav build/samples]

The originals are read from DMK_SAMPLES (the owner's folder, untouched) and never written. Every sound gets a new short
name; the originals are referred to by their number only.

The pipeline, per recording:
  1. read it (48 kHz, 16-bit, mono), take out DC;
  2. find its note: the fundamental by YIN, its nearest note and the cents off it (or the C under it, when that C is
     strong: a C with a louder fifth); most are Cs, a few an F, a G, an E;
  3. pick its rate from its bandwidth: 11,025 Hz, or 22,050 Hz if it has energy above 4.5 kHz;
  4. resample with a band limit (a Kaiser-windowed sinc);
  5. cut it: a sustained sound to 0.9 s with a loop over its last 0.3-0.45 s, the loop's start chosen where the
     wave matches its end best, an equal-power crossfade of 80 ms baked across the seam; a sound that dies away
     (keys, plucks) to where it falls under -50 dB, at most 1.2 s, one-shot;
  6. normalise to -1 dBFS, fade in over 2 ms;
  7. encode: 16-byte blocks of 28 samples, each the best of 5 two-tap predictors and 13 shifts (our own encoder,
     after the format's public description), the decoder's state at the loop's first block kept for the jump back;
  8. decode it again and print the SNR.
"""
import argparse
import os
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "firmware" / "gen" / "dmk_samples.h"
DOC = ROOT / "assets" / "samples" / "PROCESSING.md"
SRC = Path(os.environ.get("DMK_SAMPLES", Path.home() / "Documents/Music/Git/Demake/sample sounds"))
SR_IN = 48000
FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]
BANK_A = 32                                              # the FM-1's bank in the app slot (2.5); the owner picks it

# the names: one word each, the Latin name of a plant that grows where the sound's landscape is (the owner, 2026-10-24):
# alpine and snow plants for the peaks, water plants for the lake, seaside plants for the coast, night-blooming and
# shade plants for the night, houseplants for the room, succulents for the desk; woodland plants for the forest, a
# living room's for the living room, Mediterranean ones for the villa, Californian for the street, a harbour's marsh
# for the harbour, cliff-top plants for the lighthouse; given in the recordings' order, a pool
# a scene and kind
NAMES = {
    (0, "pad"): ["Soldanella", "Galanthus", "Saxifraga", "Dryas", "Gentiana", "Leontopodium", "Chionodoxa",
                 "Pulsatilla", "Androsace", "Draba", "Cassiope", "Abies", "Picea", "Larix", "Betula", "Diapensia"],
    (1, "key"): ["Nymphaea", "Nuphar", "Nelumbo", "Caltha", "Myosotis", "Menyanthes", "Sagittaria", "Salix", "Typha"],
    (2, "pad"): ["Armeria", "Limonium", "Crithmum", "Ammophila", "Eryngium", "Salicornia", "Zostera", "Crambe", "Cakile",
                 "Glaucium", "Tamarix", "Lavandula", "Calystegia", "Elymus", "Helichrysum", "Santolina"],
    (3, "pad"): ["Lunaria", "Oenothera", "Hesperis", "Cestrum", "Ipomoea", "Datura", "Nicotiana", "Matthiola",
                 "Selenicereus", "Mirabilis", "Epiphyllum", "Papaver", "Monotropa", "Viscum", "Hedera", "Valeriana"],
    (3, "bass"): ["Mandragora", "Taxus", "Atropa", "Aconitum", "Solanum", "Ilex"],
    (4, "pad"): ["Monstera", "Ficus", "Epipremnum", "Sansevieria", "Pilea", "Calathea", "Philodendron", "Peperomia",
                 "Maranta", "Hoya", "Chlorophytum", "Begonia", "Aspidistra", "Fittonia", "Tradescantia", "Oxalis"],
    (5, "key"): ["Echeveria", "Haworthia", "Crassula", "Gasteria", "Kalanchoe", "Sempervivum", "Graptopetalum"],
    (5, "pluck"): ["Lithops", "Aloe", "Mammillaria", "Rebutia", "Opuntia"],
    # the later six (a sound that moved to one is named for it; one that stayed keeps its name)
    (6, "pad"): ["Quercus", "Fagus", "Dryopteris", "Anemone", "Digitalis", "Convallaria", "Vaccinium", "Hepatica"],
    (7, "key"): ["Dracaena", "Strelitzia", "Schefflera", "Aglaonema"],
    (8, "pad"): ["Olea", "Cupressus", "Nerium", "Jasminum", "Citrus", "Laurus", "Cistus", "Punica"],
    (9, "pluck"): ["Washingtonia", "Eschscholzia", "Yucca", "Encelia"],
    (10, "pad"): ["Spartina", "Phragmites", "Juncus", "Suaeda", "Halimione", "Ruppia", "Puccinellia"],
    (10, "bass"): ["Scirpus", "Carex"],
    (11, "pad"): ["Silene", "Cochlearia", "Calluna", "Ulex", "Asplenium", "Mertensia", "Armoracia"],
}


def load(path):
    w = wave.open(str(path))
    assert w.getframerate() == SR_IN and w.getsampwidth() == 2, path
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float64) / 32768
    if w.getnchannels() == 2:
        x = x.reshape(-1, 2).mean(1)
    return x - x.mean()


def yin(x, sr, fmin=40, fmax=1200):
    """the fundamental (Hz) of a held note, the median of YIN over windows of its sustain"""
    W = 2048
    tmax, tmin = int(sr / fmin), int(sr / fmax)
    fs = []
    for s in range(int(0.25 * sr), min(len(x) - W - tmax, int(1.6 * sr)), W // 2):
        f = x[s:s + W + tmax]
        d = np.array([np.sum((f[:W] - f[t:t + W]) ** 2) for t in range(tmax)])
        c = np.cumsum(d[1:])
        cm = np.ones(tmax)
        cm[1:] = d[1:] * np.arange(1, tmax) / np.maximum(c, 1e-12)
        idx = np.where(cm[tmin:] < 0.15)[0]
        t = (idx[0] + tmin) if len(idx) else int(np.argmin(cm[tmin:]) + tmin)
        while t + 1 < tmax and cm[t + 1] < cm[t]:
            t += 1
        if 1 <= t < tmax - 1:                            # parabolic refinement
            a, b, cc = cm[t - 1], cm[t], cm[t + 1]
            t = t + 0.5 * (a - cc) / max(a - 2 * b + cc, 1e-12)
        fs.append(sr / t)
    return float(np.median(fs)) if fs else 261.63


def note_of(x):
    """its root (MIDI) and the cents off it: the nearest note to YIN's fundamental, or the C under it when that C is
    strong in the spectrum (a C held with its fifth louder than itself)"""
    f0 = yin(x, SR_IN)
    seg = x[int(0.4 * SR_IN):int(1.4 * SR_IN)]
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    fr = np.fft.rfftfreq(len(seg), 1 / SR_IN)
    top = sp[(fr > 40) & (fr < 4000)].max()
    for c in (36, 48, 60, 72):
        fc = 440 * 2 ** ((c - 69) / 12)
        if fc < f0 * 0.95 and f0 / fc < 3.2:
            band = (fr > fc * 0.97) & (fr < fc * 1.03)
            if band.any() and sp[band].max() > 0.25 * top:
                k = np.argmax(np.where(band, sp, 0))
                f0 = fr[k]
                break
    midi = 69 + 12 * np.log2(f0 / 440)
    root = int(round(midi))
    return root, int(np.clip(round((midi - root) * 100), -50, 50))


def resample(x, sr_out, taps=48):
    """band-limited resampling: a Kaiser-windowed sinc, its cutoff at 0.45 of the lower rate"""
    r = sr_out / SR_IN
    n_out = int(len(x) * r)
    t = np.arange(n_out) / r
    fc = 0.45 * min(1.0, r) * 2                           # in input samples' Nyquist units
    k0 = np.floor(t).astype(np.int64)
    y = np.zeros(n_out)
    beta = 8.0
    for j in range(-taps, taps + 1):
        k = k0 + j
        valid = (k >= 0) & (k < len(x))
        d = t - k
        h = fc * np.sinc(fc * d) * np.i0(beta * np.sqrt(np.clip(1 - (d / (taps + 1)) ** 2, 0, 1))) / np.i0(beta)
        y[valid] += x[k[valid]] * h[valid]
    return y


def rms_env(x, sr, win=0.02):
    n = max(1, int(win * sr))
    p = np.convolve(x * x, np.ones(n) / n, mode="same")
    return np.sqrt(p)


KINDS = ["pad", "key", "pluck", "bass"]


def kind_of(x, sr, root):
    env = rms_env(x, sr)
    pk = env.max() + 1e-12
    att = np.argmax(env > 0.9 * pk) / sr
    end = env[int(1.75 * sr):int(1.95 * sr)].mean() / pk if len(x) > 1.95 * sr else env[-int(0.2 * sr):].mean() / pk
    if root <= 43:
        return "bass", att, end
    if att < 0.03 and end < 0.1:
        return "pluck", att, end
    if att < 0.05 and end < 0.5:
        return "key", att, end
    return "pad", att, end


# the cuts: all 64 in the FM-1's app slot (2026-10-24, the owner: "all 64, like a rompler"), so a held sound is a
# second with its loop over the last 0.3-0.5 s, and a sound that dies away at most 1.3 s
SUS_CUT, LOOP_MIN, LOOP_MAX, ONE_MAX = 0.9, 0.3, 0.45, 1.2


def cut_and_loop(y, sr, kind, end_level):
    """the cut, the loop (in blocks of 28), the crossfade baked in"""
    B = 28
    y = y.copy()
    if kind in ("pluck", "key") and end_level < 0.15:    # it dies away: one-shot, to -50 dB or 1.6 s
        env = rms_env(y, sr)
        pk = env.max()
        below = np.where(env < pk * 10 ** (-50 / 20))[0]
        below = below[below > np.argmax(env)]
        n = min(len(y), int(ONE_MAX * sr), below[0] if len(below) else len(y))
        n = (n // B) * B
        y = y[:n]
        fo = min(int(0.05 * sr), n // 4)
        y[-fo:] *= np.linspace(1, 0, fo)
        return y, None, None
    n = (min(len(y), int(SUS_CUT * sr)) // B) * B
    lend = n // B
    xf = int(0.08 * sr)
    best, bstart = -2.0, None
    w = int(0.025 * sr)
    for lb in range(lend - int(LOOP_MAX * sr) // B, lend - int(LOOP_MIN * sr) // B + 1):
        s = lb * B
        if s - xf - w < 0:
            continue
        a, b = y[n - w:n], y[s - w:s]                    # the wave just before the end against just before the start
        sc = float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-12))
        lv = min(np.std(a), np.std(b)) / (max(np.std(a), np.std(b)) + 1e-12)
        sc = sc * 0.7 + lv * 0.3
        if sc > best:
            best, bstart = sc, lb
    s = bstart * B
    t = np.linspace(0, np.pi / 2, xf)
    y[n - xf:n] = y[n - xf:n] * np.cos(t) + y[s - xf:s] * np.sin(t)   # the end becomes the start, power kept
    return y[:n], bstart, lend


def encode(x16):
    """PS-style ADPCM: 16-byte blocks (filter << 4 | shift, flags, 14 bytes of 28 nibbles, low first); returns the
    bytes, the decoded samples and the decoder's state before every block"""
    n = len(x16) // 28
    out = bytearray()
    dec = np.zeros(n * 28, np.int64)
    states = []
    s1 = s2 = 0
    cand = [(f, s) for f in range(5) for s in range(13)]
    F0 = np.array([FILTERS[f][0] for f, _ in cand], np.int64)
    F1 = np.array([FILTERS[f][1] for f, _ in cand], np.int64)
    SH = np.array([s for _, s in cand], np.int64)
    for b in range(n):
        states.append((s1, s2))
        blk = x16[b * 28:(b + 1) * 28].astype(np.int64)
        p1 = np.full(len(cand), s1, np.int64)
        p2 = np.full(len(cand), s2, np.int64)
        err = np.zeros(len(cand), np.float64)
        nib = np.zeros((len(cand), 28), np.int64)
        for i in range(28):
            pred = (p1 * F0 + p2 * F1 + 32) >> 6
            e = blk[i] - pred
            q = np.round(e * (2.0 ** SH) / 4096.0).astype(np.int64)
            q = np.clip(q, -8, 7)
            d = np.clip(((q << 12) >> SH) + pred, -32768, 32767)
            err += (blk[i] - d) ** 2.0
            nib[:, i] = q
            p2 = p1
            p1 = d
        k = int(np.argmin(err))
        f, s = cand[k]
        out += bytes([(f << 4) | s, 0])
        for i in range(0, 28, 2):
            out.append((int(nib[k, i]) & 15) | ((int(nib[k, i + 1]) & 15) << 4))
        # decode the chosen one, the state carried on
        for i in range(28):
            pred = (s1 * FILTERS[f][0] + s2 * FILTERS[f][1] + 32) >> 6
            q = int(nib[k, i])
            d = max(-32768, min(32767, ((q << 12) >> s) + pred))
            dec[b * 28 + i] = d
            s2, s1 = s1, d
    return bytes(out), dec, states


def write_wav(path, x, sr):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(np.clip(x, -32768, 32767).astype(np.int16).tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", help="write each encoded sound, decoded, looped to 3 s, here")
    a = ap.parse_args()
    files = sorted(SRC.glob("*.wav"))
    if not files:
        raise SystemExit(f"no recordings in {SRC} (DMK_SAMPLES)")
    rows, entries = [], []
    for num, f in enumerate(files, 1):
        x = load(f)
        root, cents = note_of(x)
        spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
        fr = np.fft.rfftfreq(len(x), 1 / SR_IN)
        hi = 10 * np.log10(spec[fr > 4500].sum() / (spec.sum() + 1e-20) + 1e-20)
        lo = 10 * np.log10(spec[fr > 3500].sum() / (spec.sum() + 1e-20) + 1e-20)
        sr = 22050 if hi > -35 else 11025 if lo > -48 else 8000   # the darkest lose nothing under 4 kHz
        y = resample(x, sr)
        kind, att, endl = kind_of(y, sr, root)
        y, l0, l1 = cut_and_loop(y, sr, kind, endl)
        y = y / (np.abs(y).max() + 1e-12) * 0.891
        fi = int(0.002 * sr)
        y[:fi] *= np.linspace(0, 1, fi)
        x16 = np.round(y * 32767).astype(np.int64)
        x16 = np.concatenate([x16, np.zeros((-len(x16)) % 28, np.int64)])
        data, dec, states = encode(x16)
        snr = 10 * np.log10(np.sum(x16.astype(float) ** 2) / (np.sum((x16 - dec).astype(float) ** 2) + 1e-9))
        blocks = len(data) // 16
        loop = l0 is not None
        ls = states[l0] if loop else (0, 0)
        seg = np.array_split(np.abs(dec), 48)                 # its shape for the screen: 48 peaks, 0..255
        env = [min(255, int(round(255 * float(g.max()) / 32767))) for g in seg]
        cen = float((fr * spec).sum() / (spec.sum() + 1e-20))   # its brightness: where its energy sits
        entries.append(dict(num=num, kind=kind, root=root, cents=cents, sr=sr, blocks=blocks, cen=cen, snr=snr, dec=dec,
                            l0=l0 if loop else 0, l1=l1 if loop else blocks, loop=loop, ls=ls, data=data, env=env,
                            loop_s=(l1 - l0) * 28 / sr if loop else 0))
    # bank A first: the keys, plucks and basses, then the pads, to BANK_A; then the rest in their order
    # the landscapes (0 peaks, 1 lake, 2 coast, 3 night, 4 room, 5 desk): the pads by brightness, a quarter each to
    # the night (the darkest), the room, the coast and the peaks (the brightest); the keys by brightness too, the
    # darker by the lake, the brighter on the desk with the plucks; the basses at night
    pads = sorted((e for e in entries if e["kind"] == "pad"), key=lambda e: e["cen"])
    keys = sorted((e for e in entries if e["kind"] == "key"), key=lambda e: e["cen"])
    for i, e in enumerate(pads):
        e["named"] = (3, 4, 2, 0)[i * 4 // len(pads)]
    for i, e in enumerate(keys):
        e["named"] = 1 if i < len(keys) // 2 else 5
    for e in entries:
        if e["kind"] in ("pluck", "bass"):
            e["named"] = {"pluck": 5, "bass": 3}[e["kind"]]
    # since 0.5, twelve landscapes (the owner's six more: 6 forest, 7 living room, 8 villa, 9 California, 10 harbour,
    # 11 lighthouse): the pads by brightness, an eighth each, from the night (the darkest) through the forest, the
    # harbour in fog, the room, the lighthouse, the coast and the villa to the peaks (the brightest); the keys by
    # brightness, a third each, to the lake, the living room, the desk; the plucks to California; the basses half to
    # the harbour, half to the night. A name stays the one its first landscape gave it (`named`): no sound renamed
    for i, e in enumerate(pads):
        e["scene"] = (3, 6, 10, 4, 11, 2, 8, 0)[i * 8 // len(pads)]
    for i, e in enumerate(keys):
        e["scene"] = (1, 7, 5)[i * 3 // len(keys)]
    basses = [e for e in entries if e["kind"] == "bass"]
    for i, e in enumerate(basses):
        e["scene"] = 10 if i < len(basses) // 2 else 3
    for e in entries:
        if e["kind"] == "pluck":
            e["scene"] = 9
    used = {}                                           # the names, by the first landscape and kind, in the recordings' order
    for e in entries:
        k = (e["named"], e["kind"])
        pool = NAMES[k]
        e["name"] = pool[used.get(k, 0) % len(pool)]
        used[k] = used.get(k, 0) + 1
    used = {}                                           # then a sound that moved to a later landscape: a plant of it
    for e in entries:                                   # (the owner, 2026-10-25)
        if e["scene"] != e["named"]:
            k = (e["scene"], e["kind"])
            e["name"] = NAMES[k][used.get(k, 0) % len(NAMES[k])]
            used[k] = used.get(k, 0) + 1
    for e in entries:
        assert len(e["name"]) <= 12, e["name"]
    assert len({e["name"] for e in entries}) == len(entries), "a name given twice"
    for e in entries:
        num, name, kind, root, cents, sr, blocks = e["num"], e["name"], e["kind"], e["root"], e["cents"], e["sr"], e["blocks"]
        rows.append(f"| {num:02d} | {name} | {kind} | {root} | {cents:+d} | {sr} | {blocks * 28 / sr:.2f} s | "
                    f"{f'{e['loop_s']:.2f} s' if e['loop'] else 'one-shot'} | {e['snr']:.1f} dB | {len(e['data'])} B |")
        nn = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][root % 12] + str(root // 12 - 1)
        print(f"{num:02d} {name:12s} {kind:5s} {nn:>3s}{cents:+3d}c {sr:5d} Hz {blocks * 28 / sr:4.2f} s "
              f"{'loop' if e['loop'] else 'one '} SNR {e['snr']:4.1f} dB {len(e['data']):6d} B")
        if a.wav:
            pcm = e["dec"].astype(float)
            if e["loop"]:
                body, lp = pcm[:e["l1"] * 28], pcm[e["l0"] * 28:e["l1"] * 28]
                while len(body) < 3 * sr:
                    body = np.concatenate([body, lp])
                pcm = body[:3 * sr]
            write_wav(Path(a.wav) / f"{num:02d}_{name}.wav", pcm, sr)
    order = sorted(entries, key=lambda e: (e["kind"] == "pad", e["num"]))
    bank_a = order[:BANK_A]
    rest = [e for e in entries if e not in bank_a]
    bank_a = sorted(bank_a, key=lambda e: e["num"])
    ordered = bank_a + rest
    # the three synths' first sounds: a pad, a key, a pluck, named as the screen shows them
    L = ["/* SPDX-License-Identifier: GPL-3.0-only */",
         "/* generated by tools/gen_samples.py from the owner's recordings: do not edit */",
         "#pragma once", "#include <stdint.h>", "",
         "typedef struct {",
         "    char name[13];",
         "    uint8_t root;            /* the note it was played at, MIDI */",
         "    int8_t cents;            /* how far off it, -50..50 */",
         "    uint8_t loops;           /* 1: loops from loop0 to loop1, else one-shot */",
         "    uint8_t kind;            /* 0 pad, 1 key, 2 pluck, 3 bass */",
         "    uint8_t scene;           /* Home's landscape: 0 peaks, 1 lake, 2 coast, 3 night, 4 room, 5 desk, 6 forest,",
         "                                7 living room, 8 villa, 9 California, 10 harbour, 11 lighthouse */",
         "    uint32_t rate_q16;       /* its rate over 44.1 kHz, Q16 */",
         "    uint16_t blocks, loop0, loop1;",
         "    int16_t ls1, ls2;        /* the decoder's state entering loop0 */",
         "    const uint8_t *data;     /* 16 B a block of 28 samples */",
         "    uint8_t env[48];         /* its shape, 48 peaks 0..255, for the screen */",
         "} dmk_sample_t;", "",
         f"#define DMK_BANK_A {BANK_A}",
         "#ifndef DMK_SAMPLES_N", f"#define DMK_SAMPLES_N {len(ordered)}", "#endif",
         "/* the first song's sounds: the first recording of a pad, of keys, of a pluck */",
         *[f"#define DMK_FIRST_{k.upper()} {next(i for i, e in enumerate(ordered) if e['kind'] == k)}" for k in ("pad", "key", "pluck")],
         "#define DMK_SCENES 12", ""]
    for i, e in enumerate(ordered):
        L.append(f"#if DMK_SAMPLES_N > {i}")
        L.append(f"static const uint8_t SMP{i:02d}[{len(e['data'])}] = {{" + ",".join(map(str, e["data"])) + "};")
        L.append("#endif")
    L.append("static const dmk_sample_t DMK_SAMPLES[DMK_SAMPLES_N] = {")
    for i, e in enumerate(ordered):
        rq = round(e["sr"] * 65536 / 44100)
        L.append(f"#if DMK_SAMPLES_N > {i}")
        L.append(f"    {{\"{e['name']}\", {e['root']}, {e['cents']}, {int(e['loop'])}, {KINDS.index(e['kind'])}, {e['scene']}, {rq}u, {e['blocks']}, {e['l0']}, "
                 f"{e['l1']}, {e['ls'][0]}, {e['ls'][1]}, SMP{i:02d}, {{" + ",".join(map(str, e["env"])) + f"}}}},   /* {e['num']:02d}, {e['kind']} */")
        L.append("#endif")
    L.append("};")
    OUT.write_text("\n".join(L) + "\n")
    tot = sum(len(e["data"]) for e in ordered)
    ta = sum(len(e["data"]) for e in bank_a)
    print(f"{OUT.relative_to(ROOT)}: {len(ordered)} sounds, {tot} B ({ta} B in bank A, the first {BANK_A})")
    DOC.parent.mkdir(parents=True, exist_ok=True)
    DOC.write_text("# The sample bank\n\nThe owner's own recordings (made with their pedals), each processed by "
                   "`tools/gen_samples.py` (its docstring is the pipeline). Each is named after a plant of its landscape, its Latin name; the "
                   "originals are numbered as in the owner's folder. Bank A (the FM-1's app slot): "
                   + ", ".join(f"{e['num']:02d}" for e in bank_a) + ".\n\n"
                   "| # | Name | Kind | Root | Cents | Rate | Cut | Loop | SNR | Size |\n"
                   "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n" + "\n".join(rows) + "\n")
    print(DOC.relative_to(ROOT))


if __name__ == "__main__":
    main()
