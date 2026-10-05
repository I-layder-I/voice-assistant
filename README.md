# Voice Assistant

Лёгкий офлайн голосовой ассистент для Linux, написанный на C++ с использованием [Vosk](https://alphacephei.com/vosk/).

Распознавание речи выполняется локально, без облачных сервисов. Пользовательские команды задаются обычными shell-скриптами, поэтому ассистента можно расширять без изменения исходного кода.

## Возможности

* Офлайн распознавание речи
* Поддержка нескольких языков и моделей Vosk
* Пользовательские команды через shell-скрипты
* Не требует облачных сервисов

## Требования

Для запуска:

* Linux
* ALSA
* Vosk
* ICU
* хотя бы одна модель Vosk

Для сборки дополнительно:

* C++23 compiler
* CLI11

## Установка

### Готовая версия

Установочный скрипт:

```bash
curl -fsSL https://raw.githubusercontent.com/I-layder-I/voice-assistant/main/install.sh | bash
```

После установки:

```bash
voice-assistant --help
```

### Сборка из исходников

```bash
git clone https://github.com/I-layder-I/voice-assistant.git
cd voice-assistant
```

На Arch Linux зависимости можно установить командой:

```bash
sudo pacman -S base-devel alsa-lib icu vosk cli11
```

Затем собрать:

```bash
g++ voice-assistant.cpp \
    -o build/voice-assistant \
    -std=c++23 \
    -lasound \
    -pthread \
    -lvosk \
    -licuuc
```

## Быстрый старт

Команды находятся в:

```text
~/.config/voice-assistant/commands/
```

Модели можно скачать с [официальной страницы Vosk](https://alphacephei.com/vosk/models).

1. Скачайте подходящую модель с официального сайта.
2. Распакуйте архив.
3. Переименуйте каталог модели в код языка, например:

```text
ru
```

или:

```text
en
```

4. Поместите модель в:

```text
~/.local/share/voice-assistant/models/
```

Например:

```text
~/.local/share/voice-assistant/models/
└── ru/
```

После этого:

```bash
voice-assistant --language ru
```

## Пользовательские команды

Команды являются обычными shell-скриптами.

Пример:

```bash
#!/bin/bash

# WORDS: открой браузер, запусти браузер

firefox
```

Файл:

```text
~/.config/voice-assistant/commands/browser.sh
```

Ассистент найдёт команду по указанным ключевым словам и запустит соответствующий скрипт.

Поддерживаются два режима сопоставления:

```bash
voice-assistant --matching exact
```

```bash
voice-assistant --matching substring
```

Подробное описание формата команд, `WORDS`, matching и других возможностей находится в документации.

## CLI

Основные параметры:

| Параметр            | Описание                     |
| ------------------- | ---------------------------- |
| `-m, --model`       | Путь к модели Vosk           |
| `-c, --commands`    | Путь к каталогу команд       |
| `-r, --sample-rate` | Частота дискретизации        |
| `--language`        | Язык распознавания           |
| `--stdin`           | Обработка текста через stdin |
| `--matching`        | Режим сопоставления команд   |
| `--vosk-debug`      | Отладочный вывод Vosk        |

Полный список:

```bash
voice-assistant --help
```

## Удаление

Если программа была установлена через установочный скрипт:

```bash
curl -fsSL https://raw.githubusercontent.com/I-layder-I/voice-assistant/main/uninstall.sh | bash
```

Пользовательские модели и команды удаляются отдельно.

## Лицензия

Проект распространяется под лицензией **MIT**.

Полный текст лицензии находится в файле [`LICENSE`](LICENSE).
