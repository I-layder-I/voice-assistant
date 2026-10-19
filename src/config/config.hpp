#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "utils/logger.hpp"

namespace config {

struct Config {
  bool stdin_mode = false;

  std::string matching = "substring";

  std::vector<std::string> languages = {"en"};

  std::vector<std::filesystem::path> model_paths;
  std::filesystem::path commands_path;

  bool force_default_paths = false;
  bool force_default_model_path = false;
  bool force_default_commands_path = false;

  int sample_rate = 16000;
  double min_confidence = 0.8;
  std::chrono::minutes watchdog_interval{30};

  logger::Level log_level = logger::Level::Info;
};

// TODO: добавить конфиг и его парсинг
// [[nodiscard]] std::optional<Config> parseConfig(const std::filesystem::path& config_path);

}  // namespace config
