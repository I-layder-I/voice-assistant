#include "recognition/recognizer.hpp"

#include <vosk_api.h>

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "utils/logger.hpp"

namespace recognition {

namespace {

struct ModelDeleter {
  void operator()(VoskModel* m) const noexcept {
    if (m) vosk_model_free(m);
  }
};

struct RecognizerDeleter {
  void operator()(VoskRecognizer* r) const noexcept {
    if (r) vosk_recognizer_free(r);
  }
};

using ModelPtr = std::unique_ptr<VoskModel, ModelDeleter>;
using VoskRecPtr = std::unique_ptr<VoskRecognizer, RecognizerDeleter>;

RecognitionResult ParseVoskResult(const std::string& json) {
  RecognitionResult r;
  try {
    auto j = nlohmann::json::parse(json);
    r.text = j.value("text", "");

    if (j.contains("result") && j["result"].is_array()) {
      double sum = 0.0;
      int n = 0;
      for (const auto& w : j["result"]) {
        if (w.contains("conf")) {
          sum += w["conf"].get<double>();
          ++n;
        }
      }
      if (n > 0) r.confidence = sum / n;
      r.word_count = static_cast<int>(j["result"].size());
    }
  } catch (const nlohmann::json::exception& e) {
    logger::error("Failed to parse Vosk JSON: {}", e.what());
  }
  return r;
}

}  // namespace

struct Recognizer::Impl {
  std::filesystem::path model_path;
  int sample_rate = 16000;

  ModelPtr model;
  VoskRecPtr recognizer;

  bool CreateRecognizer() {
    if (!model) return false;

    VoskRecognizer* raw = vosk_recognizer_new(model.get(), static_cast<float>(sample_rate));
    if (!raw) {
      logger::error("Failed to create Vosk recognizer for '{}'", model_path.string());
      return false;
    }
    recognizer.reset(raw);
    vosk_recognizer_set_words(recognizer.get(), 1);
    return true;
  }
};

Recognizer::Recognizer(std::filesystem::path model_path, int sample_rate)
    : impl_(std::make_unique<Impl>()) {
  impl_->model_path = std::move(model_path);
  impl_->sample_rate = sample_rate;
}

Recognizer::~Recognizer() = default;

Recognizer::Recognizer(Recognizer&&) noexcept = default;
Recognizer& Recognizer::operator=(Recognizer&&) noexcept = default;

bool Recognizer::load() {
  if (impl_->model) {
    logger::warn("Recognizer::load() called twice for '{}'", impl_->model_path.string());
    return true;
  }

  if (logger::getLevel() == logger::Level::Debug) {
    vosk_set_log_level(1);
  } else {
    vosk_set_log_level(-1);
  }

  if (!std::filesystem::is_directory(impl_->model_path)) {
    logger::error("Model path is not a directory: '{}'", impl_->model_path.string());
    return false;
  }

  const std::string path = impl_->model_path.string();
  VoskModel* raw = vosk_model_new(path.c_str());
  if (!raw) {
    logger::error("vosk_model_new failed for '{}'", path);
    return false;
  }
  impl_->model.reset(raw);

  if (!impl_->CreateRecognizer()) {
    impl_->model.reset();
    return false;
  }

  logger::debug("Recognizer loaded: model='{}', rate={} Hz", path, impl_->sample_rate);
  return true;
}

std::optional<RecognitionResult> Recognizer::acceptAudio(std::span<const short> pcm) {
  if (!impl_->recognizer) {
    logger::error("accept_audio() called before load()");
    return std::nullopt;
  }
  if (pcm.empty()) return std::nullopt;

  const auto* data = reinterpret_cast<const char*>(pcm.data());
  const int bytes = static_cast<int>(pcm.size() * sizeof(short));

  const int is_final = vosk_recognizer_accept_waveform(impl_->recognizer.get(), data, bytes);

  if (is_final < 0) {
    logger::error("vosk_recognizer_accept_waveform error");
    return std::nullopt;
  }
  if (is_final == 0) {
    return std::nullopt;
  }

  const char* json = vosk_recognizer_result(impl_->recognizer.get());
  if (!json) return std::nullopt;

  auto result = ParseVoskResult(json);
  if (result.text.empty()) return std::nullopt;

  logger::debug("Recognized: '{}' (conf={:.2f})", result.text, result.confidence);
  return result;
}

std::optional<RecognitionResult> Recognizer::finalize() {
  if (!impl_->recognizer) return std::nullopt;

  const char* json = vosk_recognizer_final_result(impl_->recognizer.get());
  if (!json) return std::nullopt;

  auto result = ParseVoskResult(json);
  vosk_recognizer_reset(impl_->recognizer.get());

  if (result.text.empty()) return std::nullopt;
  return result;
}

void Recognizer::reset() {
  if (impl_->recognizer) vosk_recognizer_reset(impl_->recognizer.get());
}

bool Recognizer::restart() {
  impl_->recognizer.reset();
  return impl_->CreateRecognizer();
}

const std::filesystem::path& Recognizer::modelPath() const noexcept { return impl_->model_path; }

bool Recognizer::isLoaded() const noexcept { return impl_ && impl_->recognizer != nullptr; }

}  // namespace recognition
