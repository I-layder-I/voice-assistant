#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "CLI/CLI.hpp"
#include "app/application.hpp"
#include "config/config.hpp"
#include "utils/logger.hpp"

namespace {

app::Application* g_app = nullptr;

void onSignal(int) {
  if (g_app) g_app->requestShutdown();
}

logger::Level parseLogLevel(const std::string& str) {
  if (str == "debug") return logger::Level::Debug;
  if (str == "info") return logger::Level::Info;
  if (str == "warn") return logger::Level::Warn;
  if (str == "error") return logger::Level::Error;
  return logger::Level::Info;
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App cli{"Voise Assistant"};

  config::Config cfg;
  std::vector<std::filesystem::path> cli_models;
  std::vector<std::string> cli_languages;
  std::string cli_log_level = "info";

  cli.add_option("-m,--model", cli_models, "Set the Model(s) path(s); repeatable");
  cli.add_option("-c,--commands", cfg.commands_path, "Set the Commands path");
  cli.add_option("-r,--sample-rate", cfg.sample_rate, "Set the Sample Rate")->default_val(16000);

  cli.add_flag("--stdin", cfg.stdin_mode, "Read commands from standard input");

  cli.add_option("--matching", cfg.matching, "Set the Matching")
      ->check(CLI::IsMember({"exact", "substring"}))
      ->default_val("substring");

  cli.add_option("--min-confidence", cfg.min_confidence, "Set minimum confidence for Vosk")
      ->default_val(0.85);

  cli.add_option("--log-level", cli_log_level, "Set log level")
      ->check(CLI::IsMember({"debug", "info", "warn", "error"}))
      ->default_val("info");

  cli.add_flag("--default-paths", cfg.force_default_paths, "Use default Model and Commands paths");
  cli.add_flag("--default-model", cfg.force_default_model_path, "Use default Model path");
  cli.add_flag("--default-commands", cfg.force_default_commands_path, "Use default Commands path");

  cli.add_option("-l,--language", cli_languages, "Set the language(s); repeatable")
      ->check(CLI::IsMember({"en", "ru"}));

  CLI11_PARSE(cli, argc, argv);

  if (!cli_models.empty()) cfg.model_paths = cli_models;
  if (!cli_languages.empty()) cfg.languages = cli_languages;
  cfg.log_level = parseLogLevel(cli_log_level);

  logger::setLevel(cfg.log_level);

  try {
    app::Application application{std::move(cfg)};
    g_app = &application;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    if (!application.start()) {
      logger::error("Failed to start application");
      return EXIT_FAILURE;
    }

    while (application.isRunning()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    application.stop();
  } catch (const std::exception& e) {
    logger::error("Fatal: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
