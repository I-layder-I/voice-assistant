#include "commands/registry.hpp"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "utils/logger.hpp"

namespace fs = std::filesystem;

namespace commands {

namespace {

struct KeyPhrase {
  std::string phrase;
  std::vector<std::string> words;
};

std::string normalizeText(const std::string& text);
std::vector<std::string> splitWords(const std::string& text);
bool containsAnyPhrase(const std::vector<std::string>& text,
                       const std::vector<std::string>& key_phrase_words);
std::vector<KeyPhrase> extractKeyPhrases(const fs::path& script);
std::vector<fs::path> findScripts(const fs::path& dir);
std::string trim(const std::string& text);

}  // namespace

struct Registry::Command {
  std::string name;
  fs::path script_path;
  std::vector<KeyPhrase> key_phrases;
};

Registry::Registry(std::filesystem::path dir, std::string matching)
    : dir_(std::move(dir)), matching_(std::move(matching)) {}

Registry::~Registry() = default;

bool Registry::load() {
  if (!fs::is_directory(dir_)) {
    logger::error("Directory '{}' does not exists; aborting.", dir_.string());
    return false;
  }

  std::vector<fs::path> scripts = findScripts(dir_);
  if (scripts.empty()) {
    logger::error("No scripts found in '{}'", dir_.string());
    return false;
  }

  std::unordered_map<std::string, std::string> owners;
  for (const auto& s : scripts) {
    Command command;

    command.name = s.stem().string();
    command.script_path = s;
    command.key_phrases = extractKeyPhrases(s);

    if (command.key_phrases.empty()) {
      logger::warn("No key phrases in '{}', skipping", command.name);
      continue;
    }

    for (const auto& key_phrase : command.key_phrases) {
      auto [owner, inserted] = owners.emplace(key_phrase.phrase, command.name);

      if (!inserted) {
        logger::error("Duplicate key phrase '{}': already in '{}', also in '{}'", key_phrase.phrase,
                      owner->second, command.name);
        return false;
      }
    }

    commands_.push_back(std::move(command));
  }

  if (commands_.empty()) {
    logger::error("All scripts were skipped; nothing to load");
    return false;
  }

  logger::info("Loaded {} commands(s) from '{}'", commands_.size(), dir_.string());

  for (const auto& command : commands_) {
    logger::debug("  '{}':", command.name);
    for (const auto& phrase : command.key_phrases) {
      logger::debug("    \"{}\"", phrase.phrase);
    }
  }
  return true;
}

std::optional<MatchResult> Registry::match(const std::string& text) const {
  std::vector<std::string> text_words = splitWords(normalizeText(text));

  if (text_words.empty()) {
    logger::debug("Empty text after normalization, no match");
    return std::nullopt;
  }

  MatchResult best;

  for (const auto& command : commands_) {
    for (size_t i = 0; i < command.key_phrases.size(); ++i) {
      bool ok = false;
      if (matching_ == "exact")
        ok = (command.key_phrases[i].words == text_words);
      else
        ok = containsAnyPhrase(text_words, command.key_phrases[i].words);

      if (!ok) continue;

      if (command.key_phrases[i].words.size() > best.matched_words) {
        best.matched_words = command.key_phrases[i].words.size();
        best.command_name = command.name;
        best.script_path = command.script_path;
      }
    }
  }

  if (best.command_name.empty()) return std::nullopt;
  return best;
}

bool Registry::execute(const fs::path& script_path) const {
  if (!fs::exists(script_path)) {
    logger::error("Script path not exists '{}'; aborting", script_path.string());
    return false;
  }

  int pipefd[2];

  if (pipe(pipefd) < 0) return false;

  int flags = fcntl(pipefd[1], F_GETFD);

  if (flags < 0 || fcntl(pipefd[1], F_SETFD, flags | FD_CLOEXEC) < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  pid_t pid = fork();

  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  if (pid == 0) {
    close(pipefd[0]);

    if (setsid() < 0) {
      int err = errno;

      if (write(pipefd[1], &err, sizeof(err)) != sizeof(err)) _exit(1);

      _exit(1);
    }

    pid_t pid2 = fork();

    if (pid2 < 0) {
      int err = errno;

      if (write(pipefd[1], &err, sizeof(err)) != sizeof(err)) _exit(1);

      _exit(1);
    }

    if (pid2 > 0) {
      close(pipefd[1]);
      _exit(0);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    execl("/bin/bash", "bash", script_path.c_str(), nullptr);

    int err = errno;
    if (write(pipefd[1], &err, sizeof(err)) != sizeof(err)) _exit(1);

    _exit(1);
  }

  close(pipefd[1]);

  int status;
  pid_t result;

  do {
    result = waitpid(pid, &status, 0);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    close(pipefd[0]);
    return false;
  }

  int exec_errno = 0;
  ssize_t bytes = read(pipefd[0], &exec_errno, sizeof(exec_errno));

  close(pipefd[0]);

  if (bytes > 0) {
    logger::error("Failed to execute command '{}': {}", script_path.stem().string(),
                  strerror(exec_errno));
    return false;
  }

  return true;
}

bool Registry::handle(const std::string& text) const {
  const auto m = match(text);
  if (!m) {
    logger::debug("No command matched '{}'", text);
    return false;
  }
  return execute(m->script_path);
}

namespace {

bool containsAnyPhrase(const std::vector<std::string>& text,
                       const std::vector<std::string>& key_phrase) {
  if (key_phrase.empty() || key_phrase.size() > text.size()) return false;

  for (size_t i = 0; i <= text.size() - key_phrase.size(); ++i) {
    bool match = true;

    for (size_t j = 0; j < key_phrase.size(); ++j) {
      if (text[i + j] != key_phrase[j]) {
        match = false;
        break;
      }
    }

    if (match) {
      return true;
    }
  }

  return false;
}

std::vector<fs::path> findScripts(const fs::path& dir) {
  std::vector<fs::path> files;

  if (!fs::is_directory(dir)) return {};

  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".sh") files.push_back(entry.path());
  }

  return files;
}

std::vector<KeyPhrase> extractKeyPhrases(const fs::path& script) {
  std::vector<KeyPhrase> result;

  std::ifstream file(script);
  if (!file) return {};

  std::string line;

  while (std::getline(file, line)) {
    line = normalizeText(line);

    auto marker_end = line.find(':');

    if (marker_end == std::string::npos) continue;

    std::string marker = line.substr(0, marker_end);

    marker.erase(std::remove_if(marker.begin(), marker.end(),
                                [](unsigned char c) { return std::isspace(c); }),
                 marker.end());

    if (marker != "#words") continue;

    std::stringstream ss(line.substr(marker_end + 1));
    std::string phrase;
    while (std::getline(ss, phrase, ',')) {
      phrase = trim(phrase);
      if (phrase.empty()) continue;

      KeyPhrase kp;
      kp.phrase = phrase;
      kp.words = splitWords(phrase);
      result.push_back(std::move(kp));
    }
  }
  return result;
}

std::string normalizeText(const std::string& text) {
  UErrorCode status = U_ZERO_ERROR;

  const icu::Normalizer2* normalizer = icu::Normalizer2::getNFCInstance(status);

  if (U_FAILURE(status)) return text;

  icu::UnicodeString unicode = icu::UnicodeString::fromUTF8(text);

  unicode.toLower();

  icu::UnicodeString normalized = normalizer->normalize(unicode, status);

  if (U_FAILURE(status)) return text;

  std::string result;
  normalized.toUTF8String(result);

  return result;
}

std::string trim(const std::string& text) {
  size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";

  size_t end = text.find_last_not_of(" \t\r\n");

  return text.substr(start, end - start + 1);
}
std::vector<std::string> splitWords(const std::string& text) {
  std::vector<std::string> words;
  std::stringstream ss(text);
  std::string word;
  while (ss >> word) words.push_back(std::move(word));
  return words;
}

}  // namespace

}  // namespace commands
