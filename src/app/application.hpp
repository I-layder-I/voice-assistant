#pragma once

#include <atomic>
#include <memory>

#include "config/config.hpp"

namespace app {

class Application {
 public:
  explicit Application(config::Config cfg);
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  [[nodiscard]] bool start();
  void stop();
  [[nodiscard]] bool isRunning() const noexcept;

  void requestShutdown() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace app
