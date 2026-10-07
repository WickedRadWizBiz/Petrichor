#!/usr/bin/env python3
"""
Analyses the Salamander Grand Piano V3 (Yamaha C5, Alexander Holm, CC-BY 3.0) into the data that
drives Piano I: every partial of every sampled key and velocity layer, fitted with the engine's own
two-mode model, plus the attack residual - the hammer, action and soundboard noise that is not on
any partial.

    python3 Tools/hybrid/analyse_salamander.py <Salamander>/Samples Source/DSP/Data/piano_hybrid.bin

Needs numpy, scipy and ffmpeg (to decode the FLACs). Takes a few minutes on 4 cores.

Per partial n of a recording x(t) (left and right analysed separately, powers averaged so the
spaced microphones never comb-filter each other):

    |X_n(t)| = | a1 e^(-s1 t) + a2 e^(-s2 t) e^(i (2 pi beat t + phase)) |

a1/s1 is the prompt mode, a2/s2 the aftersound mode, beat their frequency difference - exactly the
pair of complex resonators each partial gets in PianoVoice. Decay rates, the aftersound and the
beat are string properties, so they are pooled over the velocity layers; the amplitudes are kept
per layer, which captures how the spectrum brightens as the hammer hits harder.

The residual is what is left when every partial is removed: each partial is heterodyned to DC,
low-passed (bandwidth below half the partial spacing) and re-modulated, and the sum of partials is
subtracted from the recording (a deterministic + stochastic split, after Serra and Smith's spectral
modelling synthesis). Its first 250 ms, mu-law coded, is the strike's directly heard noise.
"""

import argparse
import math
import os
import struct
import subprocess
import sys
from multiprocessing import Pool

import numpy as np
from scipy import signal
from scipy.optimize import least_squares

FS = 48000
NOTES = ["A0", "C1", "D#1", "F#1"] + [f"{n}{o}" for o in range(1, 8) for n in ("A", "C", "D#", "F#") if not (o == 1 and n in ("C", "D#", "F#"))]
NAMES = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
LAYERS = 16
# Centre MIDI velocity of each Salamander layer (from the SFZ's lovel / hivel).
LAYER_RANGES = [(1, 26), (27, 34), (35, 36), (37, 43), (44, 46), (47, 50), (51, 56), (57, 64), (65, 72),
                (73, 80), (81, 88), (89, 96), (97, 104), (105, 112), (113, 120), (121, 127)]
LAYER_VELOCITY = [0.5 * (a + b) for a, b in LAYER_RANGES]
RESIDUAL_LAYERS = [3, 8, 12, 16]
RESIDUAL_SECONDS = 0.25
MAX_PARTIALS = 96
ANALYSIS_SECONDS = 10.0


def note_key(name):
    pitch, octave = name[:-1], int(name[-1])
    return 12 * (octave + 1) + NAMES[pitch]


def anchor_notes():
    notes = sorted(set(NOTES + ["C8"]), key=note_key)
    assert len(notes) == 30, notes
    return notes


#==================================================================================================
def load(path):
    """Stereo float32 at 48 kHz: from a decoded .f32 file, or a FLAC via ffmpeg."""
    if path.endswith(".f32"):
        x = np.fromfile(path, dtype=np.float32)
    else:
        raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-ar", str(FS), "-t", "12", "-f", "f32le", "-"],
                             check=True, capture_output=True).stdout
        x = np.frombuffer(raw, dtype=np.float32)
    return x.reshape(-1, 2).astype(np.float64)


def find_onset(mono):
    a = np.abs(mono)
    i = int(np.argmax(a > 0.02 * a.max()))
    return max(i - int(0.002 * FS), 0)


def law_b(key):
    """PianoVoice::inharmonicity(), the starting guess."""
    k = min(max(key, 21), 108)
    lg = (math.log10(3e-4) + (-4.0 - math.log10(3e-4)) * (k - 21) / 24.0) if k <= 45 else (-4.0 + (math.log10(8e-3) + 4.0) * (k - 45) / 63.0)
    return 10 ** lg


