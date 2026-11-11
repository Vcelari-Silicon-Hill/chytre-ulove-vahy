#!/usr/bin/env bash
# Builds the firmware and copies it to the server's web installer.
# Needs arduino-cli with the ESP32 core 3.2.x and the libraries listed in README.md.
# The parts are flashed separately (not as one merged image) so that an update keeps
# the settings and calibration stored in the NVS partition at 0x9000.
set -euo pipefail
cd "$(dirname "$0")"
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=no_fs,FlashSize=4M"
OUT=build
VERSION=$(sed -n 's/#define FW_VERSION "\(.*\)"/\1/p' ulova_vaha/config.h)
arduino-cli compile --fqbn "$FQBN" --output-dir "$OUT" ulova_vaha
DEST=../server/static/flash/firmware
mkdir -p "$DEST"
cp "$OUT/ulova_vaha.ino.bootloader.bin" "$DEST/bootloader.bin"   # 0x0
cp "$OUT/ulova_vaha.ino.partitions.bin" "$DEST/partitions.bin"   # 0x8000
cp boot_app0.bin "$DEST/boot_app0.bin"                           # 0xe000, from the Arduino-ESP32 core
cp "$OUT/ulova_vaha.ino.bin" "$DEST/ulova_vaha.bin"              # 0x10000
sed -i.bak "s/\"version\": \"[^\"]*\"/\"version\": \"$VERSION\"/" ../server/static/flash/manifest.json
rm -f ../server/static/flash/manifest.json.bak
echo "Firmware $VERSION -> $DEST"
