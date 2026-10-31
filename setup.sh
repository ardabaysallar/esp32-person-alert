#!/usr/bin/env bash
# One-command setup for esp32-person-alert (macOS / Linux).
#   ./setup.sh            install tools, check secrets, compile and upload
#   ./setup.sh compile    install tools and compile only (no board needed)
set -euo pipefail

cd "$(dirname "$0")"
SKETCH=PersonAlert
CORE="esp32:esp32@2.0.5"
URL="https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,FlashMode=qio,UploadMode=default,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default"
MODE="${1:-upload}"

# 1. arduino-cli: use the one on PATH, the Arduino IDE 2 copy, or install via Homebrew
if command -v arduino-cli >/dev/null 2>&1; then
  CLI=arduino-cli
elif [ -x "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" ]; then
  CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
elif command -v brew >/dev/null 2>&1; then
  echo "Installing arduino-cli with Homebrew..."
  brew install arduino-cli
  CLI=arduino-cli
else
  echo "arduino-cli not found. Install it from https://arduino.github.io/arduino-cli/ and re-run." >&2
  exit 1
fi

# 2. ESP32 core (2.0.5 is the version Freenove's tutorial uses) and libraries, pinned
echo "Installing ESP32 core and libraries..."
"$CLI" core update-index --additional-urls "$URL" >/dev/null
"$CLI" core install "$CORE" --additional-urls "$URL"
"$CLI" lib install "WiFiManager@2.0.17" "ESP Mail Client@3.4.24" "Chirale_TensorFLowLite@2.0.0"

# 3. Secrets: create secrets.h from the template, refuse to continue until it is filled in
if [ ! -f "$SKETCH/secrets.h" ]; then
  cp "$SKETCH/secrets.h.example" "$SKETCH/secrets.h"
  echo
  echo "Created $SKETCH/secrets.h. Open it, fill in the three values, save, then run ./setup.sh again."
  [ "$MODE" = "compile" ] || exit 0
fi
if grep -q "PASTE-16-CHAR\|PUT-YOUR-MAIN\|your.camera.account" "$SKETCH/secrets.h"; then
  if [ "$MODE" = "compile" ]; then
    echo "Note: secrets.h still has placeholders (fine for compile only)."
  else
    echo "$SKETCH/secrets.h still has placeholder values. Fill it in and re-run." >&2
    exit 1
  fi
fi

# 4. Compile
echo "Compiling..."
"$CLI" compile -b "$FQBN" "$SKETCH"
[ "$MODE" = "compile" ] && { echo "Compile OK."; exit 0; }

# 5. Find the board and upload
PORT=$(ls /dev/cu.wchusbserial* /dev/cu.usbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n1 || true)
if [ -z "$PORT" ]; then
  echo "No board found. Plug in the USB-C port next to the CH343 chip, install the WCH driver (see README), then re-run." >&2
  exit 1
fi
echo "Uploading to $PORT ..."
"$CLI" upload -p "$PORT" -b "$FQBN" "$SKETCH"
echo
echo "Done. Watch the output with:  $CLI monitor -p $PORT -c baudrate=115200"
