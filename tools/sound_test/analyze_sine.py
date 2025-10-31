#!/usr/bin/env python3
"""
analyze_sine.py

Simple script to analyze a WAV file for dominant frequency and harmonic content,
and produce a spectrum PNG. Minimal dependencies: numpy + matplotlib. If
scipy is installed the script will use it to read WAV, otherwise it falls back
to the built-in wave module (supports common PCM formats).

Usage:
  python analyze_sine.py captured.wav

Outputs:
  - prints dominant frequency and simple harmonic magnitudes
  - writes 'spectrum.png' next to the input file
"""
import sys
import os
import numpy as np
import matplotlib.pyplot as plt

def read_wav(path):
    try:
        from scipy.io import wavfile
        sr, data = wavfile.read(path)
        return sr, data
    except Exception:
        import wave
        with wave.open(path, 'rb') as wf:
            sr = wf.getframerate()
            nchan = wf.getnchannels()
            sampwidth = wf.getsampwidth()
            nframes = wf.getnframes()
            raw = wf.readframes(nframes)
            # determine dtype
            if sampwidth == 1:
                dtype = np.uint8  # unsigned 8-bit PCM
            elif sampwidth == 2:
                dtype = np.int16
            elif sampwidth == 4:
                dtype = np.int32
            else:
                raise RuntimeError('Unsupported sample width: %d' % sampwidth)
            data = np.frombuffer(raw, dtype=dtype)
            if nchan > 1:
                data = data.reshape(-1, nchan)
            return sr, data

def to_mono(data):
    if data.ndim == 1:
        return data.astype(np.float64)
    return data.mean(axis=1).astype(np.float64)

def main():
    if len(sys.argv) < 2:
        print('Usage: python analyze_sine.py <file.wav>')
        sys.exit(1)
    path = sys.argv[1]
    if not os.path.exists(path):
        print('File not found:', path); sys.exit(1)
    sr, data = read_wav(path)
    print('Sample rate:', sr, 'Shape:', getattr(data, 'shape', None), 'dtype:', getattr(data, 'dtype', None))
    mono = to_mono(data)
    # normalize to [-1,1] for integer formats
    if np.issubdtype(mono.dtype, np.integer):
        info_max = np.iinfo(mono.dtype).max
        mono = mono.astype(np.float64) / (info_max if info_max else 1.0)
    mono = mono - np.mean(mono)

    # take a 1-3 second window starting at 0.1s if possible
    start = int(0.1 * sr)
    length = min(len(mono) - start, sr * 3)
    if length <= 0:
        seg = mono
    else:
        seg = mono[start:start+length]

    # apply window
    win = np.hanning(len(seg))
    fft = np.fft.rfft(seg * win)
    freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
    mag = np.abs(fft)

    peak_idx = np.argmax(mag)
    peak_freq = freqs[peak_idx]
    print(f'Dominant frequency: {peak_freq:.2f} Hz')

    # print first few harmonics
    for h in range(1, 6):
        target = peak_freq * h
        idx = np.argmin(np.abs(freqs - target))
        print(f'Harmonic {h}: {freqs[idx]:.1f} Hz  magnitude={mag[idx]:.3e}')

    # rough SNR estimate: peak energy vs rest
    peak_energy = mag[peak_idx]**2
    total_energy = (mag**2).sum()
    noise_energy = max(total_energy - peak_energy, 1e-20)
    snr_db = 10.0 * np.log10((peak_energy + 1e-20) / noise_energy)
    print(f'Rough peak-vs-rest (dB): {snr_db:.2f} dB')

    # save spectrum plot
    out_png = os.path.splitext(path)[0] + '_spectrum.png'
    plt.figure(figsize=(10,4))
    plt.plot(freqs, 20*np.log10(mag + 1e-20))
    plt.xlim(0, min(sr/2, peak_freq*6))
    plt.xlabel('Hz')
    plt.ylabel('Magnitude (dB)')
    plt.title(f'Spectrum of {os.path.basename(path)}')
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(out_png)
    print('Saved spectrum plot to', out_png)

if __name__ == '__main__':
    main()
