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

# === Явно указать путь к libvosk.so ===
# Путь к локальной libvosk.so (в каталоге сборки или исходников)
set(LOCAL_VOSK_LIB "${CMAKE_CURRENT_SOURCE_DIR}/libvosk.so")

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
add_executable(voice-assistant
    main.cpp
    mainwindow.cpp
    mainwindow.h
    voiceassistant.cpp
    voiceassistant.h
)

# --- Добавлено: Копирование libvosk.so и установка RPATH ---
if(EXISTS ${LOCAL_VOSK_LIB})
    # Скопировать libvosk.so в директорию сборки после компиляции
    # Это гарантирует, что библиотека будет рядом с исполняемым файлом
    add_custom_command(TARGET voice-assistant POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LOCAL_VOSK_LIB}
        $<TARGET_FILE_DIR:voice-assistant>
    )

    # Установить RPATH в $ORIGIN (директория исполняемого файла)
    # Это заставит динамический компоновщик искать библиотеки в той же директории, что и исполняемый файл.
    set_target_properties(voice-assistant PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()
# --- ---

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
