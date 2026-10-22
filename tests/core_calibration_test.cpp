#include <doctest/doctest.h>

#include "synth.hpp"
#include "wav.hpp"

#include "training/core/calibration/calibrator.hpp"
#include "training/core/calibration/notes_step.hpp"
#include "training/core/calibration/result_step.hpp"
#include "training/core/calibration/silence_step.hpp"
#include "training/core/detector/detector.hpp"
#include "training/core/errors.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using training::core::CannotStartUnit;
using training::core::Interval;
using training::core::InvalidStep;
using training::core::Moment;
using training::core::SampleRate;
using training::core::calibration::Calibrator;
using training::core::calibration::kFloorDb;
using training::core::calibration::NotesStep;
using training::core::calibration::ResultStep;
using training::core::calibration::SilenceStep;
using training::core::calibration::Step;
using training::core::detector::Detector;
using training::core::detector::Settings;
using training::test::evenPlucks;
using training::test::Pluck;
using training::test::readWav;
using training::test::render;
using training::test::SynthSettings;

namespace {

constexpr double kSampleRate = 48000.0;
const SampleRate kRate = *SampleRate::create(kSampleRate);

/// Начало потока, которое шаг тишины не слушает, и сам шаг, в сэмплах.
constexpr std::size_t kSettle = 4800;
constexpr std::size_t kSilence = std::size_t{30} * 4800;

/// Прогрев фильтров шага нот после его запуска, в сэмплах.
constexpr std::size_t kNotesSettle = 4800;

/// Сколько нот просит сыграть шаг нот по умолчанию.
constexpr std::size_t kNotes = 8;

std::size_t samplesOf(double seconds) {
  return static_cast<std::size_t>(std::llround(seconds * kSampleRate));
}

/// Белый шум с пиком `db` dBFS длиной `seconds` с.
std::vector<float> noise(double seconds, double db, std::uint32_t seed = 1) {
  return render({}, samplesOf(seconds), SynthSettings{.noiseDb = db, .seed = seed});
}

std::vector<float> joined(std::initializer_list<std::vector<float>> parts) {
  std::vector<float> all;
  for (const std::vector<float> &part : parts)
    all.insert(all.end(), part.begin(), part.end());
  return all;
}

/// Калибратор частоты `kSampleRate` со своим детектором, уже запущенный.
/// Блоки идут вплотную друг за другом во времени ядра.
struct Bench {
  std::unique_ptr<Calibrator> calibrator = std::move(*Calibrator::create(kRate));
  /// Время первого сэмпла следующего блока.
  Moment stream = Moment() + 5 * Interval::kSecond;

  Bench() { REQUIRE(calibrator->start()); }
  Bench(const Bench &) = delete;
  Bench &operator=(const Bench &) = delete;
  Bench(Bench &&) = delete;
  Bench &operator=(Bench &&) = delete;
  ~Bench() = default;

  /// Подаёт блок `samples` со временем `stream`; следующий идёт вплотную.
  void block(std::span<const float> samples) {
    calibrator->receive({.samples = samples, .start = stream});
    stream += static_cast<double>(samples.size()) / kSampleRate * Interval::kSecond;
  }

  /// Подаёт `signal` блоками, длины которых даёт `next`.
  void feed(std::span<const float> signal, const std::function<std::size_t()> &next) {
    for (std::size_t from = 0; from < signal.size();) {
      const std::size_t length = std::min(next(), signal.size() - from);
      block(signal.subspan(from, length));
      from += length;
    }
  }

  void feed(std::span<const float> signal) {
    feed(signal, [] { return std::size_t{512}; });
  }

