#!/usr/bin/env bash

exec gdb-multiarch -x ./scripts/debug.gdbinit "${1:-./build/Debug/radar_firmware.elf}"
