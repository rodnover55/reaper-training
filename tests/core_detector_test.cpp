#include <doctest/doctest.h>

#include "detector_onsets.hpp"

#include "training/core/detector/detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

using training::core::detector::Detector;
using training::core::detector::Onset;
using training::core::detector::Settings;
using training::test::detectorOf;
using training::test::rateOf;
using training::test::takeOnsets;

namespace {

constexpr double kSampleRate = 44100.0;

/// Сигнал из щелчков: затухающая синусоида 3 кГц с амплитудой 0.5. Синусоида
/// начинается с нуля ровно на заданной позиции — это и есть начало щелчка.
std::vector<float> clicks(const std::vector<std::size_t> &starts, std::size_t length) {
  std::vector<float> signal(length, 0.0F);
  for (const std::size_t start : starts) {
    for (std::size_t i = 0; start + i < length && i < 2000; ++i) {
      const double t = static_cast<double>(i) / kSampleRate;
      const double value =
          0.5 * std::sin(2.0 * std::numbers::pi * 3000.0 * t) * std::exp(-t / 0.01);
      signal[start + i] += static_cast<float>(value);
    }
  }
  return signal;
}

/// Прогоняет сигнал через новый детектор кусками по `chunk` сэмплов.
std::vector<Onset> detect(const std::vector<float> &signal, std::size_t chunk) {
  Detector detector = detectorOf(Settings{.sampleRate = rateOf(kSampleRate)});
  std::vector<Onset> onsets;
  const std::span<const float> all(signal);
  for (std::size_t from = 0; from < all.size(); from += chunk) {
    detector.process(all.subspan(from, std::min(chunk, all.size() - from)));
    takeOnsets(detector, onsets);
  }
  return onsets;
}

} // namespace

TEST_CASE("детектор: щелчки находятся у своего начала") {
  const std::vector<std::size_t> starts{4410, 11025, 22050, 30000};
  const std::vector<Onset> onsets = detect(clicks(starts, 44100), 512);

  REQUIRE(onsets.size() == starts.size());
  for (std::size_t i = 0; i < starts.size(); ++i) {
    // Начало найдено в пределах 0.25 мс: 11 сэмплов при 44.1 кГц.
    CHECK(std::abs(static_cast<double>(onsets[i].position) - static_cast<double>(starts[i])) <
          11.0);
  }
}

TEST_CASE("детектор: нарезка потока не меняет результат") {
  const std::vector<float> signal = clicks({1000, 5000, 9001, 20000, 31234}, 40000);
  const std::vector<Onset> reference = detect(signal, signal.size());
  REQUIRE(reference.size() == 5);

  for (const std::size_t chunk : {64U, 512U, 1000U}) {
    CAPTURE(chunk);
    const std::vector<Onset> onsets = detect(signal, chunk);
    REQUIRE(onsets.size() == reference.size());
    for (std::size_t i = 0; i < onsets.size(); ++i) {
      CHECK(onsets[i].position == reference[i].position);
    }
  }
}

TEST_CASE("детектор: мёртвое время глотает второй щелчок рядом") {
  // Второй щелчок через 10 мс, третий — через 30 мс: оба внутри мёртвого
  // времени 40 мс, как тело ноты после щелчка медиатора. Четвёртый — через
  // 100 мс после первого, когда тот уже затих ниже порога.
  const std::vector<Onset> onsets = detect(clicks({4410, 4851, 5733, 8820}, 20000), 256);

  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(static_cast<double>(onsets[0].position) - 4410.0) < 11.0);
  CHECK(std::abs(static_cast<double>(onsets[1].position) - 8820.0) < 11.0);
}

TEST_CASE("детектор: щелчок сразу после мёртвого времени находится") {
  // Второй щелчок через 45 мс. В окне максимума — хвост первого от 20 до 40 мс,
  // уже в 7 раз тише: второй поднимается над ним резко.
  const std::vector<Onset> onsets = detect(clicks({4410, 6395}, 20000), 256);

  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(static_cast<double>(onsets[0].position) - 4410.0) < 11.0);
  CHECK(std::abs(static_cast<double>(onsets[1].position) - 6395.0) < 11.0);
}

TEST_CASE("детектор: reset начинает поток заново") {
  const std::vector<float> signal = clicks({3000}, 10000);
  Detector detector = detectorOf(Settings{.sampleRate = rateOf(kSampleRate)});
  std::vector<Onset> first;
  detector.process(signal);
  takeOnsets(detector, first);

  // Непрочитанная атака уходит вместе с прошлым потоком.
  detector.process(signal);
  detector.reset();
  CHECK_FALSE(detector.takeOnset());

  std::vector<Onset> second;
  detector.process(signal);
  takeOnsets(detector, second);
  REQUIRE(first.size() == 1);
  REQUIRE(second.size() == 1);
  CHECK(second[0].position == first[0].position);
}

TEST_CASE("детектор: настройки — те, с которыми он создан") {
  // Тихий щелчок −40 dBFS: при пороге −50 он атака, при пороге −30 — нет.
  std::vector<float> quiet = clicks({2000, 12000}, 20000);
  for (float &sample : quiet)
    sample *= 0.02F;

  for (const double silenceDb : {-50.0, -30.0}) {
    CAPTURE(silenceDb);
    const Settings settings{.sampleRate = rateOf(kSampleRate), .silenceDb = silenceDb};
    Detector detector = detectorOf(settings);
    CHECK(detector.settings().silenceDb == silenceDb);
    CHECK(detector.settings().sampleRate == settings.sampleRate);
    std::vector<Onset> onsets;
    detector.process(quiet);
    takeOnsets(detector, onsets);
    CHECK(onsets.size() == (silenceDb < -40.0 ? 2U : 0U));
  }
}

