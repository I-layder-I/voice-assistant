#include "app/application.hpp"

#include <memory>

#include "utils/logger.hpp"
#include "worker/worker.hpp"

namespace app {

struct Application::Impl {
  config::Config cfg;
  std::unique_ptr<worker::Worker> w;
  std::atomic<bool> shutdownRequested{false};
};

Application::Application(config::Config cfg) : impl_(std::make_unique<Impl>()) {
  impl_->cfg = std::move(cfg);
}

Application::~Application() { stop(); }

bool Application::start() {
  impl_->w = std::make_unique<worker::Worker>(impl_->cfg);
  return impl_->w->start();
}

void Application::stop() {
  if (impl_ && impl_->w) {
    impl_->w->stop();
    impl_->w.reset();
  }
}

bool Application::isRunning() const noexcept {
  return impl_ && impl_->w && impl_->w->isRunning() && !impl_->shutdownRequested;
}

void Application::requestShutdown() noexcept { impl_->shutdownRequested = true; }

}  // namespace app
