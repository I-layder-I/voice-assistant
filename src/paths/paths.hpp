#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace paths {

struct PathsConfig {
  std::vector<std::filesystem::path> model_paths;
  std::filesystem::path commands_path;

  std::vector<std::string> languages;

  bool force_default_paths = false;
  bool force_default_model_path = false;
  bool force_default_commands_path = false;
};

std::vector<std::filesystem::path> getModelPaths(const PathsConfig& cfg);

std::filesystem::path getCommandsPath(const PathsConfig& cfg);

}  // namespace paths
