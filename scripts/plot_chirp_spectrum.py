#!/usr/bin/env python3
"""Compare the one-sided FFT spectra of single-chirp ADC captures."""

import argparse
from pathlib import Path

from chirp_range import ADC_FULL_SCALE, read_capture


OUTDOOR_CAPTURES = {
    "single_chirp_outside.csv": "Outside 1 (~10 m)",
    "single_chirp_outside2.csv": "Outside 2 (~15 m)",
    "single_chirp_outside3.csv": "Outside 3 (pointed up)",
}
DISPLAY_FLOOR_DBFS = -120


def spectrum(samples, sample_rate):
  import numpy as np

  samples = np.asarray(samples, dtype=float)
  window = np.hanning(samples.size)
  # Zero-padding gives a smoother trace between the capture's FFT bins.
  fft_size = max(1024, samples.size)
  centered = samples - samples.mean()
  magnitude = np.abs(np.fft.rfft(centered * window, n=fft_size))
  magnitude /= window.sum() * ADC_FULL_SCALE
  interior = magnitude[1:-1] if fft_size % 2 == 0 else magnitude[1:]
  interior *= 2
  floor = 10 ** (DISPLAY_FLOOR_DBFS / 20)
  level = 20 * np.log10(np.maximum(magnitude, floor))
  return np.fft.rfftfreq(fft_size, d=1 / sample_rate) / 1000, level


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("csv", nargs="*", type=Path,
                      help="capture CSVs, defaulting to the three outdoor tests")
  parser.add_argument("--channel", choices=("a", "b"), default="b")
  parser.add_argument("-o", "--output", type=Path,
                      help="save to a file instead of opening a plot window")
  args = parser.parse_args()

  try:
    from matplotlib import pyplot as plt
    if args.output:
      plt.switch_backend("Agg")
  except ImportError:
    parser.error("install NumPy and Matplotlib with "
                 "`python -m pip install numpy matplotlib`")

  data_dir = Path(__file__).resolve().parents[1] / "data"
  captures = args.csv or [data_dir / name for name in OUTDOOR_CAPTURES]
  channel_index = ("a", "b").index(args.channel)

  figure, axes = plt.subplots(figsize=(10, 5), layout="constrained")
  line_styles = ("-", "--", "-.")
  for index, path in enumerate(captures):
    try:
      with path.open(encoding="utf-8-sig") as lines:
        sample_rate, _, channels = read_capture(lines)
    except (KeyError, OSError, ValueError) as error:
      parser.error(f"{path}: {error}")
    frequency, level = spectrum(channels[channel_index], sample_rate)
    label = path.stem if args.csv else OUTDOOR_CAPTURES[path.name]
    axes.plot(frequency, level, line_styles[index % len(line_styles)], label=label)

  axes.margins(x=0)
  axes.set(title=f"Single-chirp spectra · ADC channel {args.channel.upper()}",
           xlabel="Beat frequency (kHz)", ylabel="Amplitude (dBFS)")
  axes.grid(alpha=0.25)
  axes.legend()
  if args.output:
    figure.savefig(args.output, dpi=160)
  else:
    plt.show()


if __name__ == "__main__":
  main()
