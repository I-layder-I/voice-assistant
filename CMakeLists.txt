cmake_minimum_required(VERSION 3.16)

project(VoiceAssistantGUI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включить автоматическую обработку Qt макросов
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Найти Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)

# === Явно указать пути к локальным ресурсам ===
# Путь к локальной libvosk.so (в каталоге сборки или исходников)
set(LOCAL_VOSK_LIB "${CMAKE_CURRENT_SOURCE_DIR}/libvosk.so")
# Пути к директориям ресурсов
set(LOCAL_MODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/model")
set(LOCAL_COMMANDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/commands")
# === ===

# Проверяем, существует ли локальная библиотека
if(EXISTS ${LOCAL_VOSK_LIB})
    message(STATUS "Найдена локальная библиотека Vosk: ${LOCAL_VOSK_LIB}")

    # Создаем импортированную цель для локальной библиотеки
    add_library(vosk_local SHARED IMPORTED)
    set_target_properties(vosk_local PROPERTIES
        IMPORTED_LOCATION "${LOCAL_VOSK_LIB}"
    )

    # Используем локальную библиотеку
    set(VOSK_TARGET vosk_local)
else()
    message(WARNING "Локальная библиотека Vosk (${LOCAL_VOSK_LIB}) не найдена. Ищу системную...")
    # Пытаемся найти системную установку
    set(VOSK_TARGET vosk) # Предполагаем, что цель 'vosk' будет найдена системно
endif()

# Создаем исполняемый файл
# ВАЖНО: Убедитесь, что список файлов корректен.
# Исходный файл показывал их без пробелов, что может быть ошибкой.
add_executable(voice-assistant
    main.cpp
    mainwindow.cpp
    mainwindow.h
    voiceassistant.cpp
    voiceassistant.h
)

# === НАЧАЛО: Копирование ресурсов ===
# --- 1. Подготовка к копированию ---
# Флаги для отслеживания необходимости копирования
set(COPY_VOSK_LIB FALSE)
set(COPY_MODEL_DIR FALSE)
set(COPY_COMMANDS_DIR FALSE)

# Проверяем существование ресурсов
if(EXISTS ${LOCAL_VOSK_LIB})
    set(COPY_VOSK_LIB TRUE)
endif()
if(EXISTS ${LOCAL_MODEL_DIR} AND IS_DIRECTORY ${LOCAL_MODEL_DIR})
    set(COPY_MODEL_DIR TRUE)
else()
    message(STATUS "Директория модели (${LOCAL_MODEL_DIR}) не найдена или не является директорией. Пропускаем копирование модели.")
endif()
if(EXISTS ${LOCAL_COMMANDS_DIR} AND IS_DIRECTORY ${LOCAL_COMMANDS_DIR})
    set(COPY_COMMANDS_DIR TRUE)
else()
    message(STATUS "Директория команд (${LOCAL_COMMANDS_DIR}) не найдена или не является директорией. Пропускаем копирование команд.")
endif()
# --- ---

# --- 2. Добавление команд копирования ---
if(COPY_VOSK_LIB OR COPY_MODEL_DIR OR COPY_COMMANDS_DIR)
    # Создаем список команд для add_custom_command
    set(CUSTOM_COPY_COMMANDS "")

    # Копируем libvosk.so, если она существует
    if(COPY_VOSK_LIB)
        list(APPEND CUSTOM_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LOCAL_VOSK_LIB}"
            "$<TARGET_FILE_DIR:voice-assistant>/libvosk.so"
        )
    endif()

    # Копируем директорию model, если она существует
    if(COPY_MODEL_DIR)
        list(APPEND CUSTOM_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${LOCAL_MODEL_DIR}"
            "$<TARGET_FILE_DIR:voice-assistant>/model"
        )
    endif()

    # Копируем директорию commands, если она существует
    if(COPY_COMMANDS_DIR)
        list(APPEND CUSTOM_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${LOCAL_COMMANDS_DIR}"
            "$<TARGET_FILE_DIR:voice-assistant>/commands"
        )
    endif()

    # Добавляем единую команду копирования всех ресурсов после сборки
    add_custom_command(TARGET voice-assistant POST_BUILD
        ${CUSTOM_COPY_COMMANDS}
        COMMENT "Копирование локальных ресурсов (libvosk.so, model, commands) в директорию сборки"
        VERBATIM
    )
endif()
# --- КОНЕЦ: Копирование ресурсов ---

# Линкуем библиотеки
target_link_libraries(voice-assistant
    Qt6::Core
    Qt6::Widgets
    ${ALSA_LIBRARIES}
    # Используем определённую выше цель для Vosk
    ${VOSK_TARGET}
    pthread
)

# Добавляем include директории
target_include_directories(voice-assistant PRIVATE
    ${ALSA_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Добавляем флаги компиляции
target_compile_options(voice-assistant PRIVATE ${ALSA_CFLAGS_OTHER})

