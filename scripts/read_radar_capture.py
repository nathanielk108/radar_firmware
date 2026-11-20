#!/usr/bin/env python3
"""Convert an RDR1 radar capture from the SD card to CSV.

Export every chirp:
  python scripts/read_radar_capture.py C0000000.BIN --output capture.csv

Export just the first chirp for chirp_range.py or plot_chirp_spectrum.py:
  python scripts/read_radar_capture.py C0000000.BIN --chirp 0 --output chirp.csv

Verify and export every capture indexed by a characterization run:
  python scripts/read_radar_capture.py --manifest R0000000.CSV --output captures
"""

import argparse
import csv
from os import SEEK_END
from pathlib import Path
import re
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


class RunManifest(NamedTuple):
  entries: dict
  gain_mv: int
  chirps: int
  interval_us: int
  requested: int
  status: str


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


def comment_fields(line, keys):
  if not line.startswith("# "):
    raise ValueError("missing manifest comment")
  entries = line[2:].split(",")
  fields = dict(entry.split("=", 1) for entry in entries)
  if len(entries) != len(keys) or fields.keys() != keys:
    raise ValueError("unexpected manifest comment fields")
  return fields


def integer(value, key, lower, upper):
  if not value.isascii() or not value.isdecimal():
    raise ValueError(f"invalid {key}: {value!r}")
  number = int(value)
  if not lower <= number <= upper:
    raise ValueError(f"invalid {key}: {value!r}")
  return number


def read_manifest(raw):
  # Ignore a final line cut off before its newline.
  lines = raw[:raw.rfind(b"\n") + 1].decode("utf-8-sig").splitlines()
  if len(lines) < 2 or lines[1] != "index,file,sequence,first_sample_us":
    raise ValueError("missing or invalid characterization manifest header")
  fields = comment_fields(lines[0], {"radar_run", "tag", "gain_mv", "captures",
                                     "chirps", "interval_us", "gap_ms"})
  if fields["radar_run"] != "1":
    raise ValueError("unsupported characterization manifest version")
  if not re.fullmatch(r"[A-Za-z0-9_-]{1,16}", fields["tag"]):
    raise ValueError("invalid characterization tag")
  gain = integer(fields["gain_mv"], "gain_mv", 1, 65535)
  requested = integer(fields["captures"], "captures", 1, 1000)
  chirps = integer(fields["chirps"], "chirps", 1, 64)
  interval_min, interval_max = (0, 0) if chirps == 1 else (500, 10000)
  interval_us = integer(fields["interval_us"], "interval_us", interval_min, interval_max)
  integer(fields["gap_ms"], "gap_ms", 0, 60000)
  status = "interrupted"
  if len(lines) > 2 and lines[-1].startswith("#"):
    footer = comment_fields(lines.pop(), {"status", "saved"})
    status = footer["status"]
    if status not in {"complete", "stopped", "capture_error", "storage_error", "usb_error"}:
      raise ValueError(f"unexpected characterization status {status!r}")
    saved = integer(footer["saved"], "saved", 0, requested)
    if saved != len(lines) - 2 or (status == "complete" and saved != requested):
      raise ValueError("manifest footer disagrees with its capture rows")

  entries = {}
  for index, row in enumerate(csv.reader(lines[2:], strict=True), 1):
    if len(row) != 4 or index > requested:
      raise ValueError(f"invalid capture row {index}")
    row_index, name, sequence, timestamp = row
    if row_index != str(index):
      raise ValueError(f"invalid index at capture row {index}")
    if not re.fullmatch(r"C[0-9]{7}\.BIN", name) or name in entries:
      raise ValueError(f"invalid or duplicate filename at capture row {index}: {name!r}")
    integer(timestamp, "first_sample_us", 0, 0xFFFFFFFF)
    entries[name] = integer(sequence, "sequence", 0, 0xFFFFFFFF)
  return RunManifest(entries, gain, chirps, interval_us, requested, status)


def export_manifest(manifest, output, chirp):
  raw = manifest.read_bytes()
  run = read_manifest(raw)
  if chirp is not None and not 0 <= chirp < run.chirps:
    raise ValueError(f"--chirp must be in the range 0 to {run.chirps - 1}")
  if run.status != "complete":
    print(f"{manifest.name}: {run.status}, "
          f"{len(run.entries)} of {run.requested} captures indexed", file=sys.stderr)
  captures = []
  for name, sequence in run.entries.items():
    path = manifest.with_name(name)
    try:
      with path.open("rb") as source:
        header = read_header(source)
      if (header.sequence != sequence or header.gain_mv != run.gain_mv or
          header.chirp_count != run.chirps or
          header.repetition_ticks * 1000000 != header.timer_hz * run.interval_us):
        raise ValueError("header disagrees with characterization manifest")
    except (OSError, ValueError) as error:
      raise ValueError(f"{path}: {error}") from error
    captures.append((path, header))

  # Preflight all indexed files before creating a new export directory.
  output.mkdir()
  (output / "manifest.csv").write_bytes(raw)
  for path, header in captures:
    with path.open("rb") as source, (output / path.with_suffix(".csv").name).open(
        "x", encoding="utf-8", newline="") as destination:
      write_csv(source, destination, header, chirp)
  print(f"{len(captures)} of {run.requested} captures exported ({run.status})")


def main():
  parser = argparse.ArgumentParser(
      description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  inputs = parser.add_mutually_exclusive_group(required=True)
  inputs.add_argument("capture", nargs="?", type=Path, help="RDR1 capture file")
  inputs.add_argument("--manifest", type=Path, help="characterization run manifest")
  parser.add_argument("--chirp", type=int, help="export one zero-based chirp")
  parser.add_argument("-o", "--output", type=Path,
                      help="new CSV file, or new directory when using --manifest")
  args = parser.parse_args()

  try:
    if args.manifest:
      if not args.output:
        parser.error("--manifest requires --output with a new directory")
      export_manifest(args.manifest, args.output, args.chirp)
      return
    with args.capture.open("rb") as source:
      header = read_header(source)
      if args.chirp is not None and not 0 <= args.chirp < header.chirp_count:
        parser.error(f"--chirp must be in the range 0 to {header.chirp_count - 1}")
      if args.output:
        with args.output.open("x", encoding="utf-8", newline="") as destination:
          write_csv(source, destination, header, args.chirp)
      else:
        write_csv(source, sys.stdout, header, args.chirp)
  except (OSError, ValueError, csv.Error, struct.error) as error:
    parser.exit(2, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
  main()
