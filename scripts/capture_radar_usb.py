#!/usr/bin/env python3
"""Save a USB radar run as RDR1 files and a standard run manifest. Requires pySerial."""

import argparse
from contextlib import ExitStack
from io import BytesIO
import math
from pathlib import Path
import re
import sys
import time

from read_radar_capture import HEADER, read_header, read_manifest


MAX_SAMPLES = 128
FRAME_PREFIXES = ("# capture=", "# data=", "# status=")
CAPTURE = re.compile(r"# capture=(\d+),first_sample_us=(\d+),bytes=(\d+)")
DATA = re.compile(r"# data=([a-fA-F0-9]{2,128})")
FOOTER = re.compile(r"# status=(complete|stopped|capture_error|usb_error),saved=(\d+)")
COLUMNS = "index,file,sequence,first_sample_us\n"


def receive_lines(port, timeout):
  """Read whole lines without asking the serial driver for each byte."""
  pending = bytearray()
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    pending.extend(port.read(min(port.in_waiting or 1, 4096)))
    while b"\n" in pending:
      line, _, pending = pending.partition(b"\n")
      yield line.rstrip(b"\r").decode("ascii", errors="replace")
      if line.startswith((b"# radar_usb=", b"# capture=", b"# data=")):
        deadline = time.monotonic() + timeout
    if len(pending) > 4096:
      raise ValueError("USB stream contains an overlong line")
  raise ValueError("timed out waiting for USB radar data")


def parse_run_header(line, args):
  metadata = line.replace("# radar_usb=", "# radar_run=", 1)
  run = read_manifest(f"{metadata}\n{COLUMNS}".encode("ascii"))
  if (run.requested, run.chirps, run.interval_us) != (
      args.captures, args.chirps, args.interval_us):
    raise ValueError("USB run metadata disagrees with the requested capture settings")
  fields = dict(field.split("=", 1) for field in metadata[2:].split(","))
  if fields["tag"] != args.tag or int(fields["gap_ms"]) != args.gap_ms:
    raise ValueError("USB run metadata disagrees with the requested tag or gap")
  return metadata, run


def save_capture(payload, run, directory, index):
  header = read_header(BytesIO(payload))
  if (header.samples > MAX_SAMPLES or header.gain_mv != run.gain_mv or
      header.chirp_count != run.chirps or
      header.repetition_ticks * 1000000 != header.timer_hz * run.interval_us):
    raise ValueError("RDR1 header disagrees with the USB run metadata")
  name = f"C{index - 1:07d}.BIN"
  path = directory / name
  temporary = path.with_suffix(".part")
  with temporary.open("xb") as stream:
    stream.write(payload)
  temporary.rename(path)
  return name, header.sequence


