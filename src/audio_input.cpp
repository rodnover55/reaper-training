#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_Audio_RegHardwareHook
#define REAPERAPI_WANT_GetPlayPosition2Ex
#define REAPERAPI_WANT_GetPlayStateEx

#include "audio_input.hpp"

#include <reaper_plugin_functions.h>

#include "journal.hpp"

#include <algorithm>
#include <chrono>
#include <ranges>
#include <span>

namespace training::reaper {
namespace {

// Биты состояния транспорта (`GetPlayStateEx`).
constexpr int kPlaying = 1;
constexpr int kPaused = 2;
constexpr int kRecording = 4;

/// За сколько секунд звука помнить блоки: атака сообщается позже своего
/// начала, и блок её начала должен найтись.
constexpr double kHistorySeconds = 1.0;

/// Секунды монотонных часов — тех же, что в журнале.
double monotonicNow() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

} // namespace

AudioInput::AudioInput()
    : samples_(std::make_unique<concurrency::SpscRing<float, (1U << 18)>>()) {
  blockSamples_.reserve(kMaxBlock);

  worker_ = std::thread([this] { work(); });

  hook_.OnAudioBuffer = onAudioBuffer;
  hook_.userdata1 = this;
  registered_ = Audio_RegHardwareHook(true, &hook_) > 0;
  journal("audio hook registered={}", registered_);
}

AudioInput::~AudioInput() {
  // Хук — первым: пока он снят, звуковой поток не пишет в очереди.
  if (registered_)
    Audio_RegHardwareHook(false, &hook_);

  stop_.store(true);
  if (worker_.joinable())
    worker_.join();
}

void AudioInput::onAudioBuffer(bool isPost, int length, double sampleRate,
                               audio_hook_register_t *registration) {
  // Звуковой поток: только копирование — ни блокировок, ни выделения памяти
  // (design.md D3).
  if (isPost || !registration || !registration->userdata1)
    return;

  static_cast<AudioInput *>(registration->userdata1)
      ->onBlock(length, sampleRate, registration);
}

void AudioInput::onBlock(int length, double sampleRate, audio_hook_register_t *registration) {
  const int state = GetPlayStateEx(nullptr);
  sampleRate_.store(sampleRate, std::memory_order_relaxed);

  BlockHeader header{.sequence = nextSequence_++,
                     .position = GetPlayPosition2Ex(nullptr),
                     .sampleRate = sampleRate,
                     .monotonic = monotonicNow(),
                     .length = length};

  // Места для заголовка нет — пропадает весь блок, а пропуск номера скажет
  // рабочему потоку, что поток сэмплов разорван.
  if (headers_.freeSpace() == 0) {
    (void)headers_.push(header);
    return;
  }

  const bool playing = (state & kPaused) == 0 && (state & (kPlaying | kRecording)) != 0;
  const int channel = channel_.load(std::memory_order_relaxed);
  const int channels = inputChannels_.load(std::memory_order_relaxed);

  if (playing && channel >= 0 && channel < channels && length > 0 &&
      static_cast<std::size_t>(length) <= kMaxBlock) {
    if (const ReaSample *input = registration->GetBuffer(false, channel)) {
      const auto count = static_cast<std::size_t>(length);
      for (std::size_t i = 0; i < count; ++i)
        scratch_[i] = static_cast<float>(input[i]);

      header.hasSamples = samples_->push(std::span<const float>(scratch_.data(), count));
    }
  }

  (void)headers_.push(header);
}

void AudioInput::work() {
  while (!stop_.load()) {
    while (const auto header = headers_.pop())
      feed(*header);

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void AudioInput::feed(const BlockHeader &header) {
  const bool gap = header.sequence != expectedSequence_;
  expectedSequence_ = header.sequence + 1;

  if (!header.hasSamples) {
    streaming_ = false;
    previousPosition_.reset();
    return;
  }

  blockSamples_.resize(static_cast<std::size_t>(header.length));
  (void)samples_->pop(std::span<float>(blockSamples_));

  const double silenceDb = silenceDb_.load(std::memory_order_relaxed);
  const double energyRatio = energyRatio_.load(std::memory_order_relaxed);

  if (!detector_ || header.sampleRate != detectorRate_) {
    detector_.emplace(onset::Settings{
        .sampleRate = header.sampleRate, .silenceDb = silenceDb, .energyRatio = energyRatio});
    detectorRate_ = header.sampleRate;
    appliedSilenceDb_ = silenceDb;
    appliedEnergyRatio_ = energyRatio;
    streaming_ = false;
  }

  if (silenceDb != appliedSilenceDb_) {
    detector_->setSilenceDb(silenceDb);
    appliedSilenceDb_ = silenceDb;
  }

  if (energyRatio != appliedEnergyRatio_) {
    detector_->setEnergyRatio(energyRatio);
    appliedEnergyRatio_ = energyRatio;
  }

  // Новый запуск транспорта или разрыв потока: сэмплы до и после несмежны.
  if (!streaming_ || gap) {
    if (!streaming_)
      ++run_;
    detector_->reset();
    history_.clear();
    previousPosition_.reset();
    streaming_ = true;
  }

  const bool moving = previousPosition_ && header.position != *previousPosition_;
  previousPosition_ = header.position;
  history_.push_back(
      {.streamStart = detector_->position(), .header = header, .moving = moving});

  found_.clear();
  detector_->process(blockSamples_, found_);

  for (const onset::Onset &found : found_) {
    // Блок начала атаки — последний из начавшихся не позже неё.
    auto newestFirst = std::views::reverse(history_);
    const auto block =
        std::ranges::find_if(newestFirst, [&found](const StreamBlock &candidate) {
          return static_cast<double>(candidate.streamStart) <= found.position;
        });
    if (block == newestFirst.end())
      continue;
    if (!block->moving) {
      journal("onset dropped: position {:.6f} is not moving", block->header.position);
      continue;
    }

    const HookOnset hit{.blockPosition = block->header.position,
                        .offset = found.position - static_cast<double>(block->streamStart),
                        .sampleRate = block->header.sampleRate,
                        .levelDb = found.levelDb,
                        .monotonic = block->header.monotonic,
                        .run = run_};

    journal("onset run={} P={:.6f} i={:.3f} level={:.1f} dB", hit.run, hit.blockPosition,
            hit.offset, hit.levelDb);
    (void)onsets_.push(hit);
  }

  // Блоки старше kHistorySeconds звука больше не понадобятся.
  const auto keep = static_cast<std::uint64_t>(kHistorySeconds * header.sampleRate);
  const std::uint64_t now = detector_->position();
  const auto stale = std::ranges::find_if(history_, [now, keep](const StreamBlock &candidate) {
    return candidate.streamStart + keep >= now;
  });
  history_.erase(history_.begin(), stale);
}

} // namespace training::reaper
