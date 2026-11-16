#!/usr/bin/env python3
"""Estimate the range of the strongest beat tone in each chirp channel."""

import argparse
import cmath
import math
import sys


METADATA_KEYS = ("sample_rate_hz", "step_hz", "step_rate_hz", "samples")
SPEED_OF_LIGHT_M_S = 299_792_458


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


def peak_frequency(samples, sample_rate, window):
  count = len(samples)
  mean = sum(samples) / count
  centered = [value - mean for value in samples]
  if window == "hann":
    centered = [value * (0.5 - 0.5 * math.cos(math.tau * i / (count - 1)))
                for i, value in enumerate(centered)]

  powers = [0.0]
  for k in range(1, count // 2 + 1):
    tone = sum(value * cmath.exp(-1j * math.tau * k * i / count)
               for i, value in enumerate(centered))
    powers.append(abs(tone) ** 2)

  peak = max(range(1, len(powers)), key=powers.__getitem__)
  if powers[peak] == 0:
    return None

  # A parabola through the three log-power bins estimates the sub-bin peak.
  offset = 0.0
  if 1 < peak < len(powers) - 1 and min(powers[peak - 1:peak + 2]) > 0:
    left, center, right = map(math.log, powers[peak - 1:peak + 2])
    curvature = left - 2 * center + right
    if curvature < 0:
      offset = 0.5 * (left - right) / curvature
  return (peak + offset) * sample_rate / count


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("csv", type=argparse.FileType("r", encoding="utf-8-sig"),
                      help="firmware USB CDC capture, or - for stdin")
  parser.add_argument("--window", choices=("hann", "rectangular"), default="hann")
  args = parser.parse_args()
  try:
    sample_rate, slope, channels = read_capture(iter(args.csv))
  except (KeyError, OSError, ValueError) as error:
    parser.exit(2, f"{parser.prog}: {error}\n")

  for name, samples in zip(("adc_a", "adc_b"), channels):
    frequency = peak_frequency(samples, sample_rate, args.window)
    if frequency is None:
      print(f"{name}: no non-DC peak")
    else:
      distance = SPEED_OF_LIGHT_M_S * frequency / (2 * slope)
      print(f"{name}: {frequency:.1f} Hz, {distance:.2f} m")
  print("Peak estimates can include clutter or interference.", file=sys.stderr)


if __name__ == "__main__":
  main()
