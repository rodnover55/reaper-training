#include <doctest/doctest.h>

#include "synth.hpp"
#include "wav.hpp"

#include "training/onset/calibration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <random>
#include <span>
#include <vector>

using training::onset::Calibration;
using training::onset::CalibrationResult;
using training::onset::CalibrationStep;
using training::onset::judgeCalibration;
using training::onset::kCalibrationFloorDb;
using training::onset::kCalibrationNotes;
using training::onset::Verdict;
using training::test::evenPlucks;
using training::test::Pluck;
using training::test::readWav;
using training::test::render;
using training::test::SynthSettings;

namespace {

constexpr double kSampleRate = 48000.0;

/// Начало потока, которое шаг тишины не слушает, и сам шаг, в сэмплах.
constexpr std::size_t kSettle = 4800;
constexpr std::size_t kSilence = std::size_t{30} * 4800;

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

/// Подаёт `signal` в калибровку кусками, длины которых даёт `next`.
void feed(Calibration &calibration, std::span<const float> signal,
          const std::function<std::size_t()> &next) {
  for (std::size_t from = 0; from < signal.size();) {
    const std::size_t length = std::min(next(), signal.size() - from);
    calibration.process(signal.subspan(from, length));
    from += length;
  }
}

void feed(Calibration &calibration, std::span<const float> signal) {
  feed(calibration, signal, [] { return std::size_t{512}; });
}

/// Сэмпл, после которого калибровка перешла к шагу `step`, при подаче по
/// одному сэмплу; пусто — не перешла.
std::optional<std::size_t> stepAt(std::span<const float> signal, CalibrationStep step) {
  Calibration calibration(kSampleRate);
  for (std::size_t i = 0; i < signal.size(); ++i) {
    calibration.process(signal.subspan(i, 1));
    if (calibration.step() == step)
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

TEST_CASE("Калибровка: шаг тишины меряет шум и кончается через 3 с") {
  const std::vector<float> signal = noise(4.0, -100.0);

  CHECK(stepAt(signal, CalibrationStep::Notes) == kSettle + kSilence);

  Calibration calibration(kSampleRate);
  CHECK(calibration.silenceLeft() == doctest::Approx(3.0));
  feed(calibration, std::span(signal).first(kSettle + samplesOf(1.5)));
  CHECK(calibration.step() == CalibrationStep::Silence);
  CHECK(calibration.silenceLeft() == doctest::Approx(1.5));

  feed(calibration, std::span(signal).subspan(kSettle + samplesOf(1.5)));
  CHECK(calibration.step() == CalibrationStep::Notes);
  CHECK(calibration.noiseDb() > -106.0);
  CHECK(calibration.noiseDb() < -94.0);
  CHECK_FALSE(calibration.secondsSinceSound());
}

TEST_CASE("Калибровка: звук в тишине начинает шаг заново") {
  // Щипок на второй секунде шага, через 50 мс струну глушат.
  const std::size_t pluck = kSettle + samplesOf(1.5);
  const std::vector<float> signal =
      render({Pluck{.start = pluck, .amplitude = 0.3},
              Pluck{.start = pluck + samplesOf(0.05), .amplitude = 0.0}},
             samplesOf(8.0), SynthSettings{.noiseDb = -100.0});

  Calibration calibration(kSampleRate);
  feed(calibration, std::span(signal).first(kSettle + samplesOf(3.0)));
  CHECK(calibration.step() == CalibrationStep::Silence);
  REQUIRE(calibration.secondsSinceSound());
  CHECK(*calibration.secondsSinceSound() < 1.5);

  feed(calibration, std::span(signal).subspan(kSettle + samplesOf(3.0)));
  CHECK(calibration.step() == CalibrationStep::Notes);
  CHECK(calibration.noiseDb() < -94.0);
}

TEST_CASE("Калибровка: цифровая тишина — наименьший уровень") {
  const std::vector<float> signal(kSettle + kSilence, 0.0F);
  Calibration calibration(kSampleRate);
  feed(calibration, signal);
  CHECK(calibration.step() == CalibrationStep::Notes);
  CHECK(calibration.noiseDb() == kCalibrationFloorDb);
  CHECK(calibration.levelDb() == kCalibrationFloorDb);
}

TEST_CASE("Калибровка: разрыв начинает тишину заново, ноты остаются") {
  const std::vector<float> quiet = noise(2.0, -100.0);
  Calibration calibration(kSampleRate);
  feed(calibration, quiet);
  CHECK(calibration.silenceLeft() < 1.2);
  calibration.discontinuity();
  CHECK(calibration.silenceLeft() == doctest::Approx(3.0));
  CHECK_FALSE(calibration.secondsSinceSound());
}

TEST_CASE("Калибровка: глушёные ноты DI-гитары") {
  const std::vector<float> signal = mutedQuarters();
  Calibration calibration(kSampleRate);
  feed(calibration, signal);

  REQUIRE(calibration.step() == CalibrationStep::Done);
  REQUIRE(calibration.result());
  const CalibrationResult &result = *calibration.result();
  CHECK(result.verdict == Verdict::Good);
  REQUIRE(calibration.attacksDb().size() == static_cast<std::size_t>(kCalibrationNotes));
  for (const double attack : calibration.attacksDb()) {
    CHECK(attack >= kQuietestClickDb);
    CHECK(attack <= kLoudestClickDb);
  }
  CHECK(result.softestDb < -59.0);
  CHECK(result.peakDb == doctest::Approx(-22.1).epsilon(0.02));
  CHECK(result.thresholdDb == std::lround((result.noiseDb + result.softestDb) / 2.0));
}

TEST_CASE("Калибровка: ход не зависит от нарезки потока") {
  const std::vector<float> signal = mutedQuarters();

  Calibration whole(kSampleRate);
  whole.process(signal);
  REQUIRE(whole.result());

  std::mt19937 random(7);
  std::uniform_int_distribution<std::size_t> lengths(1, 3000);
  for (const auto &next : std::vector<std::function<std::size_t()>>{
           [] { return std::size_t{1}; }, [] { return std::size_t{4096}; },
           [&] { return lengths(random); }}) {
    Calibration chunked(kSampleRate);
    feed(chunked, signal, next);
    REQUIRE(chunked.result());
    CHECK(chunked.attacksDb() == whole.attacksDb());
    CHECK(chunked.noiseDb() == whole.noiseDb());
    CHECK(chunked.result()->peakDb == whole.result()->peakDb);
    CHECK(chunked.result()->thresholdDb == whole.result()->thresholdDb);
  }

  const auto notesAt = stepAt(signal, CalibrationStep::Notes);
  const auto doneAt = stepAt(signal, CalibrationStep::Done);
  CHECK(notesAt == kSettle + kSilence);
  REQUIRE(doneAt);
  CHECK(*doneAt < signal.size());
}

TEST_CASE("Калибровка: без нот — обрыв через 15 с") {
  const std::vector<float> signal = noise(3.1 + 15.5, -100.0);
  const auto doneAt = stepAt(signal, CalibrationStep::Done);
  REQUIRE(doneAt);
  const double afterNotes = static_cast<double>(*doneAt - (kSettle + kSilence)) / kSampleRate;
  CHECK(afterNotes >= 15.0);
  CHECK(afterNotes < 15.006);

  Calibration calibration(kSampleRate);
  feed(calibration, signal);
  REQUIRE(calibration.result());
  CHECK(calibration.result()->verdict == Verdict::NotEnoughNotes);
  CHECK(calibration.result()->notes == 0);
  CHECK(calibration.result()->thresholdDb == kCalibrationFloorDb);
}

TEST_CASE("Калибровка: три ноты и тишина — обрыв с тремя") {
  const std::vector<float> signal = plucked(3, 0.3);
  Calibration calibration(kSampleRate);
  feed(calibration, signal);
  REQUIRE(calibration.result());
  CHECK(calibration.result()->verdict == Verdict::NotEnoughNotes);
  CHECK(calibration.result()->notes == 3);
}

TEST_CASE("Калибровка: восемь щипков на тихом фоне — удача") {
  Calibration calibration(kSampleRate);
  feed(calibration, plucked(8, 0.3));
  REQUIRE(calibration.result());
  CHECK(calibration.result()->verdict == Verdict::Good);
  CHECK(calibration.result()->notes == kCalibrationNotes);
}

TEST_CASE("Калибровка: перегруз входа") {
  Calibration calibration(kSampleRate);
  feed(calibration, plucked(8, 1.0, 6.0));
  REQUIRE(calibration.result());
  CHECK(calibration.result()->verdict == Verdict::Clipping);
  CHECK(calibration.result()->peakDb >= -1.0);
}

TEST_CASE("Калибровка: порог и вердикт по уровням") {
  const std::array<double, 8> quiet{-55, -58, -54, -60, -59, -55, -59, -56};

  const CalibrationResult good = judgeCalibration(-98.0, quiet, -22.0);
  CHECK(good.verdict == Verdict::Good);
  CHECK(good.thresholdDb == -79.0);
  CHECK(good.softestDb == -60.0);
  CHECK(good.loudestDb == -54.0);
  CHECK(good.notes == 8);

  // Половина округляется от нуля.
  CHECK(judgeCalibration(-97.0, quiet, -22.0).thresholdDb == -79.0);

  const CalibrationResult close = judgeCalibration(-66.0, quiet, -24.0);
  CHECK(close.verdict == Verdict::NoiseTooClose);
  CHECK(close.thresholdDb == -63.0);

  CHECK(judgeCalibration(-72.0, quiet, -24.0).verdict == Verdict::Good);
  CHECK(judgeCalibration(-71.0, quiet, -24.0).verdict == Verdict::NoiseTooClose);

  // Перегруз — раньше зазора.
  CHECK(judgeCalibration(-66.0, quiet, -0.1).verdict == Verdict::Clipping);
  CHECK(judgeCalibration(-98.0, quiet, -1.0).verdict == Verdict::Clipping);
  CHECK(judgeCalibration(-98.0, quiet, -1.1).verdict == Verdict::Good);

  // Нот меньше восьми — раньше всего.
  CHECK(judgeCalibration(-98.0, std::span(quiet).first(7), -0.1).verdict ==
        Verdict::NotEnoughNotes);
}

TEST_CASE("Калибровка: удар закрывает итог через 5 с, но не раньше") {
  const auto doneAt = stepAt(pluckedThen({}), CalibrationStep::Done);
  REQUIRE(doneAt);

  const std::size_t early = *doneAt + samplesOf(2.0);
  const std::size_t late = *doneAt + samplesOf(6.0);
  const std::vector<float> signal = pluckedThen({early, late});

  // Удары ничего не меняют в ходе до итога.
  REQUIRE(stepAt(signal, CalibrationStep::Done) == doneAt);

  Calibration calibration(kSampleRate);
  feed(calibration, std::span(signal).first(*doneAt + samplesOf(5.9)));
  REQUIRE(calibration.step() == CalibrationStep::Done);
  CHECK_FALSE(calibration.soundAfterResult());

  feed(calibration, std::span(signal).subspan(*doneAt + samplesOf(5.9)));
  CHECK(calibration.soundAfterResult());
  CHECK(calibration.result()->verdict == Verdict::Good);

  // Нарезка потока не меняет, где отмечен звук.
  std::mt19937 random(11);
  std::uniform_int_distribution<std::size_t> lengths(1, 3000);
  Calibration chunked(kSampleRate);
  feed(chunked, std::span(signal).first(*doneAt + samplesOf(5.9)),
       [&] { return lengths(random); });
  CHECK_FALSE(chunked.soundAfterResult());
  feed(chunked, std::span(signal).subspan(*doneAt + samplesOf(5.9)),
       [&] { return lengths(random); });
  CHECK(chunked.soundAfterResult());
}

TEST_CASE("Калибровка: тишина после итога итог не закрывает") {
  Calibration calibration(kSampleRate);
  feed(calibration, pluckedThen({}));
  REQUIRE(calibration.step() == CalibrationStep::Done);
  CHECK_FALSE(calibration.soundAfterResult());
  CHECK(calibration.levelDb() < calibration.noiseDb() + 20.0);
}
