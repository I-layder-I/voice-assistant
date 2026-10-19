#include "utils/logger.hpp"

#include <cstdio>
#include <ctime>

namespace logger {

namespace {

Level g_level = Level::Info;

const char* levelName(Level l) {
  switch (l) {
    case Level::Debug:
      return "DEBUG";
    case Level::Info:
      return "INFO";
    case Level::Warn:
      return "WARN";
    case Level::Error:
      return "ERROR";
  }
  return "?";
}

}  // namespace

void setLevel(Level lvl) { g_level = lvl; }

Level getLevel() { return g_level; };

void log(Level lvl, std::string_view msg) {
  if (lvl < g_level) return;
  std::fprintf(stderr, "[%s] %.*s\n", levelName(lvl), static_cast<int>(msg.size()), msg.data());
}

}  // namespace logger
