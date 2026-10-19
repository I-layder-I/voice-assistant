#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio/capture.hpp"
#include "commands/registry.hpp"
#include "config/config.hpp"
#include "recognition/recognizer.hpp"

namespace worker {

class Worker {
 public:
  explicit Worker(const config::Config& cfg);
  ~Worker();

  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  [[nodiscard]] bool start();
  void stop();

  [[nodiscard]] bool isRunning() const noexcept { return running_.load(); }

 private:
  bool init();
  bool initRecognizers();
  bool initAudio();
  bool initCommands();

  void micLoop();
  void stdinLoop();

  bool readAudio();
  void processRecognizers();

  void watchdogTick();

  static bool isSilence(const short* data, std::size_t frames);

  config::Config cfg_;

  std::atomic<bool> running_{false};
  std::thread thread_;

  std::unique_ptr<commands::Registry> registry_;
  audio::Capture capture_;
  std::vector<recognition::Recognizer> recognizers_;

  std::vector<short> buffer_;
  int audio_frames_;

  std::chrono::steady_clock::time_point last_watchdog_{};
};

}  // namespace worker