def estimate_partials(mono, f_nominal, key):
    """Fundamental and inharmonicity from a long spectrum; returns (f0, B, measured {n: freq})."""
    seg = mono[int(0.05 * FS):int(1.5 * FS)]
    n_fft = 1 << int(math.ceil(math.log2(len(seg) * 8)))
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n_fft))
    freqs = np.fft.rfftfreq(n_fft, 1.0 / FS)
    B = law_b(key)
    # The tuning is stretched (the top octave by up to a semitone): find partial 1 within +-6 %,
    # or partial 2 for the low bass, whose fundamental barely radiates.
    f0 = f_nominal
    for n in ((2, 3) if f_nominal < 60 else (1,)):
        fp = n * f_nominal * math.sqrt(1 + B * n * n)
        lo, hi = np.searchsorted(freqs, fp * 0.94), np.searchsorted(freqs, fp * 1.06)
        i = lo + int(np.argmax(spec[lo:hi]))
        f0 = freqs[i] / (n * math.sqrt(1 + B * n * n))
        break
    found = {}
    for _ in range(4):
        found = {}
        for n in range(1, 61):
            fp = n * f0 * math.sqrt(1 + B * n * n)
            if fp > min(5000.0, 0.45 * FS):
                break
            half = min(0.3 * f0, 0.015 * fp + 2.0)
            lo, hi = np.searchsorted(freqs, fp - half), np.searchsorted(freqs, fp + half)
            if hi - lo < 3:
                continue
            i = lo + int(np.argmax(spec[lo:hi]))
            if spec[i] < 1e-3 * spec.max():
                continue
            found[n] = freqs[i]
        ns = np.array(sorted(found))
        if len(ns) >= 5:
            fs_ = np.array([found[n] for n in ns])
            A = np.vstack([np.ones_like(ns, float), ns.astype(float) ** 2]).T
            y = (fs_ / ns) ** 2
            c, *_ = np.linalg.lstsq(A, y, rcond=None)
            resid = np.abs(A @ c - y) / y
            keep = resid < 3 * np.median(resid) + 1e-6
            c, *_ = np.linalg.lstsq(A[keep], y[keep], rcond=None)
            if c[0] > 0:
                f0, B = math.sqrt(c[0]), max(c[1] / c[0], 0.0)
        elif len(ns) >= 1:
            f0 = found[ns[0]] / ns[0]
    return f0, B, found


