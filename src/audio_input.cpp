#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_Audio_RegHardwareHook
#define REAPERAPI_WANT_GetPlayPosition2Ex
#define REAPERAPI_WANT_GetPlayStateEx

#include "audio_input.hpp"

#include <reaper_plugin_functions.h>

#include "clock.hpp"
#include "journal.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <span>

namespace training::reaper {

AudioInput::AudioInput(const InputSettings &input)
    : samples_(std::make_unique<SpscRing<float, (1U << 18)>>()),
      calibrationSamples_(std::make_unique<SpscRing<float, (1U << 18)>>()), input_(input) {
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

void AudioInput::setReceiver(core::input::SoundReceiver *receiver, std::uint64_t run) {
  const std::scoped_lock lock(receiverMutex_);
  receiver_ = receiver;
  receiverRun_ = run;
}

std::uint64_t AudioInput::onStart(RunStart start, std::uint64_t countInSamples) {
  const auto nanoseconds = [](core::Moment time) {
    return static_cast<std::int64_t>((time - core::Moment()) / core::Interval::kNanosecond);
  };
  startSequence_.fetch_add(1);
  startPosition_.store(nanoseconds(start.start.position));
  startRate_.store(start.start.rate);
  startTime_.store(nanoseconds(start.time));
  startCountIn_.store(countInSamples);
  startRun_.store(++runs_);
  startSequence_.fetch_add(1);
  runPending_.store(true);
  return runs_;
}

RunStart AudioInput::pendingStart(std::uint64_t &countInSamples, std::uint64_t &run) const {
  const auto moment = [](std::int64_t nanoseconds) {
    return core::Moment() + static_cast<double>(nanoseconds) * core::Interval::kNanosecond;
  };
  for (;;) {
    const std::uint64_t before = startSequence_.load();
    if (before % 2 != 0)
      continue;
    const RunStart start{
        .start = {.position = moment(startPosition_.load()), .rate = startRate_.load()},
        .time = moment(startTime_.load())};
    countInSamples = startCountIn_.load();
    run = startRun_.load();
    if (startSequence_.load() == before)
      return start;
  }
}

bool AudioInput::alignRun(bool playing, core::Moment position, int length, double sampleRate) {
  // Начало отсчёта — первый блок после отметки начала, который REAPER
  // обрабатывает уже в воспроизведении или записи без паузы: его время — T0
  // минус длина отсчёта.
  if (playing && runPending_.exchange(false)) {
    std::uint64_t countIn = 0;
    run_ = pendingStart(countIn, runNumber_);
    countInLeft_ = countIn;
    held_.reset();
    rateStart_ =
        run_.time - static_cast<double>(countIn) / sampleRate * core::Interval::kSecond;
    rateSamples_ = 0;
  }
  if (!countInLeft_)
    return false;

  // Позиция пошла дальше P0: воспроизведение началось с прошлого блока, и он
  // звучит с T0. Если REAPER встал раньше старта, удержанный блок уходит со
  // временем T0, и время идёт дальше от него.
  const bool moved = playing && position > run_.start.position;
  if (moved || !playing) {
    std::optional<int> before;
    if (held_)
      before = held_->length;
    else if (moved)
      before = lastLength_;
    if (held_)
      pushHeld(*held_);
    if (before) {
      rateStart_ = run_.time;
      rateSamples_ = static_cast<std::uint64_t>(std::max(*before, 0));
    }
    countInLeft_.reset();
    held_.reset();
    return false;
  }

  // Отсчёт кончился, а позиция ещё на P0: блок ждёт старта со временем T0.
  if (*countInLeft_ == 0) {
    rateStart_ = run_.time;
    rateSamples_ = 0;
    return true;
  }
  *countInLeft_ -= std::min(*countInLeft_, static_cast<std::uint64_t>(std::max(length, 0)));
  return false;
}

void AudioInput::pushHeld(BlockHeader header) {
  if (headers_.freeSpace() > 0 && header.hasSamples)
    header.hasSamples = samples_->push(
        std::span<const float>(heldSamples_.data(), static_cast<std::size_t>(header.length)));
  (void)headers_.push(header);
}

void AudioInput::onBlock(int length, double sampleRate, audio_hook_register_t *registration) {
  // Подача — один раз на блок: блок идёт туда, куда шёл звук, когда его взяли.
  const bool calibration = calibration_.load(std::memory_order_relaxed);
  sampleRate_.store(sampleRate, std::memory_order_relaxed);

  // Время ядра: отрезок с одной частотой начинается на конце прошлого блока,
  // и время блока считается от его начала по сэмплам — стыки блоков
  // совпадают точно.
  const auto timeOf = [this, sampleRate](std::uint64_t samples) {
    return rateStart_ + static_cast<double>(samples) / sampleRate * core::Interval::kSecond;
  };
  if (sampleRate != hookRate_) {
    if (hookRate_ > 0.0)
      rateStart_ =
          rateStart_ + static_cast<double>(rateSamples_) / hookRate_ * core::Interval::kSecond;
    rateSamples_ = 0;
    hookRate_ = sampleRate;
  }

  const int state = GetPlayStateEx(nullptr);
  const bool playing = (state & 1) != 0 && (state & 2) == 0;
  const core::Moment position = projectTime(GetPlayPosition2Ex(nullptr));
  const bool hold = alignRun(playing, position, length, sampleRate);

  const core::Moment time = timeOf(rateSamples_);
  rateSamples_ += static_cast<std::uint64_t>(std::max(length, 0));
  lastLength_ = length;
  publishClock({.end = timeOf(rateSamples_), .blockLength = length, .sampleRate = sampleRate});

  const core::Interval compensation =
      static_cast<double>(compensation_.load(std::memory_order_relaxed)) *
      core::Interval::kNanosecond;
  BlockHeader header{.time = time,
                     .start = time - compensation,
                     .position = position,
                     .run = runNumber_,
                     .sampleRate = sampleRate,
                     .length = length,
                     .calibration = calibration,
                     .hasSamples = false};

  // Места для заголовка нет — пропадает весь блок, и ядро отсчитает его как
  // время без звука.
  if (!hold && headers_.freeSpace() == 0) {
    (void)headers_.push(header);
    return;
  }

  // Пока главный поток пишет настройки, блок берёт вход прошлого блока: вид
  // входа и канал — из одной и той же правки.
  if (const std::unique_lock lock(settingsMutex_, std::try_to_lock); lock.owns_lock()) {
    choiceInput_ = input_.input();
    choiceChannel_ = input_.channel();
  }
  const int channel = choiceInput_ == Input::Audio ? choiceChannel_ : -1;

  // Числа входов хук не знает: `input_nch` REAPER не заполняет, а для канала,
  // которого нет у карты, GetBuffer отдаёт пустой указатель.
  const ReaSample *input = channel >= 0 ? registration->GetBuffer(false, channel) : nullptr;
  if (channel >= 0)
    checkedChannel_.store(channel * 2 + (input ? 1 : 0), std::memory_order_relaxed);

  if (input && length > 0 && static_cast<std::size_t>(length) <= kMaxBlock) {
    const auto count = static_cast<std::size_t>(length);
    float *to = hold ? heldSamples_.data() : scratch_.data();
    for (std::size_t i = 0; i < count; ++i)
      to[i] = static_cast<float>(input[i]);

    header.hasSamples = hold || samples_->push(std::span<const float>(scratch_.data(), count));
  }

  // Удержанный блок заменяет прежний удержанный: отправится только последний
  // блок на P0 — блок старта.
  if (hold) {
    held_ = header;
    return;
  }
  (void)headers_.push(header);
}

void AudioInput::publishClock(const HostClock &clock) {
  clockSequence_.fetch_add(1);
  clockEnd_.store(
      static_cast<std::int64_t>((clock.end - core::Moment()) / core::Interval::kNanosecond));
  clockLength_.store(clock.blockLength);
  clockSequence_.fetch_add(1);
}

std::optional<HostClock> AudioInput::clock() const {
  for (;;) {
    const std::uint64_t before = clockSequence_.load();
    if (before % 2 != 0)
      continue;
    const std::int64_t end = clockEnd_.load();
    const int length = clockLength_.load();
    if (clockSequence_.load() != before)
      continue;
    if (before == 0)
      return std::nullopt;

    return HostClock{.end = core::Moment() +
                            static_cast<double>(end) * core::Interval::kNanosecond,
                     .blockLength = length,
                     .sampleRate = sampleRate_.load(std::memory_order_relaxed)};
  }
}

void AudioInput::work() {
  while (!stop_.load()) {
    while (const auto header = headers_.pop())
      feed(*header);

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void AudioInput::feed(const BlockHeader &header) {
  (void)places_.push(
      BlockPlace{.time = header.time, .position = header.position, .run = header.run});

  if (header.hasSamples && header.calibration) {
    forwardCalibration(header);
    return;
  }

  // Блок без звука канала приёмник получает пустым: время идёт.
  std::span<const float> samples;
  if (header.hasSamples) {
    blockSamples_.resize(static_cast<std::size_t>(header.length));
    (void)samples_->pop(std::span<float>(blockSamples_));
    samples = blockSamples_;
  }

  // Приёмник принимает блоки только своей частоты
  // (`core::input::SoundReceiver.sampleRate`) и своего запуска: блок другой
  // или недопустимой частоты и блок со временем прошлой связки ему не
  // отдаётся.
  const std::optional<core::SampleRate> rate = core::SampleRate::create(header.sampleRate);
  if (!rate)
    return;
  const std::scoped_lock lock(receiverMutex_);
  if (!receiver_ || header.run != receiverRun_ || receiver_->sampleRate() != *rate)
    return;
  receiver_->receive({.samples = samples, .start = header.start});
}

void AudioInput::forwardCalibration(const BlockHeader &header) {
  const auto length = static_cast<std::size_t>(header.length);
  blockSamples_.resize(length);
  (void)samples_->pop(std::span<float>(blockSamples_));

  // Места нет — кусок пропадает: калибровка увидит разрыв по времени.
  if (calibrationChunks_.freeSpace() == 0 || calibrationSamples_->freeSpace() < length)
    return;

  (void)calibrationSamples_->push(std::span<const float>(blockSamples_));
  (void)calibrationChunks_.push(
      {.sampleRate = header.sampleRate, .length = header.length, .start = header.start});
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
