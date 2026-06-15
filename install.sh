#!/bin/bash
set -euo pipefail

TMP=$(mktemp -d)
URL="https://github.com/I-layder-I/voice-assistant/releases/latest/download/voice-assistant.tar.gz"

echo
echo "Wellcome to Voice-assistant!"
echo

echo "Downloading latest release..."
curl -fL "$URL" -o "$TMP/release.tar.gz"
tar -xzf "$TMP/release.tar.gz" -C "$TMP"

echo "Copying binary..."
sudo install -Dm755 "$TMP/voice-assistant" \
/usr/local/bin/voice-assistant

echo "Copying model..."
install -d ~/.local/share/voice-assistant
cp -r "$TMP/model" \
~/.local/share/voice-assistant/
echo "Copying commands..."
install -d ~/.config/voice-assistant
cp -r "$TMP/commands" \
~/.config/voice-assistant/

rm -rf "$TMP"

echo "Installed."