def track(x, freqs, f0, onset):
    """Amplitude tracks of every partial (left/right powers averaged), and of the floor between them."""
    win = max(2048, 1 << int(math.ceil(math.log2(4.0 * FS / f0))))
    hop = 480
    n_fft = 2 * win
    w = np.hanning(win)
    scale = 2.0 / w.sum()
    end = min(x.shape[0], onset + int(ANALYSIS_SECONDS * FS))
    starts = np.arange(onset - win // 2, end - win, hop)
    if len(starts) < 20:
        return None
    bins = np.fft.rfftfreq(n_fft, 1.0 / FS)
    power = np.zeros((len(starts), len(bins)))
    for ch in range(2):
        xc = np.concatenate([np.zeros(win), x[:, ch], np.zeros(win)])
        frames = np.stack([xc[s + win:s + 2 * win] for s in starts])
        power += np.abs(np.fft.rfft(frames * w, n_fft, axis=1)) ** 2
    mag = np.sqrt(0.5 * power) * scale
    t = (starts + win // 2 - onset) / FS

    amps = np.zeros((len(starts), len(freqs)))
    floors = np.zeros_like(amps)
    for j, fp in enumerate(freqs):
        half = min(0.3 * f0, 3 * FS / n_fft + 0.004 * fp)
        lo, hi = np.searchsorted(bins, fp - half), np.searchsorted(bins, fp + half) + 1
        amps[:, j] = mag[:, lo:hi].max(axis=1)
        # The floor: what sits between this partial and its neighbours.
        g_lo, g_hi = np.searchsorted(bins, fp + 0.4 * f0), np.searchsorted(bins, fp + 0.6 * f0) + 1
        floors[:, j] = np.median(mag[:, g_lo:g_hi], axis=1) if g_hi > g_lo else 0.0
    # Smooth the floor in time (it is noisy) and keep it from undercutting a quiet recording's hiss.
    k = 15
    floors = signal.medfilt(floors, kernel_size=(k, 1))
    return t, amps, floors, win


def model(p, t):
    a1, s1, a2, s2, beat, ph = p
    return np.abs(a1 * np.exp(-s1 * t) + a2 * np.exp(-s2 * t) * np.exp(1j * (2 * np.pi * beat * t + ph)))


def fit_partial(t, env, floor, t_min):
    """Two-mode fit in the log domain over the frames well above the floor. None if too few."""
    ok = (t >= t_min) & (env > 2.0 * floor) & (env > 1e-7)
    if ok.sum() < 12:
        return None
    # Use only the leading run of valid frames (once it sinks into the floor, it stays there).
    idx = np.nonzero(ok)[0]
    last = idx[0]
    for i in idx[1:]:
        if i - last > 25:
            break
        last = i
    sel = idx[idx <= last]
    if len(sel) < 12:
        return None
    # Thin out the slow tail: 10 ms frames for the first 2 s, then every 4th.
    sel = sel[(t[sel] < 2.0) | (np.arange(len(sel)) % 4 == 0)]
    tt, y = t[sel], env[sel]
    ly = np.log(y)
    span = tt[-1] - tt[0]
    early = tt < tt[0] + max(0.15, 0.25 * span)
    late = tt > tt[0] + 0.5 * span
    se = max(-np.polyfit(tt[early], ly[early], 1)[0], 0.05) if early.sum() >= 3 else 1.0
    pl = np.polyfit(tt[late], ly[late], 1) if late.sum() >= 3 else (np.array([-se, ly[-1] + se * tt[-1]]))
    sl = max(-pl[0], 0.02)
    a0 = y[0] * math.exp(se * tt[0])
    best = None
    for beat in (0.2, 1.0):
        for ratio in (0.08, 0.3):
            p0 = [a0 * (1 - ratio), max(se, sl * 1.2), max(math.exp(pl[1]), a0 * ratio * 0.3), sl, beat, 0.0]
            lo = [0, 0, 0, 0.0, 0, -np.pi]
            hi = [a0 * 20 + 1e-9, 80, a0 * 20 + 1e-9, 30, 5.0, np.pi]
            p0 = np.clip(p0, np.array(lo) + 1e-12, np.array(hi) - 1e-12)
            try:
                r = least_squares(lambda p: np.log(model(p, tt) + 1e-9) - ly, p0, bounds=(lo, hi), max_nfev=200)
            except ValueError:
                continue
            if best is None or r.cost < best.cost:
                best = r
    if best is None:
        return None
    a1, s1, a2, s2, beat, ph = best.x
    # The amplitudes are extrapolated back from the first frame seen to the strike (t = 0). A
    # partial that only rises out of the floor late would extrapolate absurdly: allow +12 dB at most.
    t0 = tt[0]
    a1 *= math.exp(min(s1 * t0, 1.4) - s1 * t0)
    a2 *= math.exp(min(s2 * t0, 1.4) - s2 * t0)
    # Convention: mode 1 is the prompt (faster) one.
    if s2 > s1:
        a1, s1, a2, s2, beat, ph = a2, s2, a1, s1, -beat, -ph
    rms = math.sqrt(2 * best.cost / len(tt))
    return dict(a1=a1, s1=s1, a2=a2, s2=s2, beat=beat, phase=ph, err_db=rms * 8.686, t_end=tt[-1])


def residual(x, onset, freqs, f0):
    """The recording minus every partial (heterodyned, low-passed, re-modulated), mono."""
    n = int(RESIDUAL_SECONDS * FS) + int(0.1 * FS)
    mono = 0.5 * (x[onset:onset + n, 0] + x[onset:onset + n, 1])
    if len(mono) < n:
        mono = np.concatenate([mono, np.zeros(n - len(mono))])
    t = np.arange(n) / FS
    bw = float(np.clip(0.35 * f0, 6.0, 80.0))
    b, a = signal.butter(4, bw / (0.5 * FS))
    pad = int(0.1 * FS)
    det = np.zeros(n)
    for fp in freqs:
        if fp >= 0.45 * FS:
            break
        lo = np.exp(-2j * np.pi * fp * t)
        z = mono * lo
        # Zero-phase low-pass; pad in front so the onset is not bent by the filter's edge.
        zz = np.concatenate([np.zeros(pad), z])
        zr = signal.filtfilt(b, a, zz.real)[pad:]
        zi = signal.filtfilt(b, a, zz.imag)[pad:]
        det += 2.0 * np.real((zr + 1j * zi) * np.conj(lo))
    res = (mono - det)[:int(RESIDUAL_SECONDS * FS)]
    fade = np.ones(len(res))
    m = int(0.08 * FS)
    fade[-m:] = np.cos(np.linspace(0, np.pi / 2, m)) ** 2
    res *= fade
    return res, float(np.sqrt(np.mean(mono[:len(res)] ** 2)))


def analyse(job):
    note, layer, path = job
    key = note_key(note)
    x = load(path)
    mono = 0.5 * (x[:, 0] + x[:, 1])
    onset = find_onset(mono)
    x = x[:min(x.shape[0], onset + int(ANALYSIS_SECONDS * FS) + 8192)]
    mono = 0.5 * (x[:, 0] + x[:, 1])
    f_nom = 440.0 * 2 ** ((key - 69) / 12)
    f0, B, found = estimate_partials(mono[onset:], f_nom, key)

    # Partial frequencies: measured where found, the stiff-string law elsewhere.
    freqs, ns = [], []
    for n in range(1, MAX_PARTIALS + 1):
        fp = found.get(n, n * f0 * math.sqrt(1 + B * n * n))
        if fp > min(18000.0, 0.45 * FS):
            break
        freqs.append(fp)
        ns.append(n)

    tr = track(x, freqs, f0, onset)
    fits = []
    if tr is not None:
        t, amps, floors, win = tr
        t_min = 0.35 * win / FS
        for j in range(len(freqs)):
            fits.append(fit_partial(t, amps[:, j], floors[:, j], t_min))
    res, rms = residual(x, onset, freqs, f0) if layer in RESIDUAL_LAYERS else (None, 0.0)
    return dict(note=note, key=key, layer=layer, f0=f0, B=B, freqs=freqs, found=sorted(found), fits=fits,
                residual=res, note_rms=rms)


#==================================================================================================
def mulaw_encode(x):
    peak = float(np.max(np.abs(x))) + 1e-12
    y = x / peak
    mu = 255.0
    c = np.sign(y) * np.log1p(mu * np.abs(y)) / np.log1p(mu)
    q = np.clip(np.round((c + 1.0) * 127.5), 0, 255).astype(np.uint8)
    return q, peak


def nan_interp(v):
    """Fills NaNs in a 1-D array by linear interpolation / edge hold."""
    v = np.array(v, float)
    ok = np.isfinite(v)
    if ok.sum() == 0:
        return v
    idx = np.arange(len(v))
    v[~ok] = np.interp(idx[~ok], idx[ok], v[ok])
    return v


def build_anchor(results):
    """Pools one key's 16 layers into the stored table."""
    results = sorted(results, key=lambda r: r["layer"])
    key = results[0]["key"]
    loud = [r for r in results if r["layer"] >= 9] or results
    f0 = float(np.median([r["f0"] for r in loud]))
    Bs = [r["B"] for r in loud if len(r["found"]) >= 6]
    B = float(np.median(Bs)) if Bs else float("nan")
    num = max(len(r["freqs"]) for r in results)

    # Frequency ratios to partial 1 (median over layers where the partial was found).
    ratio = np.full(num, np.nan)
    for n in range(1, num + 1):
        vals = [r["freqs"][n - 1] / r["f0"] / n for r in results if n in r["found"] and len(r["freqs"]) >= n]
        if vals:
            ratio[n - 1] = n * float(np.median(vals))
    law = np.array([n * math.sqrt(1 + (B if math.isfinite(B) else 1e-4) * n * n) for n in range(1, num + 1)])
    law /= law[0]
    ratio = np.where(np.isfinite(ratio), ratio, law)
    ratio /= ratio[0]

    # String properties pooled over the layers, weighted toward good fits of loud layers.
    pooled = {k: np.full(num, np.nan) for k in ("s1", "s2", "after_db", "beat", "phase")}
    amp = np.full((LAYERS, num), np.nan)
    for n in range(num):
        rows = []
        for r in results:
            f = r["fits"][n] if n < len(r["fits"]) else None
            if f is None:
                continue
            amp[r["layer"] - 1, n] = 20 * math.log10(max(f["a1"], 1e-12))
            w = (r["layer"] / 16.0) ** 2 / (0.3 + f["err_db"])
            rows.append((w, f))
        if not rows:
            continue
        ws = np.array([w for w, _ in rows])

        def wmedian(vals):
            o = np.argsort(vals)
            c = np.cumsum(ws[o])
            return float(np.asarray(vals)[o][np.searchsorted(c, 0.5 * c[-1])])

        pooled["s1"][n] = math.exp(wmedian([math.log(max(f["s1"], 1e-3)) for _, f in rows]))
        pooled["s2"][n] = math.exp(wmedian([math.log(max(f["s2"], 1e-3)) for _, f in rows]))
        pooled["after_db"][n] = wmedian([20 * math.log10(max(f["a2"], 1e-12) / max(f["a1"], 1e-12)) for _, f in rows])
        pooled["beat"][n] = wmedian([f["beat"] for _, f in rows])
        best = max(rows, key=lambda wf: wf[0])[1]
        pooled["phase"][n] = best["phase"]

    # Gaps: decays follow sigma = s1 + b f^2 across partials, so interpolate them in log; the
    # aftersound and beat by neighbours.
    for k in ("s1", "s2"):
        pooled[k] = np.exp(nan_interp(np.log(pooled[k])))
    # A 10 s recording cannot tell a 90 s aftersound from an endless one: cap the extrapolation.
    pooled["s1"] = np.clip(pooled["s1"], 6.91 / 40.0, 200.0)
    pooled["s2"] = np.clip(pooled["s2"], 6.91 / 90.0, 200.0)
    pooled["s2"] = np.minimum(pooled["s2"], pooled["s1"])
    pooled["after_db"] = np.clip(nan_interp(pooled["after_db"]), -60.0, 12.0)
    pooled["beat"] = np.clip(nan_interp(pooled["beat"]), -5.0, 5.0)
    pooled["phase"] = np.where(np.isfinite(pooled["phase"]), pooled["phase"], 0.0)
    if not np.any(np.isfinite(pooled["s1"])):
        raise RuntimeError(f"no partial fitted for key {key}")

    # Amplitudes: smooth over the layers (each partial's level grows smoothly with velocity), then
    # fill partials lost in the floor by continuing the spectrum's slope beyond the last one heard.
    for n in range(num):
        col = amp[:, n]
        ok = np.isfinite(col)
        if ok.sum() >= 3:
            lv = np.array(LAYER_VELOCITY)
            c = np.polyfit(np.log(lv[ok]), col[ok], 2 if ok.sum() >= 6 else 1)
            fitted = np.polyval(c, np.log(lv))
            # Keep the measurement where it exists, lightly pulled toward the smooth curve.
            amp[:, n] = np.where(ok, 0.5 * col + 0.5 * fitted, np.nan)
    for li in range(LAYERS):
        row = amp[li]
        ok = np.isfinite(row)
        if not ok.any():
            continue
        last = int(np.nonzero(ok)[0][-1])
        # Interior gaps (a strike-point notch, a partial under the floor): neighbours minus 10 dB.
        interior = nan_interp(row)
        row = np.where(ok, row, interior - 10.0)
        if last + 1 < num:
            ref = np.nonzero(ok)[0][-6:]
            octs = np.log2(ratio[ref])
            slope = np.polyfit(octs, row[ref], 1)[0] if len(ref) >= 3 else -12.0
            slope = min(slope, -6.0) - 6.0  # beyond the floor it is only getting darker
            row[last + 1:] = row[last] + slope * (np.log2(ratio[last + 1:]) - np.log2(ratio[last]))
        amp[li] = row
    # Safety nets against stray fits: no partial above the strongest of the first twelve by more than
    # 8 dB, and a softer layer never louder than the next louder one by more than 3 dB.
    for li in range(LAYERS):
        row = amp[li]
        if np.any(np.isfinite(row[:12])):
            amp[li] = np.minimum(row, np.nanmax(row[:12]) + 8.0)
    for li in range(LAYERS - 2, -1, -1):
        amp[li] = np.where(np.isfinite(amp[li]) & np.isfinite(amp[li + 1]), np.minimum(amp[li], amp[li + 1] + 3.0), amp[li])
    # Layers where a partial never appeared at all (quiet layers): fall back to the next louder layer.
    for li in range(LAYERS - 2, -1, -1):
        bad = ~np.isfinite(amp[li])
        amp[li, bad] = amp[li + 1, bad] - 6.0
    amp = np.nan_to_num(amp, nan=-160.0)
    amp = np.maximum(amp, -160.0)

    res_layers = []
    for layer in RESIDUAL_LAYERS:
        r = next(r for r in results if r["layer"] == layer)
        q, peak = mulaw_encode(r["residual"])
        res_layers.append((20 * math.log10(peak), q))

    return dict(key=key, f0=f0, B=B if math.isfinite(B) else 0.0, num=num, ratio=ratio, amp=amp,
                s1=pooled["s1"], s2=pooled["s2"], after_db=pooled["after_db"], beat=pooled["beat"],
                phase=pooled["phase"], residual=res_layers)


def write_blob(anchors, path):
    out = bytearray()
    out += b"PPHY"
    out += struct.pack("<6I", 1, len(anchors), LAYERS, MAX_PARTIALS, FS, len(RESIDUAL_LAYERS))
    out += struct.pack("<I", int(RESIDUAL_SECONDS * FS))
    out += struct.pack(f"<{LAYERS}f", *LAYER_VELOCITY)
    out += struct.pack(f"<{len(RESIDUAL_LAYERS)}f", *[LAYER_VELOCITY[l - 1] for l in RESIDUAL_LAYERS])
    for a in anchors:
        out += struct.pack("<iiff", a["key"], a["num"], a["f0"], a["B"])
        for n in range(a["num"]):
            out += struct.pack("<6f", a["ratio"][n], a["s1"][n], a["s2"][n], a["after_db"][n], a["beat"][n], a["phase"][n])
            out += struct.pack(f"<{LAYERS}f", *a["amp"][:, n])
    for a in anchors:
        for gain_db, q in a["residual"]:
            out += struct.pack("<f", gain_db)
            out += q.tobytes()
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("samples", help="Salamander 'Samples' folder (FLAC), or a folder of pre-decoded stereo .f32")
    ap.add_argument("out", help="output blob, e.g. Source/DSP/Data/piano_hybrid.bin")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--report", help="optional text report of the fits")
    args = ap.parse_args()

    jobs = []
    for note in anchor_notes():
        for layer in range(1, LAYERS + 1):
            stem = f"{note}v{layer}"
            path = os.path.join(args.samples, stem + ".f32")
            if not os.path.exists(path):
                path = os.path.join(args.samples, stem + ".flac")
            if not os.path.exists(path):
                sys.exit(f"missing sample {stem}")
            jobs.append((note, layer, path))

    with Pool(args.jobs) as pool:
        results = []
        for i, r in enumerate(pool.imap_unordered(analyse, jobs, chunksize=1)):
            results.append(r)
            if (i + 1) % 16 == 0:
                print(f"  analysed {i + 1}/{len(jobs)}", file=sys.stderr, flush=True)

    anchors = []
    for note in anchor_notes():
        anchors.append(build_anchor([r for r in results if r["note"] == note]))
    size = write_blob(anchors, args.out)
    print(f"wrote {args.out}: {len(anchors)} keys x {LAYERS} layers, {size / 1024:.0f} KiB")

    if args.report:
        with open(args.report, "w") as f:
            for a in anchors:
                f.write(f"key {a['key']:3d} f0 {a['f0']:8.2f} B {a['B']:.2e} partials {a['num']}\n")
                for n in range(min(a["num"], 12)):
                    f.write(f"   n{n + 1:2d} ratio {a['ratio'][n]:7.4f} T60 prompt {6.91 / a['s1'][n]:6.2f} s after {6.91 / a['s2'][n]:6.2f} s"
                            f" after {a['after_db'][n]:6.1f} dB beat {a['beat'][n]:5.2f} Hz  amp v4 {a['amp'][3, n]:6.1f} v10 {a['amp'][9, n]:6.1f} v16 {a['amp'][15, n]:6.1f}\n")
            fits = [f for r in results for f in r["fits"] if f is not None]
            errs = np.array([f["err_db"] for f in fits])
            f.write(f"\n{len(fits)} partial fits, median log error {np.median(errs):.2f} dB, 90th pct {np.percentile(errs, 90):.2f} dB\n")


if __name__ == "__main__":
    main()
