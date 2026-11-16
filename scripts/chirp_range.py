#!/usr/bin/env python3
"""Report beat peaks and their apparent ranges at the programmed chirp slope."""

import argparse
import cmath
import math
import sys


METADATA_KEYS = ("sample_rate_hz", "step_hz", "step_rate_hz", "samples")
SPEED_OF_LIGHT_M_S = 299_792_458
ADC_FULL_SCALE = 32768


def read_capture(lines):
  for line in lines:
    line = line.strip()
    if line.startswith("# sample_rate_hz="):
      metadata = dict(item.split("=", 1) for item in line[2:].split(","))
      sample_rate, step_hz, step_rate, count = (
          int(metadata[key]) for key in METADATA_KEYS
      )
      if min(sample_rate, step_hz, step_rate) <= 0 or count < 5:
        raise ValueError("invalid chirp metadata")
      break
  else:
    raise ValueError("missing chirp metadata")

  if next(lines, "").strip() != "index,adc_a,adc_b":
    raise ValueError("missing chirp CSV header")

  adc_a, adc_b = [], []
  for index in range(count):
    line = next(lines, "").strip()
    if not line:
      raise ValueError(f"capture ended after {index} of {count} samples")
    row, a, b = map(int, line.split(","))
    if row != index:
      raise ValueError(f"expected sample index {index}, got {row}")
    if not (-32768 <= a <= 32767 and -32768 <= b <= 32767):
      raise ValueError(f"sample {index} is outside the signed 16-bit ADC range")
    adc_a.append(a)
    adc_b.append(b)
  return sample_rate, step_hz * step_rate, (adc_a, adc_b)


def beat_peaks(samples, sample_rate, window):
  count = len(samples)
  mean = sum(samples) / count
  centered = [value - mean for value in samples]
  window_sum = count
  if window == "hann":
    weights = [0.5 - 0.5 * math.cos(math.tau * i / (count - 1))
               for i in range(count)]
    centered = [value * weight for value, weight in zip(centered, weights)]
    window_sum = sum(weights)

  powers = [0.0]
  for k in range(1, count // 2 + 1):
    tone = sum(value * cmath.exp(-1j * math.tau * k * i / count)
               for i, value in enumerate(centered))
    powers.append(abs(tone) ** 2)

  last_bin = count // 2
  powers.append(0.0)
  peaks = [k for k in range(1, last_bin + 1)
           if powers[k - 1] < powers[k] >= powers[k + 1]]
  peaks.sort(key=powers.__getitem__, reverse=True)

  for peak in peaks:
    offset = 0.0
    power = powers[peak]
    neighbors = powers[peak - 1:peak + 2]
    if 1 < peak < last_bin and min(neighbors) > 0:
      # Interpolate the peak frequency and height in log power.
      left, center, right = map(math.log, neighbors)
      curvature = left - 2 * center + right
      if curvature < 0:
        offset = 0.5 * (left - right) / curvature
        power = math.exp(center - 0.25 * (left - right) * offset)
    frequency = (peak + offset) * sample_rate / count
    scale = 1 if 2 * peak == count else 2
    amplitude = scale * math.sqrt(power) / window_sum
    yield frequency, 20 * math.log10(amplitude / ADC_FULL_SCALE)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("csv", type=argparse.FileType("r", encoding="utf-8-sig"),
                      help="firmware USB CDC capture, or - for stdin")
  parser.add_argument("--channel", choices=("a", "b", "both"), default="both")
  parser.add_argument("--peaks", type=int, default=1,
                      help="maximum number of local peaks per channel, ranked by level")
  parser.add_argument("--window", choices=("hann", "rectangular"), default="hann")
  args = parser.parse_args()
  if args.peaks < 1:
    parser.error("--peaks must be at least 1")
  try:
    sample_rate, slope, channels = read_capture(iter(args.csv))
  except (KeyError, OSError, ValueError) as error:
    parser.exit(2, f"{parser.prog}: {error}\n")

  for channel, samples in zip(("a", "b"), channels):
    if args.channel not in (channel, "both"):
      continue
    name = f"adc_{channel}"
    peaks = list(beat_peaks(samples, sample_rate, args.window))
    if args.peaks > 1:
      peaks.sort(key=lambda peak: peak[1], reverse=True)
    if not peaks:
      print(f"{name}: no non-DC peak")
    for rank, (frequency, level) in enumerate(peaks[:args.peaks], 1):
      distance = SPEED_OF_LIGHT_M_S * frequency / (2 * slope)
      result = f"{frequency:.1f} Hz, {distance:.2f} m"
      if args.peaks == 1:
        print(f"{name}: {result}")
      else:
        print(f"{name} peak {rank}: {result}, {level:.1f} dBFS")
  print("Beat peaks can include harmonics, clutter, or interference.",
        file=sys.stderr)


if __name__ == "__main__":
  main()
