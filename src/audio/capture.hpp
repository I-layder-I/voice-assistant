#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace audio {

class Capture {
 public:
  Capture();
  ~Capture();

  Capture(const Capture&) = delete;
  Capture& operator=(const Capture&) = delete;

  [[nodiscard]] bool open(const std::string& device, int sample_rate, int channels = 1);

  void close();

  [[nodiscard]] bool isOpen() const noexcept;

  [[nodiscard]] int read(std::span<short> buffer);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace audio
