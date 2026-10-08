#include <doctest/doctest.h>

#include "training/onset/detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

using training::onset::Detector;
using training::onset::Onset;
using training::onset::Settings;

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
  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> onsets;
  const std::span<const float> all(signal);
  for (std::size_t from = 0; from < all.size(); from += chunk)
    detector.process(all.subspan(from, std::min(chunk, all.size() - from)), onsets);
  return onsets;
}

} // namespace

TEST_CASE("детектор: щелчки находятся у своего начала") {
  const std::vector<std::size_t> starts{4410, 11025, 22050, 30000};
  const std::vector<Onset> onsets = detect(clicks(starts, 44100), 512);

  REQUIRE(onsets.size() == starts.size());
  for (std::size_t i = 0; i < starts.size(); ++i) {
    // Начало найдено в пределах 0.25 мс: 11 сэмплов при 44.1 кГц.
    CHECK(std::abs(onsets[i].position - static_cast<double>(starts[i])) < 11.0);
    CHECK(onsets[i].levelDb > -16.0);
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
      CHECK(onsets[i].levelDb == reference[i].levelDb);
    }
  }
}

TEST_CASE("детектор: мёртвое время глотает второй щелчок рядом") {
  // Второй щелчок через 10 мс, третий — через 30 мс: оба внутри мёртвого
  // времени 40 мс, как тело ноты после щелчка медиатора. Четвёртый — через
  // 100 мс после первого, когда тот уже затих ниже порога.
  const std::vector<Onset> onsets = detect(clicks({4410, 4851, 5733, 8820}, 20000), 256);

  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(onsets[0].position - 4410.0) < 11.0);
  CHECK(std::abs(onsets[1].position - 8820.0) < 11.0);
}

TEST_CASE("детектор: щелчок сразу после мёртвого времени находится") {
  // Второй щелчок через 45 мс. В окне максимума — хвост первого от 20 до 40 мс,
  // уже в 7 раз тише: второй поднимается над ним резко.
  const std::vector<Onset> onsets = detect(clicks({4410, 6395}, 20000), 256);

  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(onsets[0].position - 4410.0) < 11.0);
  CHECK(std::abs(onsets[1].position - 6395.0) < 11.0);
}

TEST_CASE("детектор: reset начинает поток заново") {
  const std::vector<float> signal = clicks({3000}, 10000);
  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> first;
  detector.process(signal, first);
  CHECK(detector.position() == signal.size());

  detector.reset();
  CHECK(detector.position() == 0);

  std::vector<Onset> second;
  detector.process(signal, second);
  REQUIRE(first.size() == 1);
  REQUIRE(second.size() == 1);
  CHECK(second[0].position == first[0].position);
}

TEST_CASE("детектор: порог тишины меняется на ходу") {
  // Тихий щелчок −40 dBFS: при пороге −50 он атака, при пороге −30 — нет.
  std::vector<float> quiet = clicks({2000, 12000}, 20000);
  for (float &sample : quiet)
    sample *= 0.02F;

  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> onsets;
  const std::span<const float> all(quiet);
  detector.process(all.first(10000), onsets);
  CHECK(onsets.size() == 1);

  detector.setSilenceDb(-30.0);
  detector.process(all.subspan(10000), onsets);
  CHECK(onsets.size() == 1);
  CHECK(detector.position() == quiet.size());
}

TEST_CASE("детектор: пропуск двигает время, атака после него — с учётом пропуска") {
  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> onsets;
  const std::vector<float> silence(4410, 0.0F);
  detector.process(silence, onsets);
  detector.skip(4410);
  CHECK(detector.position() == 8820);

  detector.process(clicks({4410}, 13230), onsets);
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(onsets[0].position - 13230.0) < 11.0);
  CHECK(detector.position() == 8820 + 13230);
}

TEST_CASE("детектор: мёртвое время отсчитывается и через пропуск") {
  // Мёртвое время 100 мс, щелчок на 100 мс. Пропуск начинается на 110 мс.
  const Settings settings{.sampleRate = kSampleRate, .deadTimeMs = 100.0};
  const std::vector<float> before = clicks({4410}, 4851);
  const std::vector<float> after = clicks({1764}, 4410);

  // Пропуск 80 мс: второй щелчок через 40 мс после пропуска — на 230 мс, через
  // 130 мс после первого. Мёртвое время кончилось.
  Detector longSkip(settings);
  std::vector<Onset> found;
  longSkip.process(before, found);
  longSkip.skip(3528);
  longSkip.process(after, found);
  REQUIRE(found.size() == 2);
  CHECK(std::abs(found[1].position - (4851.0 + 3528.0 + 1764.0)) < 11.0);

  // Пропуск 20 мс: второй щелчок на 170 мс, через 70 мс после первого — ещё в
  // мёртвом времени.
  Detector shortSkip(settings);
  std::vector<Onset> suppressed;
  shortSkip.process(before, suppressed);
  shortSkip.skip(882);
  shortSkip.process(after, suppressed);
  CHECK(suppressed.size() == 1);
}

TEST_CASE("детектор: звук после пропуска не даёт ложной атаки") {
  // Ровный тон до пропуска и после: на стыке звук прыгает, но окна, задевающие
  // пропуск, в сравнения не идут.
  std::vector<float> tone(8820);
  for (std::size_t i = 0; i < tone.size(); ++i)
    tone[i] = static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 3000.0 *
                                                static_cast<double>(i) / kSampleRate));

  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> onsets;
  detector.process(tone, onsets);
  const std::size_t beforeSkip = onsets.size();
  detector.skip(2205);
  detector.process(std::span<const float>(tone).subspan(1234), onsets);
  CHECK(onsets.size() == beforeSkip);
}
