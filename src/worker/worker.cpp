#include "worker/worker.hpp"

#include <malloc.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <utility>

#include "paths/paths.hpp"
#include "utils/logger.hpp"

namespace worker {

namespace {

constexpr std::size_t kBufferFrames = 4000;
constexpr int kSilenceThreshold = 500;
constexpr int kSilentStreakLimit = 10;
constexpr auto kEagainSleep = std::chrono::milliseconds{10};

}  // namespace

Worker::Worker(const config::Config& cfg) : cfg_(cfg), buffer_(kBufferFrames) {}

Worker::~Worker() { stop(); }

bool Worker::start() {
  if (running_) return true;

  if (!init()) return false;

  running_ = true;
  last_watchdog_ = std::chrono::steady_clock::now();

  thread_ = std::thread([this] {
    if (cfg_.stdin_mode)
      stdinLoop();
    else
      micLoop();
  });

  return true;
}

void Worker::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();

  capture_.close();
  recognizers_.clear();
  registry_.reset();
}

bool Worker::init() {
  if (!initCommands()) return false;

  if (cfg_.stdin_mode) return true;

  if (!initAudio()) return false;
  return initRecognizers();
}

bool Worker::initCommands() {
  const auto cmd_path = paths::getCommandsPath({
      .commands_path = cfg_.commands_path,
      .force_default_paths = cfg_.force_default_paths,
      .force_default_commands_path = cfg_.force_default_commands_path,
  });

  if (cmd_path.empty()) {
    logger::error("No commands path resolved; aborting");
    return false;
  }

  registry_ = std::make_unique<commands::Registry>(cmd_path, cfg_.matching);
  return registry_->load();
}

bool Worker::initAudio() {
  if (!capture_.open("default", cfg_.sample_rate, 1)) {
    logger::error("Failed to open audio capture");
    return false;
  }
  return true;
}

bool Worker::initRecognizers() {
  const auto model_paths = paths::getModelPaths({
      .model_paths = cfg_.model_paths,
      .languages = cfg_.languages,
      .force_default_paths = cfg_.force_default_paths,
      .force_default_model_path = cfg_.force_default_model_path,
  });

  if (model_paths.empty()) {
    logger::error("No model paths resolved; aborting");
    return false;
  }

  for (const auto& path : model_paths) {
    recognition::Recognizer rec{path, cfg_.sample_rate};
    if (!rec.load()) {
      logger::error("Failed to load model from '{}'", path.string());
      return false;
    }
    recognizers_.push_back(std::move(rec));
  }

  logger::info("Loaded {} recognizer(s)", recognizers_.size());
  return true;
}

// ---
// Mic loop

void Worker::micLoop() {
  while (running_) {
    if (!readAudio()) continue;
    processRecognizers();
    watchdogTick();
  }
}

bool Worker::readAudio() {
  const int frames = capture_.read(buffer_);

  if (frames < 0) {
    logger::error("Audio read failed; stopping worker");
    running_ = false;
    return false;
  }

  if (frames == 0) {
    std::this_thread::sleep_for(kEagainSleep);
    return false;
  }

  audio_frames_ = frames;
  return true;
}

void Worker::processRecognizers() {
  if (audio_frames_ == 0) return;

  const auto pcm = std::span<const short>(buffer_.data(), static_cast<std::size_t>(audio_frames_));

  if (isSilence(pcm.data(), pcm.size())) {
    std::memset(buffer_.data(), 0, audio_frames_ * sizeof(short));
  }

  struct Hit {
    recognition::RecognitionResult result;
    std::optional<commands::MatchResult> match;
  };

  std::vector<Hit> hits;
  hits.reserve(recognizers_.size());

  for (auto& rec : recognizers_) {
    // Теперь Vosk всегда получает данные (речь или нули)
    auto result = rec.acceptAudio(pcm);
    if (!result) continue;

    Hit h{.result = *result, .match = registry_->match(result->text)};
    hits.push_back(std::move(h));
  }
  if (hits.empty()) return;

  std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
    const auto aw = a.match ? a.match->matched_words : 0;
    const auto bw = b.match ? b.match->matched_words : 0;
    if (aw != bw) return aw > bw;
    return a.result.confidence > b.result.confidence;
  });

  for (const auto& h : hits) {
    if (!h.match) continue;
    if (h.result.confidence < cfg_.min_confidence) {
      logger::debug("Skip '{}': conf={:.2f} < {}", h.result.text, h.result.confidence,
                    cfg_.min_confidence);
      continue;
    }

    logger::info("Executing '{}' (text='{}', conf={:.2f})", h.match->command_name, h.result.text,
                 h.result.confidence);
    if (!registry_->execute(h.match->script_path)) {
      logger::warn("Failed to execute command for text: '{}'", h.result.text);
    }
  }
  return;
}

void Worker::watchdogTick() {
  const auto now = std::chrono::steady_clock::now();

  if (now - last_watchdog_ > cfg_.watchdog_interval) {
    logger::debug("watchdog: trimming memory and restarting recognizers");
    malloc_trim(0);
    for (auto& rec : recognizers_) {
      if (!rec.restart()) {
        logger::error("watchdog: restart failed for '{}'", rec.modelPath().string());
      }
    }
    last_watchdog_ = now;
  }
}

bool Worker::isSilence(const short* data, std::size_t frames) {
  if (frames == 0) return true;

  double sum = 0.0;
  for (std::size_t i = 0; i < frames; ++i) {
    const double v = static_cast<double>(data[i]);
    sum += v * v;
  }
  const double rms = std::sqrt(sum / frames);
  return rms < kSilenceThreshold;
}

// ---
// Stdin loop

void Worker::stdinLoop() {
  std::string text;

  while (running_) {
    pollfd pfd{};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    int result = poll(&pfd, 1, 100);

    if (result < 0) {
      if (errno == EINTR) continue;
      break;
    }

    if (result == 0) continue;

    if (pfd.revents & POLLIN) {
      if (!std::getline(std::cin, text)) break;

      if (!registry_) break;

      auto m = registry_->match(text);
      if (!m) {
        logger::debug("No match for '{}'", text);
        continue;
      }

      logger::info("Executing '{}' (text='{}')", m->command_name, text);
      if (!registry_->execute(m->script_path)) {
        logger::warn("Failed to execute command for text: '{}'", text);
      }
    }

    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
  }

  running_ = false;
}

}  // namespace worker
