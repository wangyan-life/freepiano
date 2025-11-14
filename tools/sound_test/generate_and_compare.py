#!/usr/bin/env python3
"""
generate_and_compare.py

Creates a reference 440 Hz sine WAV (44100 Hz, 0.1 amplitude, 16-bit) and
compares it with an existing recorded WAV (tools/sound_test/captured.wav).
Prints dominant frequencies, frequency error, and an SNR-like metric comparing
reference vs recorded. Saves reference and comparison spectrums.

Usage:
  python generate_and_compare.py

Outputs:
  - tools/sound_test/reference_440.wav
  - tools/sound_test/reference_spectrum.png
  - tools/sound_test/diff_spectrum.png
"""
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile

ROOT = os.path.dirname(__file__)
DEFAULT_CAPTURED = os.path.join(ROOT, 'captured.wav')
ref_path = os.path.join(ROOT, 'reference_440.wav')

def write_ref(path, freq=440.0, sr=44100, dur=5.0, amp=0.1):
    t = np.arange(int(sr*dur)) / sr
    sig = amp * np.sin(2*np.pi*freq*t)
    # convert to int16
    int16 = np.int16(np.clip(sig, -1.0, 1.0) * 32767)
    wavfile.write(path, sr, int16)

def read_mono(path):
    sr, data = wavfile.read(path)
    if data.ndim > 1:
        data = data.mean(axis=1)
    dtype = data.dtype
    data_f = data.astype(np.float64)
    # normalize according to dtype
    if np.issubdtype(dtype, np.integer):
        # figure bits
        bits = data.dtype.itemsize * 8
        if bits == 16:
            data_f = data_f / 32767.0
        elif bits == 32:
            # signed 32-bit PCM
            data_f = data_f / 2147483647.0
        else:
            # fallback
            data_f = data_f / float(2**(bits-1)-1)
    elif np.issubdtype(dtype, np.floating):
        # float32/64 assumed already in -1..1
        data_f = data_f.astype(np.float64)
    return sr, data_f

def spectrum_plot(x, sr, out_png, title=None):
    N = len(x)
    win = np.hanning(N)
    fft = np.fft.rfft(x*win)
    freqs = np.fft.rfftfreq(N, 1.0/sr)
    mag = np.abs(fft)
    plt.figure(figsize=(8,4))
    plt.plot(freqs, 20*np.log10(mag+1e-12))
    plt.xlim(0, sr/2)
    plt.xlabel('Hz')
    plt.ylabel('dB')
    if title:
        plt.title(title)
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(out_png)
    plt.close()

def dominant_freq(x, sr):
    N = len(x)
    win = np.hanning(N)
    fft = np.fft.rfft(x*win)
    freqs = np.fft.rfftfreq(N, 1.0/sr)
    mag = np.abs(fft)
    idx = np.argmax(mag)
    return freqs[idx], mag

def main():
    # accept optional path to captured wav
    captured_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_CAPTURED

    print('Generating reference 440Hz ->', ref_path)
    write_ref(ref_path, freq=440.0, sr=44100, dur=5.0, amp=0.1)

    if not os.path.exists(captured_path):
        print('\nCaptured file not found:', captured_path)
        print('Reference generated. To compare, place your captured WAV at this path or run:')
        print('  python generate_and_compare.py path/to/your_captured.wav')
        return

    sr_ref, ref = read_mono(ref_path)
    sr_cap, cap = read_mono(captured_path)
    print('Ref SR:', sr_ref, 'Captured SR:', sr_cap)

    # trim to min length
    L = min(len(ref), len(cap))
    ref = ref[:L]
    cap = cap[:L]

    # normalize int16 to [-1,1]
    ref = ref / 32767.0
    # captured may be int16 already; normalize by its dtype max if needed
    cap = cap / 32767.0

    f_ref, mag_ref = dominant_freq(ref, sr_ref)
    f_cap, mag_cap = dominant_freq(cap, sr_cap)

    print(f'Reference dominant freq: {f_ref:.2f} Hz')
    print(f'Captured dominant freq: {f_cap:.2f} Hz')
    print(f'Frequency error: {(f_cap - f_ref):.2f} Hz ({(f_cap/f_ref-1)*100:.2f}%)')

    # compute simple SNR-like metric: rms(ref) vs rms(error)
    error = cap - ref
    rms_ref = np.sqrt(np.mean(ref**2))
    rms_err = np.sqrt(np.mean(error**2))
    snr_db = 20.0 * np.log10((rms_ref + 1e-12) / (rms_err + 1e-12))
    print(f'RMS ref: {rms_ref:.6f}, RMS error: {rms_err:.6f}, SNR(ref vs rec): {snr_db:.2f} dB')

    # harmonic magnitudes at multiples of f_ref
    print('\nHarmonic magnitudes (ref vs cap)')
    for h in range(1,6):
        target = f_ref * h
        freqs = np.fft.rfftfreq(len(ref), 1.0/sr_ref)
        magr = np.abs(np.fft.rfft(ref * np.hanning(len(ref))))
        magc = np.abs(np.fft.rfft(cap * np.hanning(len(cap))))
        idxr = np.argmin(np.abs(freqs - target))
        idxc = idxr
        print(f'H{h}: {freqs[idxr]:.1f} Hz  ref={magr[idxr]:.3e}  cap={magc[idxc]:.3e}')

    # save spectrums
    spectrum_plot(ref, sr_ref, os.path.join(ROOT, 'reference_spectrum.png'), title='Reference 440Hz')
    spectrum_plot(cap, sr_cap, os.path.join(ROOT, 'captured_spectrum.png'), title='Captured')
    spectrum_plot(error, sr_ref, os.path.join(ROOT, 'diff_spectrum.png'), title='Difference (cap - ref)')

    print('\nSaved spectrum images: reference_spectrum.png, captured_spectrum.png, diff_spectrum.png')

if __name__ == '__main__':
    main()
