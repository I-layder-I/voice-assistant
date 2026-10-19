#pragma once

#include <format>
#include <string_view>

namespace logger {

enum class Level { Debug, Info, Warn, Error };

void setLevel(Level lvl);
[[nodiscard]] Level getLevel();
void log(Level lvl, std::string_view msg);

template <typename... Args>
inline void debug(std::format_string<Args...> fmt, Args&&... args) {
  log(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void info(std::format_string<Args...> fmt, Args&&... args) {
  log(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void warn(std::format_string<Args...> fmt, Args&&... args) {
  log(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void error(std::format_string<Args...> fmt, Args&&... args) {
  log(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace logger
