#!/bin/bash
set -euo pipefail

echo "Removing binary..."
sudo rm -f /usr/local/bin/voice-assistant
echo "Removing libs..."
sudo rm -f /usr/local/lib/libvosk.so
echo "Removing headers..."
sudo rm -f /usr/local/include/vosk_api.h

echo "Updating lib cache..."
sudo ldconfig

echo "Done"
