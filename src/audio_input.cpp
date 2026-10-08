#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_Audio_RegHardwareHook
#define REAPERAPI_WANT_GetPlayPosition2Ex

#include "audio_input.hpp"

#include <reaper_plugin_functions.h>

#include "clock.hpp"
#include "journal.hpp"

#include <algorithm>
#include <chrono>
#include <span>

namespace training::reaper {

AudioInput::AudioInput(trainer::OnsetSearch &search)
    : samples_(std::make_unique<concurrency::SpscRing<float, (1U << 18)>>()),
      calibrationSamples_(std::make_unique<concurrency::SpscRing<float, (1U << 18)>>()),
      search_(search) {
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
  // Звуковой поток: только копирование — ни блокировок, ни выделения памяти.
  if (isPost || !registration || !registration->userdata1)
    return;

  static_cast<AudioInput *>(registration->userdata1)
      ->onBlock(length, sampleRate, registration);
}

void AudioInput::onBlock(int length, double sampleRate, audio_hook_register_t *registration) {
  // Режим — один раз на блок: блок получает режим, с которым взят.
  const trainer::AudioMode mode = mode_.load(std::memory_order_relaxed);
  sampleRate_.store(sampleRate, std::memory_order_relaxed);

  BlockHeader header{.firstSample = nextSample_,
                     .position = GetPlayPosition2Ex(nullptr),
                     .sampleRate = sampleRate,
                     .monotonic = momentNow(),
                     .length = length,
                     .mode = mode};
  nextSample_ += static_cast<std::uint64_t>(std::max(length, 0));

  // Места для заголовка нет — пропадает весь блок, а скачок номера сэмпла у
  // следующего скажет рабочему потоку, сколько звука пропало.
  if (headers_.freeSpace() == 0) {
    (void)headers_.push(header);
    return;
  }

  const int channel = channel_.load(std::memory_order_relaxed);

  // Числа входов хук не знает: `input_nch` REAPER не заполняет, а для канала,
  // которого нет у карты, GetBuffer отдаёт пустой указатель.
  const ReaSample *input = channel >= 0 ? registration->GetBuffer(false, channel) : nullptr;
  if (channel >= 0)
    checkedChannel_.store(channel * 2 + (input ? 1 : 0), std::memory_order_relaxed);

  if (mode != trainer::AudioMode::Off && input && length > 0 &&
      static_cast<std::size_t>(length) <= kMaxBlock) {
    const auto count = static_cast<std::size_t>(length);
    for (std::size_t i = 0; i < count; ++i)
      scratch_[i] = static_cast<float>(input[i]);

    header.hasSamples = samples_->push(std::span<const float>(scratch_.data(), count));
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
  const bool gap = header.firstSample != expectedSample_;
  expectedSample_ = header.firstSample + static_cast<std::uint64_t>(header.length);

  if (header.hasSamples && header.mode == trainer::AudioMode::Calibration) {
    forwardCalibration(header, gap || !calibrationStreaming_);
    calibrationStreaming_ = true;
    return;
  }
  calibrationStreaming_ = false;

  if (!header.hasSamples)
    return;

  blockSamples_.resize(static_cast<std::size_t>(header.length));
  (void)samples_->pop(std::span<float>(blockSamples_));

  found_.clear();
  search_.process(trainer::AudioBlock{.firstSample = header.firstSample,
                                      .position = header.position,
                                      .sampleRate = header.sampleRate,
                                      .monotonic = header.monotonic},
                  blockSamples_, silenceDb_.load(std::memory_order_relaxed), found_);
  for (const trainer::AudioOnset &hit : found_)
    (void)onsets_.push(hit);
}

void AudioInput::forwardCalibration(const BlockHeader &header, bool gap) {
  const auto length = static_cast<std::size_t>(header.length);
  blockSamples_.resize(length);
  (void)samples_->pop(std::span<float>(blockSamples_));

  // Места нет — кусок пропадает, и следующий скажет главному потоку о
  // разрыве.
  if (calibrationChunks_.freeSpace() == 0 || calibrationSamples_->freeSpace() < length) {
    calibrationLost_ = true;
    return;
  }

  (void)calibrationSamples_->push(std::span<const float>(blockSamples_));
  (void)calibrationChunks_.push({.sampleRate = header.sampleRate,
                                 .length = header.length,
                                 .gap = gap || calibrationLost_});
  calibrationLost_ = false;
}

std::optional<CalibrationChunk> AudioInput::popCalibration(std::vector<float> &samples) {
  const auto chunk = calibrationChunks_.pop();
  if (!chunk)
    return std::nullopt;

  samples.resize(static_cast<std::size_t>(chunk->length));
  (void)calibrationSamples_->pop(std::span<float>(samples));
  return chunk;
}

} // namespace training::reaper
