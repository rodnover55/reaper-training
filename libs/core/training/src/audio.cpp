#include "training/core/training/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <utility>

namespace training::core::training {
namespace {

/// Сколько звука помнятся блоки: атака находится позже своего начала, и блок
/// её начала должен найтись.
constexpr Interval kHistory = Interval::kSecond;

} // namespace

std::expected<std::unique_ptr<Audio>, CannotStartUnit>
Audio::create(SampleRate sampleRate, const Settings &settings) {
  std::optional<detector::Detector> detector = detector::Detector::create(
      detector::Settings{.sampleRate = sampleRate, .silenceDb = settings.silenceDb()});
  if (!detector)
    return std::unexpected(CannotStartUnit{});
  return std::make_unique<Audio>(Key(), std::move(*detector), settings.window());
}

Audio::Audio(Key /*key*/, detector::Detector detector, const HitWindow &hitWindow)
    : Trainer(hitWindow), rate_(detector.settings().sampleRate),
      detector_(std::move(detector)) {}

std::expected<void, InvalidStep> Audio::start() {
  const std::scoped_lock lock(mutex_);
  std::expected<void, InvalidStep> started = Trainer::start();
  // Время ядра у запуска своё: с концом звука до запуска блоки не сравнивают,
  // а поток детектора начинается с первого блока запуска.
  if (started) {
    heardEnd_.reset();
    history_.clear();
    detector_.reset();
    position_ = 0;
  }
  return started;
}

void Audio::setSettings(const Settings &settings) {
  // Окно попадания и порог тишины ставятся под одним замком: одновременные
  // вызовы не смешивают части разных настроек.
  const std::scoped_lock lock(mutex_);
  Trainer::setSettings(settings);
  // `Transport.setSettings`: порог тишины детектор получает без пересоздания —
  // поток и непрочитанные атаки остаются.
  detector_.setSilenceDb(settings.silenceDb());
}

void Audio::receive(const input::AudioBlock &block) {
  // Ноты ставятся под замком разбора, который берёт и `start`: каждая нота
  // ложится в тот запуск, в котором её нашли (`Audio.receive`).
  const std::scoped_lock lock(mutex_);
  if (!working())
    return;

  // `SoundReceiver::receive#order`, `#xrun`: стык с разобранным звуком — в
  // целых сэмплах, расхождение меньше половины сэмпла — округление времени.
  const double hz = rate_.hz();
  std::span<const float> samples = block.samples;
  Moment start = block.start;
  if (heardEnd_) {
    const auto away = std::llround((start - *heardEnd_) / Interval::kSecond * hz);
    if (away > 0) {
      // Пропавший звук занимает время: детектор разбирает его как тишину.
      detector_.skip(static_cast<std::uint64_t>(away));
      position_ += static_cast<std::uint64_t>(away);
    } else if (away < 0) {
      // Уже разобранное начало блока отрезается.
      const std::size_t cut = std::min(static_cast<std::size_t>(-away), samples.size());
      if (cut == samples.size())
        return;
      samples = samples.subspan(cut);
      start = block.start + static_cast<double>(cut) / hz * Interval::kSecond;
    }
  }

  heardEnd_ = start + static_cast<double>(samples.size()) / hz * Interval::kSecond;

  history_.push_back({.streamStart = position_, .start = start});
  detector_.process(samples);
  position_ += samples.size();
  takeOnsets();

  // Блоки старше `kHistory` звука больше не понадобятся.
  const auto keepSamples = static_cast<std::uint64_t>(kHistory / Interval::kSecond * hz);
  const std::uint64_t now = position_;
  const auto stale =
      std::ranges::find_if(history_, [now, keepSamples](const StreamBlock &item) {
        return item.streamStart + keepSamples >= now;
      });
  // Последний блок остаётся всегда: атака в нём может найтись позже.
  history_.erase(history_.begin(), std::min(stale, std::prev(history_.end())));
}

void Audio::takeOnsets() {
  while (const std::optional<detector::Onset> onset = detector_.takeOnset()) {
    // Блок начала атаки — последний из начавшихся не позже неё.
    auto newestFirst = std::views::reverse(history_);
    const auto from = std::ranges::find_if(newestFirst, [&onset](const StreamBlock &item) {
      return item.streamStart <= onset->position;
    });
    if (from == newestFirst.end())
      continue;
    const auto offset = static_cast<double>(onset->position - from->streamStart);
    process({.time = from->start + offset / rate_.hz() * Interval::kSecond});
  }
}

} // namespace training::core::training
