# bash <(curl -fsSL https://raw.githubusercontent.com/I-layder-I/voice-assistant/main/install.sh)

#!/bin/bash
set -euo pipefail

TMP=$(mktemp -d)

git clone https://github.com/I-layder-I/voice-assistant "$TMP"

echo "Wellcome to Voice-assistant!"

echo "1) Install assistant (default)\n
      2) Uninstall\n"
read -p "Chose action: " -n 1 -r reply
case "$reply" in
    [1]|"")
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
      sudo mkdir -p ~/.local/share/voice-assistant
      sudo cp -r "$TMP/model" \
          ~/.local/share/voice-assistant/
      echo "Copying commands..."
      sudo mkdir -p ~/.config/voice-assistant
      sudo cp -r "$TMP/commands" \
          ~/.config/voice-assistant/

      sudo ldconfig
      rm -rf "$TMP"

      echo "Installed."
      ;;
    [2])
      sudo rm -f /usr/local/bin/voice-assistant
      sudo rm -f /usr/local/lib/libvosk.so
      sudo rm -f /usr/local/include/vosk_api.h
      sudo ldconfig
      echo "Done"
      ;;
esac
