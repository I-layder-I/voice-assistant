#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace recognition {

struct RecognitionResult {
  std::string text;
  double confidence = 0.0;
  int word_count = 0;
};

struct AcceptResult {
  bool has_activity = false;
  std::optional<RecognitionResult> final_result;
};

class Recognizer {
 public:
  explicit Recognizer(std::filesystem::path model_path, int sample_rate);
  ~Recognizer();

  Recognizer(const Recognizer&) = delete;
  Recognizer& operator=(const Recognizer&) = delete;
  Recognizer(Recognizer&&) noexcept;
  Recognizer& operator=(Recognizer&&) noexcept;

  [[nodiscard]] bool load();

  [[nodiscard]] std::optional<RecognitionResult> acceptAudio(std::span<const short> pcm);

  [[nodiscard]] std::optional<RecognitionResult> finalize();

  void reset();

  [[nodiscard]] bool restart();

  [[nodiscard]] const std::filesystem::path& modelPath() const noexcept;
  [[nodiscard]] bool isLoaded() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace recognition
