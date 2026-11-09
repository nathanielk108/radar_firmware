#!/usr/bin/env bash

gdb-multiarch -x ./scripts/debug.gdbinit ./build/radar_firmware.elf
