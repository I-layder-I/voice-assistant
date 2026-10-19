#include "audio/capture.hpp"

#include <alsa/asoundlib.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "utils/logger.hpp"

namespace audio {

namespace {

constexpr std::size_t kBufferframes = 4000;

struct PcmDeleter {
  void operator()(snd_pcm_t* handle) const noexcept {
    if (handle) snd_pcm_close(handle);
  }
};

using PcmPtr = std::unique_ptr<snd_pcm_t, PcmDeleter>;

bool fail(PcmPtr& handle, const char* stage, int err) {
  logger::error("ALSA {} failed: {}", stage, snd_strerror(err));
  handle.reset();
  return false;
}

}  // namespace

struct Capture::Impl {
  PcmPtr handle;
  int sample_rate = 16000;
  int channels = 1;
};

Capture::Capture() : impl_(std::make_unique<Impl>()) {}

Capture::~Capture() = default;

bool Capture::isOpen() const noexcept { return impl_ && impl_->handle != nullptr; }

bool Capture::open(const std::string& device, int sample_rate, int channels) {
  if (isOpen()) {
    logger::warn("Capture::open called while already open");
    return false;
  }

  if (sample_rate <= 0) {
    logger::error("Invalid sample rate: {}", sample_rate);
    return false;
  }

  if (channels <= 0) {
    logger::error("Invalid channel count: {}", channels);
    return false;
  }

  snd_pcm_t* raw = nullptr;
  int err = snd_pcm_open(&raw, device.c_str(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
  if (err < 0) {
    logger::error("snd_pcm_open('{}'): {}", device, snd_strerror(err));
    return false;
  }
  impl_->handle.reset(raw);

  snd_pcm_hw_params_t* params = nullptr;
  snd_pcm_hw_params_alloca(&params);

  if ((err = snd_pcm_hw_params_any(impl_->handle.get(), params)) < 0)
    return fail(impl_->handle, "snd_pcm_hw_params_any", err);

  if ((err = snd_pcm_hw_params_set_access(impl_->handle.get(), params,
                                          SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    return fail(impl_->handle, "set_access", err);

  if ((err = snd_pcm_hw_params_set_format(impl_->handle.get(), params, SND_PCM_FORMAT_S16_LE)) < 0)
    return fail(impl_->handle, "set_format", err);

  unsigned int rate = static_cast<unsigned int>(sample_rate);
  if ((err = snd_pcm_hw_params_set_rate_near(impl_->handle.get(), params, &rate, nullptr)) < 0)
    return fail(impl_->handle, "set_rate_near", err);

  if (rate != static_cast<unsigned int>(sample_rate)) {
    logger::error("Unsupported sample rate: {} Hz (expected {} Hz)", rate, sample_rate);
    impl_->handle.reset();
    return false;
  }

  if ((err = snd_pcm_hw_params_set_channels(impl_->handle.get(), params,
                                            static_cast<unsigned int>(channels))) < 0)
    return fail(impl_->handle, "set_channels", err);

  if ((err = snd_pcm_hw_params(impl_->handle.get(), params)) < 0)
    return fail(impl_->handle, "snd_pcm_hw_params", err);

  if ((err = snd_pcm_prepare(impl_->handle.get())) < 0)
    return fail(impl_->handle, "snd_pcm_prepare", err);

  impl_->sample_rate = sample_rate;
  impl_->channels = channels;

  logger::info("Audio capture opened: device='{}', rate={} Hz, channels={}", device, sample_rate,
               channels);
  return true;
}

void Capture::close() {
  if (impl_ && impl_->handle) {
    impl_->handle.reset();
    logger::debug("Audio capture closed");
  }
}

int Capture::read(std::span<short> buffer) {
  if (!isOpen()) {
    logger::error("Capture::read called while not open");
    return -1;
  }

  if (buffer.empty()) {
    logger::warn("Capture::read called with empty buffer");
    return 0;
  }

  snd_pcm_sframes_t frames = snd_pcm_readi(impl_->handle.get(), buffer.data(), buffer.size());

  if (frames == -EAGAIN) {
    return 0;
  }

  if (frames < 0) {
    int err = snd_pcm_recover(impl_->handle.get(), static_cast<int>(frames), 0);
    if (err < 0) {
      logger::error("ALSA recovery error: {}", snd_strerror(err));
      impl_->handle.reset();
      return -1;
    }
    logger::debug("ALSA recovered from underrun/overrun");
    return 0;
  }

  return static_cast<int>(frames);
}

}  // namespace audio
