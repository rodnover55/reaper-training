#include "training/core/transport.hpp"

#include <memory>
#include <utility>

namespace training::core {

Transport::Transport(const Settings &settings, SampleRate sampleRate)
    : sampleRate_(sampleRate), settings_(settings),
      midi_(std::make_shared<training::Midi>(settings.window())) {}

std::expected<std::shared_ptr<calibration::Calibrator>, CannotStartUnit>
Transport::start(Calibration /*unit*/) {
  const std::scoped_lock lock(mutex_);
  stopUnits();
  calibrator_.reset();
  std::expected<std::unique_ptr<calibration::Calibrator>, CannotStartUnit> created =
      calibration::Calibrator::create(sampleRate_);
  if (!created)
    return std::unexpected(created.error());
  calibrator_ = std::move(*created);
  (void)calibrator_->start();
  return calibrator_;
}

std::expected<std::shared_ptr<training::Audio>, CannotStartUnit>
Transport::start(Training<training::Trainer::Type::Audio> /*unit*/) {
  const std::scoped_lock lock(mutex_);
  stopUnits();
  audio_.reset();
  std::expected<std::unique_ptr<training::Audio>, CannotStartUnit> created =
      training::Audio::create(sampleRate_, settings_);
  if (!created)
    return std::unexpected(created.error());
  audio_ = std::move(*created);
  (void)audio_->start();
  return audio_;
}

std::expected<std::shared_ptr<training::Midi>, CannotStartUnit>
Transport::start(Training<training::Trainer::Type::Midi> /*unit*/) {
  const std::scoped_lock lock(mutex_);
  stopUnits();
  (void)midi_->start();
  return midi_;
}

void Transport::stop() {
  const std::scoped_lock lock(mutex_);
  stopUnits();
}

void Transport::setSettings(const Settings &settings) {
  const std::scoped_lock lock(mutex_);
  settings_ = settings;
  midi_->setSettings(settings_);
  if (audio_)
    audio_->setSettings(settings_);
}

void Transport::stopUnits() {
  if (calibrator_)
    calibrator_->stop();
  if (audio_)
    audio_->stop();
  midi_->stop();
}

} // namespace training::core
