#!/usr/bin/env python3
"""Convert an RDR1 radar capture from the SD card to CSV.

Export every chirp:
  python scripts/read_radar_capture.py C0000000.BIN --output capture.csv

Export just the first chirp for chirp_range.py or plot_chirp_spectrum.py:
  python scripts/read_radar_capture.py C0000000.BIN --chirp 0 --output chirp.csv
"""

import argparse
import csv
from os import SEEK_END
from pathlib import Path
import struct
import sys
from typing import NamedTuple


HEADER = struct.Struct("<4sHHIIiIIIIQIII")
METADATA_KEYS = (
    "sample_rate_hz", "step_hz", "step_rate_hz", "samples", "gain_mv",
    "sequence", "timer_hz", "first_sample_offset_ticks", "bandwidth_hz",
    "start_hz", "repetition_ticks", "chirp_count",
)


class CaptureHeader(NamedTuple):
  magic: bytes
  header_bytes: int
  samples: int
  sequence: int
  timer_hz: int
  first_sample_offset_ticks: int
  sample_rate_hz: int
  step_hz: int
  step_rate_hz: int
  bandwidth_hz: int
  start_hz: int
  gain_mv: int
  repetition_ticks: int
  chirp_count: int


def read_header(source):
  raw = source.read(HEADER.size)
  if len(raw) != HEADER.size:
    raise ValueError("truncated RDR1 header")

  header = CaptureHeader(*HEADER.unpack(raw))
  if header.magic != b"RDR1" or header.header_bytes != HEADER.size:
    raise ValueError("invalid RDR1 header")
  if min(header.samples, header.chirp_count, header.timer_hz,
         header.sample_rate_hz, header.step_hz, header.step_rate_hz) == 0:
    raise ValueError("invalid sample count, chirp count, or timing")
  if header.chirp_count > 1 and header.repetition_ticks == 0:
    raise ValueError("missing repetition interval for chirp train")

  expected = HEADER.size + header.chirp_count * header.samples * 4
  actual = source.seek(0, SEEK_END)
  if actual != expected:
    raise ValueError(f"capture size is {actual} bytes, expected {expected}")
  return header


def write_csv(source, destination, header, chirp):
  multi = chirp is None and header.chirp_count > 1
  first = 0 if chirp is None else chirp
  stop = header.chirp_count if multi else first + 1
  # Existing single-chirp tools identify the header by its first key.
  fields = [f"{key}={getattr(header, key)}" for key in METADATA_KEYS]
  if not multi:
    fields.append(f"chirp={first}")
  print("# " + ",".join(fields), file=destination)

  writer = csv.writer(destination, lineterminator="\n")
  columns = ("index", "adc_a", "adc_b")
  writer.writerow(("chirp", *columns) if multi else columns)
  sample_words = struct.Struct(f"<{header.samples * 2}h")
  source.seek(HEADER.size + first * sample_words.size)
  for chirp_index in range(first, stop):
    words = sample_words.unpack(source.read(sample_words.size))
    prefix = (chirp_index,) if multi else ()
    writer.writerows((*prefix, index, words[index], words[header.samples + index])
                     for index in range(header.samples))


def main():
  parser = argparse.ArgumentParser(
      description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("capture", type=Path, help="RDR1 capture file")
  parser.add_argument("--chirp", type=int, help="export one zero-based chirp")
  parser.add_argument("-o", "--output", type=Path,
                      help="write a new CSV file instead of standard output")
  args = parser.parse_args()

  try:
    with args.capture.open("rb") as source:
      header = read_header(source)
      if args.chirp is not None and not 0 <= args.chirp < header.chirp_count:
        parser.error(f"--chirp must be in the range 0 to {header.chirp_count - 1}")
      if args.output:
        with args.output.open("x", encoding="utf-8", newline="") as destination:
          write_csv(source, destination, header, args.chirp)
      else:
        write_csv(source, sys.stdout, header, args.chirp)
  except (OSError, ValueError, struct.error) as error:
    parser.exit(2, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
  main()
