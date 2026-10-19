#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace commands {

struct MatchResult {
  std::string command_name;
  std::filesystem::path script_path;
  std::size_t matched_words = 0;
};

class Registry {
 public:
  Registry(std::filesystem::path dir, std::string matching = "substring");
  ~Registry();

  [[nodiscard]] bool load();

  [[nodiscard]] std::optional<MatchResult> match(const std::string& text) const;

  [[nodiscard]] bool execute(const std::filesystem::path& script_path) const;

  [[nodiscard]] bool handle(const std::string& text) const;

 private:
  struct Command;

  std::vector<Command> commands_;
  std::filesystem::path dir_;
  std::string matching_;
};

}  // namespace commands