  Calibrator::State state() const { return calibrator->result(); }
};

/// Сэмпл, после которого калибратор перешёл к шагу `step`, при подаче по
/// одному сэмплу; пусто — не перешёл.
std::optional<std::size_t> stepAt(std::span<const float> signal, Step step) {
  Bench bench;
  for (std::size_t i = 0; i < signal.size(); ++i) {
    bench.block(signal.subspan(i, 1));
    if (bench.state().step == step)
      return i + 1;
  }
  return std::nullopt;
}

/// Глушёные ноты DI-гитары между синтетическими тишинами: перед ними — шаг
/// тишины с шумом около фона записи, после — 2 с тишины.
std::vector<float> mutedQuarters() {
  const auto recording = readWav(std::filesystem::path(TRAINING_TEST_DATA_DIR) / "onset" /
                                 "di_muted_quarters.wav");
  REQUIRE(recording.sampleRate == kSampleRate);
  return joined({noise(3.3, -96.0, 2), recording.samples, noise(2.0, -96.0, 3)});
}

/// Щелчки медиатора в `di_muted_quarters.wav` (README): огибающая после
/// ВЧ-фильтра −60…−53 dBFS в первые 5 мс от удара.
constexpr double kQuietestClickDb = -60.0;
constexpr double kLoudestClickDb = -53.0;

/// `count` щипков через 0.5 с после шага тишины; каждый глушит прошлый,
/// последний глушится через 0.4 с — как глушёные ноты.
std::vector<float> plucked(std::size_t count, double amplitude, double drive = 0.0) {
  const std::size_t first = kSettle + kSilence + samplesOf(0.5);
  std::vector<Pluck> plucks =
      evenPlucks(first, kSampleRate / 2.0, count, {110.0, 147.0, 196.0}, amplitude);
  plucks.push_back(Pluck{.start = plucks.back().start + samplesOf(0.4), .amplitude = 0.0});
  return render(plucks, first + samplesOf(0.5 * static_cast<double>(count) + 20.0),
                SynthSettings{.noiseDb = -100.0, .drive = drive});
}

/// Восемь глушёных щипков, как `plucked(8, 0.3)`, и ещё по удару, глушёному
/// через 50 мс, на сэмплах `strikes`.
std::vector<float> pluckedThen(const std::vector<std::size_t> &strikes) {
  const std::size_t first = kSettle + kSilence + samplesOf(0.5);
  std::vector<Pluck> plucks =
      evenPlucks(first, kSampleRate / 2.0, 8, {110.0, 147.0, 196.0}, 0.3);
  plucks.push_back(Pluck{.start = plucks.back().start + samplesOf(0.4), .amplitude = 0.0});
  for (const std::size_t strike : strikes) {
    plucks.push_back(Pluck{.start = strike, .amplitude = 0.3});
    plucks.push_back(Pluck{.start = strike + samplesOf(0.05), .amplitude = 0.0});
  }
  return render(plucks, first + samplesOf(4.0 + 20.0), SynthSettings{.noiseDb = -100.0});
}

} // namespace

TEST_CASE("Калибратор: следующий шаг начинает со следующего блока") {
  // Шаг тишины кончается посреди блока: остаток блока в шаг нот не идёт, и
  // шаг нот начинает со следующего блока.
  const std::vector<float> signal = noise(4.0, -100.0);
  Bench bench;
  bench.block(std::span(signal).first(kSettle + kSilence + 1000));
  REQUIRE(bench.state().step == Step::Notes);
  CHECK(bench.state().sinceLastNote == Interval());

  bench.block(std::span(signal).subspan(kSettle + kSilence + 1000, kNotesSettle + 480));
  CHECK(bench.state().sinceLastNote / Interval::kSecond == doctest::Approx(0.01));
}

TEST_CASE("Калибратор: запуск только из Off, остановка — в Off") {
  const std::unique_ptr<Calibrator> calibrator = std::move(*Calibrator::create(kRate));
  CHECK(calibrator->sampleRate() == kRate);
  CHECK(calibrator->result().step == Step::Off);

  REQUIRE(calibrator->start());
  CHECK(calibrator->result().step == Step::Silence);
  CHECK(calibrator->start() == std::unexpected(InvalidStep{}));

  calibrator->stop();
  CHECK(calibrator->result().step == Step::Off);
  CHECK(calibrator->start());
}

