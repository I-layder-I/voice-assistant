# Voice Assistant

A lightweight offline voice assistant for Linux, written in C++ using [Vosk](https://alphacephei.com/vosk/).

Speech recognition is performed locally, without cloud services. User commands are defined using regular shell scripts, so the assistant can be extended without modifying the source code.

English version | [Русская версия](README.ru.md)

## Features

- Offline speech recognition
- Support for multiple languages and Vosk models
- User commands through shell scripts
- No cloud services required

## Requirements

For running:

- Linux
- ALSA
- Vosk
- ICU
- At least one Vosk model

For building additionally:

- C++23 compiler
- CLI11

## Installation

### Prebuilt version

Installation script:

```bash
curl -fsSL https://raw.githubusercontent.com/I-layder-I/voice-assistant/main/install.sh | bash
```

After installation:

```bash
voice-assistant --help
```

### Build from source

```bash
git clone https://github.com/I-layder-I/voice-assistant.git
cd voice-assistant
```

On Arch Linux, dependencies can be installed with:

```bash
sudo pacman -S base-devel alsa-lib icu vosk cli11
```

Then build:

```bash
g++ voice-assistant.cpp \
    -o build/voice-assistant \
    -std=c++23 \
    -lasound \
    -pthread \
    -lvosk \
    -licuuc
```

## Quick Start

Commands are located in:

```text
~/.config/voice-assistant/commands/
```

Models can be downloaded from the [official Vosk page](https://alphacephei.com/vosk/models).

1. Download a suitable model from the official website.
2. Extract the archive.
3. Rename the model directory to the language code, for example:

```text
en
```

or:

```text
ru
```

4. Place the model in:

```text
~/.local/share/voice-assistant/models/
```

For example:

```text
~/.local/share/voice-assistant/models/
└── en/
```

Then:

```bash
voice-assistant --language en
```

## User Commands

Commands are regular shell scripts.

Example:

```bash
#!/bin/bash

# WORDS: open browser, run browser

firefox
```

File:

```text
~/.config/voice-assistant/commands/browser.sh
```

The assistant will find the command using the specified keywords and execute the corresponding script.

Two matching modes are supported:

```bash
voice-assistant --matching exact
```

```bash
voice-assistant --matching substring
```

A detailed description of the command format, `WORDS`, matching, and other features can be found in the documentation.

## CLI

Main options:

| Option              | Description                    |
| ------------------- | ------------------------------ |
| `-m, --model`       | Path to the Vosk model         |
| `-c, --commands`    | Path to the commands directory |
| `-r, --sample-rate` | Sample rate                    |
| `--language`        | Recognition language           |
| `--stdin`           | Process text through stdin     |
| `--matching`        | Command matching mode          |
| `--vosk-debug`      | Vosk debug output              |

Full list:

```bash
voice-assistant --help
```

## Uninstallation

If the program was installed using the installation script:

```bash
curl -fsSL https://raw.githubusercontent.com/I-layder-I/voice-assistant/main/uninstall.sh | bash
```

User models and commands are removed separately.

## License

The project is distributed under the **MIT** license.

The full license text is available in the [`LICENSE`](LICENSE) file.
