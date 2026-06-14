#!/bin/bash
set -euo pipefail

TMP=$(mktemp -d)

git clone https://github.com/I-layder-I/voice-assistant "$TMP"
echo

echo "Wellcome to Voice-assistant!"
echo

echo "Copying binary..."
sudo install -Dm755 "$TMP/voice-assistant" \
/usr/local/bin/voice-assistant
echo "Copying libs..."
sudo install -Dm644 "$TMP/libvosk.so" \
/usr/local/lib/libvosk.so
echo "Copying headers..."
sudo install -Dm644 "$TMP/vosk_api.h" \
/usr/local/include/vosk_api.h

echo "Copying model..."
mkdir -p ~/.local/share/voice-assistant
cp -r "$TMP/model" \
~/.local/share/voice-assistant/
echo "Copying commands..."
mkdir -p ~/.config/voice-assistant
cp -r "$TMP/commands" \
~/.config/voice-assistant/

echo "Updating lib cache..."
sudo ldconfig

rm -rf "$TMP"

echo "Installed."