TEST_CASE("Калибратор: на частоте, где детектор не создаётся, — CannotStartUnit") {
  // ВЧ-фильтр детектора на 1 кГц требует частоту выше 2 кГц.
  CHECK(Calibrator::create(*SampleRate::create(1600.0)) == std::unexpected(CannotStartUnit{}));
}

TEST_CASE("Калибратор: выключенный блоки не слушает") {
  const std::unique_ptr<Calibrator> calibrator = std::move(*Calibrator::create(kRate));
  const std::vector<float> signal = noise(4.0, -100.0);
  calibrator->receive({.samples = signal, .start = Moment()});
  CHECK(calibrator->result().step == Step::Off);
}

TEST_CASE("Калибратор: шаг тишины меряет шум и кончается через 3 с") {
  const std::vector<float> signal = noise(4.0, -100.0);

  CHECK(stepAt(signal, Step::Notes) == kSettle + kSilence);

  Bench bench;
  CHECK(bench.state().silenceLeft / Interval::kSecond == doctest::Approx(3.0));
  bench.feed(std::span(signal).first(kSettle + samplesOf(1.5)));
  CHECK(bench.state().step == Step::Silence);
  CHECK(bench.state().silenceLeft / Interval::kSecond == doctest::Approx(1.5));

  bench.feed(std::span(signal).subspan(kSettle + samplesOf(1.5)));
  CHECK(bench.state().step == Step::Notes);
  CHECK(bench.state().noiseDb > -106.0);
  CHECK(bench.state().noiseDb < -94.0);
  // Звука не было: время с последнего звука идёт от начала слушания.
  CHECK(bench.state().sinceSound / Interval::kSecond == doctest::Approx(3.0));
}

TEST_CASE("Калибратор: звук в тишине начинает шаг заново") {
  // Щипок на второй секунде шага, через 50 мс струну глушат.
  const std::size_t pluck = kSettle + samplesOf(1.5);
  const std::vector<float> signal =
      render({Pluck{.start = pluck, .amplitude = 0.3},
              Pluck{.start = pluck + samplesOf(0.05), .amplitude = 0.0}},
             samplesOf(8.0), SynthSettings{.noiseDb = -100.0});

  Bench bench;
  bench.feed(std::span(signal).first(kSettle + samplesOf(3.0)));
  CHECK(bench.state().step == Step::Silence);
  CHECK(bench.state().sinceSound / Interval::kSecond < 1.5);

  bench.feed(std::span(signal).subspan(kSettle + samplesOf(3.0)));
  CHECK(bench.state().step == Step::Notes);
  CHECK(bench.state().noiseDb < -94.0);
}

TEST_CASE("Калибратор: цифровая тишина — наименьший уровень") {
  const std::vector<float> signal(kSettle + kSilence + 512, 0.0F);
  Bench bench;
  bench.feed(signal);
  CHECK(bench.state().step == Step::Notes);
  CHECK(bench.state().noiseDb == kFloorDb);
  CHECK(bench.state().levelDb == kFloorDb);
}

TEST_CASE("Калибратор: пропущенный звук считается тишиной") {
  const std::vector<float> quiet = noise(2.0, -100.0);
  Bench bench;
  bench.feed(quiet);
  const double before = bench.state().silenceLeft / Interval::kSecond;
  REQUIRE(before < 1.2);

  // Звук пропал на 0.5 с: следующий блок начинается позже конца прошлого, а
  // шаг тишины идёт дальше.
  bench.stream += 500 * Interval::kMillisecond;
  const std::vector<float> more = noise(0.1, -100.0, 4);
  bench.block(std::span(more).first(480));
  CHECK(bench.state().silenceLeft / Interval::kSecond == doctest::Approx(before - 0.51));
  // Пропуск не звук: время с последнего звука идёт от начала слушания.
  CHECK(bench.state().sinceSound / Interval::kSecond ==
        doctest::Approx(3.0 - bench.state().silenceLeft / Interval::kSecond));
}

