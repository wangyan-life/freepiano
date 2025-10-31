# Sound diagnostics tools

This folder contains simple utilities to record system audio and analyze the
resulting WAV file to verify whether the generated tone is a clean sine wave.

Files:

- `record_wasapi.ps1` - PowerShell wrapper that uses `ffmpeg` to capture the
  default WASAPI loopback to a WAV file. Example: `.
ecord_wasapi.ps1 -Out captured.wav -Seconds 5`
- `analyze_sine.py` - Python script which computes the spectrum of a WAV file,
  prints the dominant frequency and harmonic magnitudes and saves a PNG
  spectrum next to the input file. Requires `numpy` and `matplotlib` (SciPy
  optional).

Quick usage:

1. Record a short clip while running the demo:
   ```powershell
   .\record_wasapi.ps1 -Out captured.wav -Seconds 5
   ```

2. Analyze:
   ```powershell
   python analyze_sine.py captured.wav
   ```

If you prefer GUI inspection, open `captured.wav` in Audacity and view the
waveform and frequency analysis (Analyze → Plot Spectrum).
