#!/usr/bin/env python3
"""Analyze a raw stereo capture from retro_host.

Usage: analyze_audio.py <capture.raw> <s16|f32> <rate> [ref.raw ref_rate]

Reports, per 1-second window and overall:
  - RMS level, peak, clip fraction
  - "garbage" metrics: spectral flatness (white-noise-like content),
    sample-to-sample delta energy (uninitialized-memory playback is
    broadband and has huge consecutive deltas), DC offset
  - dominant spectral peaks (Hz) so tones can be compared across rates

If a reference capture is given, both are windowed to the overlapping
duration, per-window band energies (in Hz, rate-independent) are compared,
and the correlation of the log-spectra is printed: correct resampling gives
high correlation; garbage or pitch-shifted audio does not.
"""
import sys, struct
import numpy as np

def load(path, fmt, rate):
    raw = open(path, 'rb').read()
    if fmt == 'f32':
        a = np.frombuffer(raw, dtype='<f4')
    else:
        a = np.frombuffer(raw, dtype='<i2').astype(np.float32) / 32768.0
    a = a[: (len(a)//2)*2 ].reshape(-1, 2)
    return a

def spectral_profile(x, rate, nbands=64, fmax=16000.0):
    # average log-magnitude in fixed Hz bands -> rate-independent signature
    n = len(x)
    if n < 4096: return None
    win = np.hanning(4096)
    prof = np.zeros(nbands); cnt = 0
    for off in range(0, n-4096, 4096):
        seg = x[off:off+4096] * win
        mag = np.abs(np.fft.rfft(seg))
        freqs = np.fft.rfftfreq(4096, 1.0/rate)
        idx = np.minimum((freqs / fmax * nbands).astype(int), nbands-1)
        band = np.zeros(nbands)
        np.add.at(band, idx, mag)
        prof += np.log10(band + 1e-9); cnt += 1
    return prof / max(cnt,1)

def report(path, fmt, rate):
    a = load(path, fmt, rate)
    mono = a.mean(axis=1)
    dur = len(a)/rate
    rms = float(np.sqrt(np.mean(mono**2)))
    peak = float(np.max(np.abs(a))) if len(a) else 0.0
    clip = float(np.mean(np.abs(a) >= 0.999))
    dc = float(np.mean(mono))
    # consecutive-delta energy relative to signal energy: music/sfx ~ <1.0,
    # white noise ~ 2.0, uninitialized-memory garbage >> 1.5 with high level
    d = np.diff(mono)
    delta_ratio = float(np.mean(d**2) / (np.mean(mono**2) + 1e-12))
    # spectral flatness on the loudest second
    best = None; bestrms = -1
    for off in range(0, max(1,len(mono)-rate), rate):
        w = mono[off:off+rate]
        r = np.sqrt(np.mean(w**2))
        if r > bestrms: bestrms, best = r, w
    flat = 0.0; peaks = []
    if best is not None and len(best) >= 8192:
        seg = best[:8192] * np.hanning(8192)
        mag = np.abs(np.fft.rfft(seg)) + 1e-12
        flat = float(np.exp(np.mean(np.log(mag))) / np.mean(mag))
        freqs = np.fft.rfftfreq(8192, 1.0/rate)
        top = np.argsort(mag)[-6:][::-1]
        peaks = sorted(set(round(float(freqs[i])) for i in top))
    print(f"{path}: {dur:.2f}s @ {rate}Hz  rms={rms:.4f} peak={peak:.3f} "
          f"clip={clip*100:.2f}% dc={dc:+.4f} deltaE/E={delta_ratio:.2f} "
          f"flatness={flat:.3f}")
    print(f"  peaks(Hz): {peaks}")
    verdict = []
    if clip > 0.02: verdict.append("HEAVY CLIPPING")
    if delta_ratio > 1.5 and rms > 0.05: verdict.append("BROADBAND/GARBAGE-LIKE")
    if flat > 0.30 and rms > 0.05: verdict.append("NOISE-LIKE SPECTRUM")
    print("  verdict:", ", ".join(verdict) if verdict else "looks like coherent audio")
    return a, mono

def main():
    path, fmt, rate = sys.argv[1], sys.argv[2], int(sys.argv[3])
    a, mono = report(path, fmt, rate)
    if len(sys.argv) > 5:
        rpath, rrate = sys.argv[4], int(sys.argv[5])
        rfmt = sys.argv[6] if len(sys.argv) > 6 else fmt
        ra = load(rpath, rfmt, rrate); rmono = ra.mean(axis=1)
        p1 = spectral_profile(mono, rate)
        p2 = spectral_profile(rmono, rrate)
        if p1 is not None and p2 is not None:
            c = float(np.corrcoef(p1, p2)[0,1])
            print(f"  spectral-profile correlation vs {rpath}: {c:.4f} "
                  f"({'MATCH' if c > 0.97 else 'MISMATCH' if c < 0.90 else 'weak'})")

if __name__ == '__main__':
    main()