TEST_CASE("Калибратор: начало блока в уже полученном звуке отбрасывается") {
  const std::vector<float> quiet = noise(2.0, -100.0);
  Bench bench;
  bench.feed(quiet);
  const double before = bench.state().silenceLeft / Interval::kSecond;

  // Блок начинается на 240 сэмплов раньше конца полученного звука: в шаг идут
  // только его последние 240 сэмплов.
  bench.stream -= 240.0 / kSampleRate * Interval::kSecond;
  const std::vector<float> more = noise(0.01, -100.0, 4);
  bench.block(std::span(more).first(480));
  CHECK(bench.state().silenceLeft / Interval::kSecond == doctest::Approx(before - 0.005));

  // Блок целиком в полученном звуке ничего не меняет.
  bench.stream -= 480.0 / kSampleRate * Interval::kSecond;
  bench.block(std::span(more).first(240));
  CHECK(bench.state().silenceLeft / Interval::kSecond == doctest::Approx(before - 0.005));
}

TEST_CASE("Калибратор: глушёные ноты DI-гитары") {
  const std::vector<float> signal = mutedQuarters();
  Bench bench;
  bench.feed(signal);

  const Calibrator::State state = bench.state();
  REQUIRE(state.step == Step::Done);
  CHECK(state.verdict == NotesStep::Verdict::Good);
  REQUIRE(state.attacksDb.size() == kNotes);
  for (const double attack : state.attacksDb) {
    CHECK(attack >= kQuietestClickDb);
    CHECK(attack <= kLoudestClickDb);
  }
  const double softest = std::ranges::min(state.attacksDb);
  CHECK(softest < -59.0);
  CHECK(state.peakDb == doctest::Approx(-22.1).epsilon(0.02));
  CHECK(state.thresholdDb == std::lround((state.noiseDb + softest) / 2.0));
}

TEST_CASE("Калибратор: без нот — обрыв через 15 с после прогрева шага нот") {
  const std::vector<float> signal = noise(3.1 + 0.1 + 15.5, -100.0);
  const auto doneAt = stepAt(signal, Step::Done);
  REQUIRE(doneAt);
  const double afterNotes =
      static_cast<double>(*doneAt - (kSettle + kSilence + kNotesSettle)) / kSampleRate;
  CHECK(afterNotes >= 15.0);
  CHECK(afterNotes < 15.006);

  Bench bench;
  bench.feed(signal);
  const Calibrator::State state = bench.state();
  REQUIRE(state.step == Step::Done);
  CHECK(state.verdict == NotesStep::Verdict::NotEnoughNotes);
  CHECK(state.attacksDb.empty());
  CHECK(state.thresholdDb == kFloorDb);
}

TEST_CASE("Калибратор: три ноты и тишина — обрыв с тремя") {
  Bench bench;
  bench.feed(plucked(3, 0.3));
  REQUIRE(bench.state().step == Step::Done);
  CHECK(bench.state().verdict == NotesStep::Verdict::NotEnoughNotes);
  CHECK(bench.state().attacksDb.size() == 3);
}

TEST_CASE("Калибратор: восемь щипков на тихом фоне — удача") {
  Bench bench;
  bench.feed(plucked(8, 0.3));
  REQUIRE(bench.state().step == Step::Done);
  CHECK(bench.state().verdict == NotesStep::Verdict::Good);
  CHECK(bench.state().attacksDb.size() == kNotes);
}

TEST_CASE("Калибратор: перегруз входа") {
  Bench bench;
  bench.feed(plucked(8, 1.0, 6.0));
  REQUIRE(bench.state().step == Step::Done);
  CHECK(bench.state().verdict == NotesStep::Verdict::Clipping);
  CHECK(bench.state().peakDb >= -1.0);
}

