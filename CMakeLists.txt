cmake_minimum_required(VERSION 3.20)
project(voice-assistant LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CLI11 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(nlohmann_json REQUIRED)

find_path(VOSK_INCLUDE_DIR vosk_api.h)
find_library(VOSK_LIBRARY vosk)

add_executable(
  voice-assistant
  src/main.cpp
  src/app/application.cpp
  src/worker/worker.cpp
  src/commands/registry.cpp
  src/audio/capture.cpp
  src/recognition/recognizer.cpp
  src/paths/paths.cpp
  src/utils/logger.cpp)
target_include_directories(voice-assistant PRIVATE src)
target_link_libraries(
  voice-assistant PRIVATE CLI11::CLI11 ${ALSA_LIBRARIES} ICU::uc ICU::i18n
                          ${VOSK_LIBRARY} nlohmann_json::nlohmann_json)
