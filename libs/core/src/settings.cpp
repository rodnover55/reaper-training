#include "training/core/settings.hpp"

namespace training::core {

Settings::Settings(const Settings &other) : Settings(other, std::scoped_lock(other.mutex_)) {}

Settings &Settings::operator=(const Settings &other) {
  if (this == &other)
    return *this;
  const std::scoped_lock lock(mutex_, other.mutex_);
  window_ = other.window_;
  silenceDb_ = other.silenceDb_;
  return *this;
}

HitWindow Settings::window() const {
  const std::scoped_lock lock(mutex_);
  return window_;
}

void Settings::setWindow(const HitWindow &window) {
  const std::scoped_lock lock(mutex_);
  window_ = window;
}

double Settings::silenceDb() const {
  const std::scoped_lock lock(mutex_);
  return silenceDb_;
}

void Settings::setSilenceDb(double silenceDb) {
  const std::scoped_lock lock(mutex_);
  silenceDb_ = silenceDb;
}

bool Settings::operator==(const Settings &other) const {
  if (this == &other)
    return true;
  const std::scoped_lock lock(mutex_, other.mutex_);
  return window_ == other.window_ && silenceDb_ == other.silenceDb_;
}

} // namespace training::core
