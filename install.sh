#!/bin/bash
set -euo pipefail

TMP=$(mktemp -d)

MODELS_PAGE="https://alphacephei.com/vosk/models"
MODEL_DIR="$HOME/.local/share/voice-assistant/models"
COMMANDS_DIR="$HOME/.local/share/voice-assistant/commands"

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT

for cmd in curl unzip fzf awk tar sudo; do
    if ! command -v "$cmd" &> /dev/null; then
        echo "Error: Required command '$cmd' is not installed." >&2
        exit 1
    fi
done

get_model_language() {
    local model_name="$1"
    model_name="${model_name#vosk-model-small-}"
    model_name="${model_name#vosk-model-}"
    printf '%s\n' "${model_name%%-*}"
}

find_vosk_model() {
    local models selected
    models=$(
        curl -fsSL "$MODELS_PAGE" | awk '
            BEGIN { RS = "</tr>" }
            {
                line = $0
                if (!match(line, /<td/)) next
                n = split(line, cells, /<td[^>]*>/)
                if (n < 2) next
                for (i = 2; i <= n; i++) {
                    gsub(/<[^>]+>/, "", cells[i])
                    gsub(/^[ \t\r\n]+|[ \t\r\n]+$/, "", cells[i])
                    gsub(/\t/, " ", cells[i])
                    gsub(/&amp;/, "\\&", cells[i])
                    gsub(/&nbsp;/, " ", cells[i])
                }
                first = cells[2]
                if (first == "") next
                if (first !~ /^vosk-model/) {
                    current_lang = first
                    next
                }
                if (!match(line, /href="[^"]+\.zip"/)) next
                url = substr(line, RSTART+6, RLENGTH-7)
                if (n < 6) next
                name  = cells[2]
                size  = cells[3]
                notes = cells[5]
                printf "%s\t%s\t%s\t%s\t%s\n", url, name, size, notes, current_lang
            }
        '
    )

    if [[ -z "$models" ]]; then
        echo "No Vosk models found." >&2
        return 1
    fi

    selected=$(
        while IFS=$'\t' read -r url name size notes lang; do
            if [[ "$name" == *"-small-"* ]]; then
                prio=1; tag="[S]"
            else
                prio=2; tag="[B]"
            fi
            short="${notes:0:200}"
            printf '%d\t%s\t%-32s %s %-44s %6s  %s\n' \
                "$prio" "$url" "$lang" "$tag" "$name" "$size" "$short"
        done <<< "$models" |
        sort -t$'\t' -k3,3 -k1,1n -k4,4 |
        cut -f2- |
        fzf \
            --layout=reverse --border --height=50% \
            --delimiter=$'\t' --with-nth=2.. --no-hscroll \
            --tiebreak=begin,length,index \
            --prompt="Model > " \
            --header=$'[S] small — lightweight, low RAM (recommended)\n[B] big   — accurate, high RAM (up to 16 GB)\nType to filter, ↑↓ to select, Enter to confirm'
    )

    [[ -z "$selected" ]] && return 1
    printf '%s\n' "${selected%%$'\t'*}"
}

download_latest_release() {
    echo "Fetching latest version..."
    LATEST_VERSION=$(curl -sL https://api.github.com/repos/I-layder-I/voice-assistant/releases/latest | grep '"tag_name":' | cut -d '"' -f 4)

    if [ "$LATEST_VERSION" = "" ]; then
        echo "Error: Could not determine the latest version."
        exit 1
    fi

    echo "Latest version is $LATEST_VERSION; downloading..."

    URL="https://github.com/I-layder-I/voice-assistant/releases/download/${LATEST_VERSION}/voice-assistant-${LATEST_VERSION}.tar.gz"

    curl -fL "$URL" -o "$TMP/assistant-release.tar.gz"
    tar -xzf "$TMP/assistant-release.tar.gz" -C "$TMP"
}

echo
echo "Welcome to Voice-assistant!"
echo

download_latest_release

echo "Copying binary..."
sudo install -Dm755 "$TMP/voice-assistant" /usr/local/bin/voice-assistant

echo
echo "Select Vosk model..."
model_url=$(find_vosk_model) || exit 1
model_name=$(basename "$model_url" .zip)
language=$(get_model_language "$model_name")

echo
echo "Selected model: $model_name"
echo "Language: $language"
echo
echo "Downloading model..."

model_archive="$TMP/$(basename "$model_url")"
curl -fL --progress-bar "$model_url" -o "$model_archive"

echo
echo "Installing model..."

model_extract="$TMP/model"
mkdir -p "$model_extract"
unzip -q "$model_archive" -d "$model_extract"

model_source=$(find "$model_extract" -mindepth 1 -maxdepth 1 -type d -print -quit)

if [[ -z "$model_source" ]]; then
    echo "Failed to find extracted Vosk model directory." >&2
    exit 1
fi

install -d "$MODEL_DIR/$language"
cp -a "$model_source"/. "$MODEL_DIR/$language/"

echo
echo "Model installed to: $MODEL_DIR/$language"

echo
echo "Copying commands..."
install -d "$COMMANDS_DIR"
cp -r "$TMP/commands/." "$COMMANDS_DIR/"

echo
echo "Installed."
