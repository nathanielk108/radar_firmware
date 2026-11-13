#!/usr/bin/env bash

zip -r radar_firmware.zip . -x "build/*" "vendor/*" ".cache/*" ".git/*"
