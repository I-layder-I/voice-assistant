#include "paths/paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <vector>

#include "utils/logger.hpp"

namespace fs = std::filesystem;

namespace paths {

namespace {

std::optional<fs::path> requireHome(std::string_view what) {
  const char* home = std::getenv("HOME");
  if (!home) {
    logger::error(
        "HOME is not set; cannot build default {} path. "
        "Set HOME or pass the path explicitly.",
        what);
    return std::nullopt;
  }
  return fs::path{home};
}
bool ensureDirectory(const fs::path& p, std::string_view what) {
  if (fs::is_directory(p)) return true;
  logger::error("{} path '{}' is not a directory", what, p.string());
  return false;
}

}  // namespace

std::vector<fs::path> getModelPaths(const PathsConfig& cfg) {
  std::vector<fs::path> modelPaths;

  const bool needDefault = cfg.force_default_paths || cfg.force_default_model_path;

  if (!cfg.model_paths.empty() && !needDefault) {
    std::vector<fs::path> result;
    for (const auto& p : cfg.model_paths) {
      if (!ensureDirectory(p, "model")) continue;
      result.push_back(p);
    }
    if (result.empty()) {
      logger::error("No valid model paths given.");
      return {};
    }
    return result;
  }

  for (const auto& lang : cfg.languages) {
    fs::path localModelPath = fs::path("./models") / lang;
    fs::path chosen;

    const bool localExists = fs::is_directory(localModelPath);
    const bool mightNeedDefault = needDefault || !localExists;

    fs::path defaultModelPath;
    if (mightNeedDefault) {
      auto home = requireHome("model");
      if (!home) continue;

      defaultModelPath = *home / ".local/share/voice-assistant/models" / lang;
    }

    if (needDefault) {
      chosen = defaultModelPath;
    } else if (localExists) {
      chosen = localModelPath;
    } else if (fs::is_directory(defaultModelPath)) {
      chosen = defaultModelPath;
    }

    if (chosen.empty() || !fs::is_directory(chosen)) {
      logger::warn("Model for language '{}' not found", lang);
      continue;
    }

    logger::info("Model '{}': {}", lang, chosen.string());
    modelPaths.push_back(std::move(chosen));
  }

  if (modelPaths.empty()) {
    logger::error("No models resolved; aborting");
    return {};
  }

  return modelPaths;
}

fs::path getCommandsPath(const PathsConfig& cfg) {
  const bool needDefault = cfg.force_default_paths || cfg.force_default_commands_path;

  if (!cfg.commands_path.empty() && !needDefault) {
    if (!ensureDirectory(cfg.commands_path, "commands")) return {};
    return cfg.commands_path;
  }

  fs::path localCommandsPath = fs::path("./commands");
  fs::path chosen;

  const bool localExists = fs::is_directory(localCommandsPath);
  const bool mightNeedDefault = needDefault || !localExists;

  fs::path defaultCommandsPath;
  if (mightNeedDefault) {
    auto home = requireHome("commands");
    if (!home) return {};
    defaultCommandsPath = *home / ".local/share/voice-assistant/commands";
  }

  if (needDefault) {
    chosen = defaultCommandsPath;
  } else if (localExists) {
    chosen = localCommandsPath;
  } else if (fs::is_directory(defaultCommandsPath)) {
    chosen = defaultCommandsPath;
  }

  if (chosen.empty() || !fs::is_directory(chosen)) {
    logger::error("Commands path not found");
    return {};
  }

  logger::info("Commands path: {}", chosen.string());
  return chosen;
}

}  // namespace paths