TEST_CASE("детектор: новый порог тишины действует без пересоздания") {
  // Тихий щелчок −40 dBFS: при пороге −50 он атака, при пороге −30 — нет.
  // Порог меняется между щелчками; непрочитанная атака первого остаётся.
  std::vector<float> quiet = clicks({2000, 12000}, 20000);
  for (float &sample : quiet)
    sample *= 0.02F;
  const std::span<const float> all(quiet);

  for (const double before : {-50.0, -30.0}) {
    CAPTURE(before);
    const double after = before < -40.0 ? -30.0 : -50.0;
    Detector detector =
        detectorOf(Settings{.sampleRate = rateOf(kSampleRate), .silenceDb = before});
    detector.process(all.first(7000));
    detector.setSilenceDb(after);
    CHECK(detector.settings().silenceDb == after);
    detector.process(all.subspan(7000));

    std::vector<Onset> onsets;
    takeOnsets(detector, onsets);
    REQUIRE(onsets.size() == 1);
    // Начало найдено в пределах 0.25 мс: 11 сэмплов при 44.1 кГц.
    const double start = before < -40.0 ? 2000.0 : 12000.0;
    CHECK(std::abs(static_cast<double>(onsets[0].position) - start) < 11.0);
  }
}

TEST_CASE("детектор: пропуск двигает время, атака после него — с учётом пропуска") {
  Detector detector = detectorOf(Settings{.sampleRate = rateOf(kSampleRate)});
  std::vector<Onset> onsets;
  const std::vector<float> silence(4410, 0.0F);
  detector.process(silence);
  takeOnsets(detector, onsets);
  detector.skip(4410);

  detector.process(clicks({4410}, 13230));
  takeOnsets(detector, onsets);
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(static_cast<double>(onsets[0].position) - 13230.0) < 11.0);
}

TEST_CASE("детектор: мёртвое время отсчитывается и через пропуск") {
  // Мёртвое время 100 мс, щелчок на 100 мс. Пропуск начинается на 110 мс.
  const Settings settings{.sampleRate = rateOf(kSampleRate),
                          .deadTime = 100 * training::core::Interval::kMillisecond};
  const std::vector<float> before = clicks({4410}, 4851);
  const std::vector<float> after = clicks({1764}, 4410);

  // Пропуск 80 мс: второй щелчок через 40 мс после пропуска — на 230 мс, через
  // 130 мс после первого. Мёртвое время кончилось.
  Detector longSkip = detectorOf(settings);
  std::vector<Onset> found;
  longSkip.process(before);
  takeOnsets(longSkip, found);
  longSkip.skip(3528);
  longSkip.process(after);
  takeOnsets(longSkip, found);
  REQUIRE(found.size() == 2);
  CHECK(std::abs(static_cast<double>(found[1].position) - (4851.0 + 3528.0 + 1764.0)) < 11.0);

  // Пропуск 20 мс: второй щелчок на 170 мс, через 70 мс после первого — ещё в
  // мёртвом времени.
  Detector shortSkip = detectorOf(settings);
  std::vector<Onset> suppressed;
  shortSkip.process(before);
  takeOnsets(shortSkip, suppressed);
  shortSkip.skip(882);
  shortSkip.process(after);
  takeOnsets(shortSkip, suppressed);
  CHECK(suppressed.size() == 1);
}

TEST_CASE("детектор: пропуск — то же, что тишина той же длины") {
  // Щелчки до пропуска, сразу после него и позже: пропуск и поданные нули
  // дают одни и те же атаки.
  const std::vector<float> before = clicks({1000, 6000}, 7000);
  const std::vector<float> after = clicks({0, 300, 4000}, 9000);
  const auto detect = [&before, &after](bool skip) {
    Detector detector = detectorOf(Settings{.sampleRate = rateOf(kSampleRate)});
    std::vector<Onset> onsets;
    detector.process(before);
    if (skip)
      detector.skip(2205);
    else
      detector.process(std::vector<float>(2205, 0.0F));
    detector.process(after);
    takeOnsets(detector, onsets);
    return onsets;
  };

  const std::vector<Onset> skipped = detect(true);
  CHECK(skipped == detect(false));
  REQUIRE(skipped.size() == 4);
  CHECK(std::abs(static_cast<double>(skipped[2].position) - (7000.0 + 2205.0)) < 11.0);
}

TEST_CASE("детектор: нулевой пропуск ничего не меняет") {
  const std::vector<float> signal = clicks({1000, 6000}, 9000);
  const std::span<const float> all(signal);
  Detector skipped = detectorOf(Settings{.sampleRate = rateOf(kSampleRate)});
  skipped.process(all.first(3000));
  skipped.skip(0);
  skipped.process(all.subspan(3000));
  std::vector<Onset> found;
  takeOnsets(skipped, found);

  std::vector<Onset> reference;
  Detector whole = detectorOf(Settings{.sampleRate = rateOf(kSampleRate)});
  whole.process(signal);
  takeOnsets(whole, reference);
  CHECK(found == reference);
}

TEST_CASE("детектор: частота среза вне промежутка ВЧ-фильтра — детектора нет") {
  CHECK_FALSE(Detector::create(Settings{.sampleRate = rateOf(1600.0)}));
  CHECK(Detector::create(Settings{.sampleRate = rateOf(2400.0)}));
}