def collect(lines, args):
  completed = 0
  frame = None
  manifest = None
  with ExitStack() as files:
    for line in lines:
      if line.startswith("# radar_usb="):
        if manifest is not None:
          raise ValueError("unexpected new run before the previous run finished")
        metadata, run = parse_run_header(line, args)
        args.output.mkdir()
        manifest = files.enter_context((args.output / "R0000000.CSV").open(
            "x", encoding="ascii", newline="\n"))
        manifest.write(f"{metadata}\n{COLUMNS}")
        manifest.flush()
      elif manifest is None and line.startswith(FRAME_PREFIXES):
        # The board may still have queued lines from a disconnected reader.
        continue
      elif line.startswith("# capture="):
        match = CAPTURE.fullmatch(line)
        if manifest is None or frame is not None or match is None:
          raise ValueError(f"unexpected capture header {line!r}")
        index, timestamp, size = map(int, match.groups())
        if (index != completed + 1 or index > run.requested or timestamp > 0xFFFFFFFF or
            not (HEADER.size < size <= HEADER.size + 4 * MAX_SAMPLES * run.chirps)):
          raise ValueError(f"invalid capture header {line!r}")
        frame = bytearray()
      elif line.startswith("# data="):
        match = DATA.fullmatch(line)
        if frame is None or match is None:
          raise ValueError("unexpected USB capture data")
        frame.extend(bytes.fromhex(match[1]))
        if len(frame) > size:
          raise ValueError(f"capture {index} exceeds its declared size")
        if len(frame) == size:
          name, sequence = save_capture(frame, run, args.output, index)
          manifest.write(f"{index},{name},{sequence},{timestamp}\n")
          manifest.flush()
          completed += 1
          frame = None
      elif line.startswith("# status="):
        match = FOOTER.fullmatch(line)
        if manifest is None or match is None:
          raise ValueError(f"unexpected USB run footer {line!r}")
        status, saved = match.groups()
        if int(saved) != completed or (status == "complete" and
                                       (frame is not None or completed != run.requested)):
          raise ValueError(f"USB run footer disagrees with {completed} verified captures")
        manifest.write(f"{line}\n")
        return status, completed
      elif line:
        print(line, file=sys.stderr)
        if manifest is None and line.startswith(("RADAR busy", "RADAR unavailable",
                                                 "USB console unavailable", "Unknown command",
                                                 "run-usb <tag>")):
          raise ValueError("firmware rejected the USB run command")
  raise ValueError("USB stream ended before the run footer")


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("port", help="USB CDC port, such as COM13")
  parser.add_argument("tag", help="run label, up to 16 letters, digits, hyphens or underscores")
  parser.add_argument("-n", "--captures", type=int, default=1)
  parser.add_argument("--chirps", type=int, default=1, help="chirps per capture, 1 through 64")
  parser.add_argument("--interval-us", type=int, help="chirp repetition interval for a train")
  parser.add_argument("--gap-ms", type=int, default=0, help="delay between captures")
  parser.add_argument("--timeout", type=float, default=5, help="USB run inactivity timeout in seconds")
  parser.add_argument("-o", "--output", type=Path, required=True, help="new output directory")
  args = parser.parse_args()
  if not re.fullmatch(r"[A-Za-z0-9_-]{1,16}", args.tag):
    parser.error("tag must contain 1 to 16 letters, digits, hyphens or underscores")
  if not (1 <= args.captures <= 1000 and 1 <= args.chirps <= 64 and 0 <= args.gap_ms <= 60000):
    parser.error("use 1 to 1000 captures, 1 to 64 chirps and a gap of 0 to 60000 ms")
  if args.interval_us is None:
    args.interval_us = 0 if args.chirps == 1 else 1000
  interval_min, interval_max = (0, 0) if args.chirps == 1 else (500, 10000)
  if not interval_min <= args.interval_us <= interval_max:
    parser.error(f"interval must be {interval_min} to {interval_max} µs with {args.chirps} chirps")
  if not math.isfinite(args.timeout) or args.timeout <= 0:
    parser.error("timeout must be positive and finite")
  if args.output.exists():
    parser.error(f"output path already exists: {args.output}")

  try:
    import serial
  except ImportError:
    parser.exit(2, "pySerial is required. Install it with python -m pip install pyserial\n")

  command = (f"run-usb {args.tag} {args.captures} {args.chirps} "
             f"{args.interval_us} {args.gap_ms}\r")
  try:
    with serial.Serial(args.port, 115200, timeout=0.2, write_timeout=1) as port:
      port.reset_input_buffer()
      port.write(command.encode("ascii"))
      try:
        timeout = max(args.timeout, args.gap_ms / 1000 + 2)
        status, count = collect(receive_lines(port, timeout), args)
      except KeyboardInterrupt:
        port.write(b"run stop\r")
        raise
  except KeyboardInterrupt:
    parser.exit(130, "Run interrupted. Verified captures were kept.\n")
  except (OSError, ValueError) as error:
    parser.exit(2, f"{parser.prog}: {error}\n")
  print(f"Saved {count} of {args.captures} captures to {args.output} ({status})")
  return 0 if status == "complete" else 1


if __name__ == "__main__":
  sys.exit(main())