TEST_CASE("Шаг нот: шум ближе 12 дБ к самой тихой атаке — шум слишком близко") {
  const std::vector<float> signal = plucked(8, 0.3);
  Bench bench;
  bench.feed(signal);
  REQUIRE(bench.state().verdict == NotesStep::Verdict::Good);
  const double softest = std::ranges::min(bench.state().attacksDb);

  // Тот же звук нот, но шум назван на 11 дБ тише самой тихой атаки.
  const Detector detector = *Detector::create(Settings{.sampleRate = kRate});
  NotesStep step(detector, kRate);
  REQUIRE(step.start(softest - 11.0));
  const std::span<const float> notes = std::span(signal).subspan(kSettle + kSilence);
  for (std::size_t from = 0;
       from < notes.size() && step.result().step != NotesStep::Step::Done; from += 512)
    step.receive(
        {.samples = notes.subspan(from, std::min<std::size_t>(512, notes.size() - from)),
         .start = Moment()});
  const NotesStep::State state = step.result();
  REQUIRE(state.step == NotesStep::Step::Done);
  CHECK(state.attacksDb.size() == kNotes);
  CHECK(state.verdict == NotesStep::Verdict::NoiseTooClose);
  CHECK(state.thresholdDb ==
        std::lround((softest - 11.0 + std::ranges::min(state.attacksDb)) / 2.0));
}

TEST_CASE("Калибратор: удар останавливает калибратор через 5 с после итога, но не раньше") {
  const auto doneAt = stepAt(pluckedThen({}), Step::Done);
  REQUIRE(doneAt);

  const std::size_t early = *doneAt + samplesOf(2.0);
  const std::size_t late = *doneAt + samplesOf(6.0);
  const std::vector<float> signal = pluckedThen({early, late});

  // Удары ничего не меняют в ходе до итога.
  REQUIRE(stepAt(signal, Step::Done) == doneAt);

  Bench bench;
  bench.feed(std::span(signal).first(*doneAt + samplesOf(5.9)));
  REQUIRE(bench.state().step == Step::Done);

  bench.feed(std::span(signal).subspan(*doneAt + samplesOf(5.9)));
  CHECK(bench.state().step == Step::Off);
  CHECK(bench.state().verdict == NotesStep::Verdict::Good);
}

TEST_CASE("Калибратор: тишина после итога калибратор не останавливает") {
  Bench bench;
  bench.feed(pluckedThen({}));
  CHECK(bench.state().step == Step::Done);
}

TEST_CASE("Шаг нот: ищет ноты своим детектором с порогом над шумом") {
  // Детектор с порогом 0 dBFS не нашёл бы ни одной ноты: шаг нот берёт у него
  // только настройки и ищет ноты своим детектором с порогом на 6 дБ выше шума.
  const std::vector<float> signal = pluckedThen({});
  Bench bench;
  bench.feed(signal, [] { return std::size_t{1000}; });
  REQUIRE(bench.state().step == Step::Done);
  REQUIRE(bench.state().verdict == NotesStep::Verdict::Good);

  const Detector deaf = *Detector::create(Settings{.sampleRate = kRate, .silenceDb = 0.0});
  NotesStep step(deaf, kRate);
  REQUIRE(step.start(bench.state().noiseDb));
  const std::span<const float> notes = std::span(signal).subspan(kSettle + kSilence);
  for (std::size_t from = 0;
       from < notes.size() && step.result().step != NotesStep::Step::Done; from += 1000)
    step.receive(
        {.samples = notes.subspan(from, std::min<std::size_t>(1000, notes.size() - from)),
         .start = Moment()});
  const NotesStep::State state = step.result();
  REQUIRE(state.step == NotesStep::Step::Done);
  CHECK(state.attacksDb.size() == kNotes);
  CHECK(state.verdict == NotesStep::Verdict::Good);
}

TEST_CASE("Шаги калибровки: запуск только из Off") {
  Detector detector = *Detector::create(Settings{.sampleRate = kRate});

  SilenceStep silence(detector, kRate);
  CHECK(silence.sampleRate() == kRate);
  REQUIRE(silence.start());
  CHECK(silence.result().step == SilenceStep::Step::Loading);
  CHECK(silence.start() == std::unexpected(InvalidStep{}));
  silence.stop();
  CHECK(silence.result().step == SilenceStep::Step::Off);

  NotesStep notes(detector, kRate);
  CHECK(notes.sampleRate() == kRate);
  REQUIRE(notes.start(-90.0));
  CHECK(notes.noiseDb() == -90.0);
  CHECK(notes.start(-90.0) == std::unexpected(InvalidStep{}));
  notes.stop();
  CHECK(notes.result().step == NotesStep::Step::Off);

  ResultStep result(detector, kRate);
  CHECK(result.sampleRate() == kRate);
  REQUIRE(result.start(-60.0));
  CHECK(result.step() == ResultStep::Step::Loading);
  CHECK(result.thresholdDb() == -60.0);
  CHECK(result.start(-60.0) == std::unexpected(InvalidStep{}));
  result.stop();
  CHECK(result.step() == ResultStep::Step::Off);
}

TEST_CASE("Шаг итога: ждёт waitTime и слушает звук выше порога") {
  Detector detector = *Detector::create(Settings{.sampleRate = kRate});
  ResultStep step(detector, kRate, Interval::kSecond);
  REQUIRE(step.start(-80.0));

  const std::vector<float> strike =
      render({Pluck{.start = samplesOf(0.1), .amplitude = 0.3},
              Pluck{.start = samplesOf(0.15), .amplitude = 0.0}},
             samplesOf(0.6), SynthSettings{.noiseDb = -100.0, .seed = 5});
  // Удар до конца ожидания не слышен.
  step.receive({.samples = strike, .start = Moment()});
  CHECK(step.step() == ResultStep::Step::Loading);
  const std::vector<float> quiet = noise(0.5, -100.0);
  step.receive({.samples = quiet, .start = Moment()});
  CHECK(step.step() == ResultStep::Step::Working);
  step.receive({.samples = strike, .start = Moment()});
  CHECK(step.step() == ResultStep::Step::Done);
}

TEST_CASE("Шаги калибровки: пропущенный звук считается тишиной") {
  Detector detector = *Detector::create(Settings{.sampleRate = kRate});
  const std::vector<float> quiet = noise(2.0, -100.0);
  const std::vector<float> more = noise(0.01, -100.0, 4);

  // Звук пропал на 0.5 с: следующий блок начинается позже конца прошлого, а
  // шаг идёт дальше.
  SilenceStep silence(detector, kRate);
  REQUIRE(silence.start());
  silence.receive({.samples = quiet, .start = Moment()});
  const double left = silence.result().silenceLeft / Interval::kSecond;
  REQUIRE(left < 1.2);
  silence.receive({.samples = more, .start = Moment() + 2500 * Interval::kMillisecond});
  CHECK(silence.result().silenceLeft / Interval::kSecond == doctest::Approx(left - 0.51));

  NotesStep notes(detector, kRate);
  REQUIRE(notes.start(-90.0));
  notes.receive({.samples = std::span(quiet).first(samplesOf(0.2)), .start = Moment()});
  REQUIRE(notes.result().step == NotesStep::Step::Working);
  REQUIRE(notes.result().sinceLastNote / Interval::kSecond == doctest::Approx(0.1));
  notes.receive({.samples = more, .start = Moment() + 700 * Interval::kMillisecond});
  CHECK(notes.result().sinceLastNote / Interval::kSecond == doctest::Approx(0.61));

  ResultStep result(detector, kRate, Interval::kSecond);
  REQUIRE(result.start(-80.0));
  result.receive({.samples = std::span(quiet).first(samplesOf(0.5)), .start = Moment()});
  REQUIRE(result.step() == ResultStep::Step::Loading);
  result.receive({.samples = more, .start = Moment() + Interval::kSecond});
  CHECK(result.step() == ResultStep::Step::Working);
}
