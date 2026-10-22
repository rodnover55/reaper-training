#include <doctest/doctest.h>

#include "training/core/detector/detector.hpp"
#include "training/core/errors.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/settings.hpp"
#include "training/core/training/audio.hpp"
#include "training/core/training/state/bar.hpp"
#include "training/core/training/state/note.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

using training::core::HitWindow;
using training::core::Interval;
using training::core::Moment;
using training::core::SampleRate;
using training::core::detector::Settings;
using training::core::training::Audio;
using training::core::training::state::Bar;
using training::core::training::state::Beat;
using training::core::training::state::Note;

namespace {

/// Время ядра через `seconds` секунд от точки отсчёта.
Moment timeAt(double seconds) { return Moment() + seconds * Interval::kSecond; }

constexpr double kSampleRate = 48000.0;

/// Допуск на начало атаки: 0.25 мс в сэмплах.
constexpr double kStartTolerance = 12.0;

/// Время единственной доли такта тестов: разница ноты от неё — время ноты.
constexpr double kOrigin = -10.0;

/// Сигнал из щелчков: затухающая синусоида 3 кГц с амплитудой 0.5, которая
/// начинается с нуля ровно на заданном сэмпле.
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

/// Окно попадания тестов: 0 ± 10 мс.
constexpr HitWindow kWindow = *HitWindow::create(Interval(), 10 * Interval::kMillisecond);

/// Тренировка звука частоты `rate` с окном тестов и порогом тишины детектора
/// по умолчанию.
std::unique_ptr<Audio> audioAt(double rate = kSampleRate) {
  return std::move(*Audio::create(*SampleRate::create(rate),
                                  training::core::Settings(kWindow, Settings().silenceDb)));
}

/// Тренировка звука с частотой `kSampleRate` и своим детектором. Такты
/// тренировки — один такт с долей на `kOrigin` секундах и такт после него на
/// 1000 с: все ноты ложатся в эту долю, и время ноты — её разница плюс
/// `kOrigin`.
struct Bench {
  std::unique_ptr<Audio> trainer = audioAt();

  /// @param started запустить ли тренировку сразу.
  explicit Bench(bool started = true) {
    if (started)
      start();
  }

  /// Запускает тренировку и передаёт ей такты тестов.
  void start() const {
    REQUIRE(trainer->start().has_value());
    Bar whole("0", timeAt(kOrigin), timeAt(1000.0), {}, 120.0);
    whole.addDivision(Beat(0.0), timeAt(kOrigin));
    trainer->addBar(whole);
    Bar after("1", timeAt(1000.0), timeAt(1001.0), {}, 120.0);
    after.addDivision(Beat(0.0), timeAt(1000.0));
    trainer->addBar(after);
  }

  /// Останавливает и снова запускает тренировку (`start`).
  void restart() const {
    trainer->stop();
    start();
  }

  /// Подаёт блок сигнала `signal`, который начинается на его сэмпле `at` и
  /// длится `block` сэмплов или до конца сигнала. Время первого сэмпла
  /// сигнала — `origin` секунд.
  void feedBlock(std::span<const float> signal, std::size_t at, std::size_t block,
                 double origin = 0.0, double rate = kSampleRate) const {
    trainer->receive({.samples = signal.subspan(at, std::min(block, signal.size() - at)),
                      .start = timeAt(origin + static_cast<double>(at) / rate)});
  }

  /// Подаёт `signal` блоками по `block` сэмплов (`feedBlock`) с сэмпла
  /// `from` до сэмпла `to`. Блоки с номерами из `lost` пропадают.
  void feed(const std::vector<float> &signal, std::size_t block, double origin = 0.0,
            const std::vector<std::size_t> &lost = {}, double rate = kSampleRate,
            std::size_t from = 0, std::size_t to = SIZE_MAX) const {
    for (std::size_t at = from, number = from / block; at < std::min(to, signal.size());
         at += block, ++number)
      if (std::ranges::find(lost, number) == lost.end())
        feedBlock(signal, at, block, origin, rate);
  }

  /// Время легших нот, секунды, в порядке постановки.
  std::vector<double> onsets() const {
    std::vector<double> times;
    const std::vector<Bar> bars = trainer->result().timeline.bars();
    if (bars.empty())
      return times;
    const std::map<Beat, std::vector<Note>> notes = bars.front().notes();
    if (!notes.contains(Beat(0.0)))
      return times;
    for (const Note &note : notes.at(Beat(0.0)))
      times.push_back(kOrigin + note.deviation / Interval::kSecond);
    return times;
  }
};

/// Начало атаки на `seconds` секундах в сэмплах сигнала, первый сэмпл которого —
/// на `origin` секундах.
double startOf(double seconds, double origin = 0.0, double rate = kSampleRate) {
  return (seconds - origin) * rate;
}

} // namespace

TEST_CASE("Тренировка звука: атака ложится со временем её начала") {
  const Bench bench;
  bench.feed(clicks({4800, 12000}, 24000), 512);

  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(startOf(onsets[0]) - 4800.0) < kStartTolerance);
  CHECK(std::abs(startOf(onsets[1]) - 12000.0) < kStartTolerance);
}

TEST_CASE(
    "Тренировка звука: атака у конца блока — со своим временем, хотя найдена в следующих") {
  // Блоки по 32 сэмпла: детектор замечает атаку на несколько блоков позже
  // её начала.
  const Bench bench;
  bench.feed(clicks({4800 + 30, 9600 + 31}, 14400), 32);

  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(startOf(onsets[0]) - 4830.0) < kStartTolerance);
  CHECK(std::abs(startOf(onsets[1]) - 9631.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: атака в первом блоке запуска измеряется") {
  const Bench bench;
  bench.feed(clicks({4000, 12000}, 14400), 4800);
  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(startOf(onsets[0]) - 4000.0) < kStartTolerance);
  CHECK(std::abs(startOf(onsets[1]) - 12000.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: атака на 300-м сэмпле первого блока — на 6.25 мс") {
  const Bench bench;
  bench.feed(clicks({300}, 9600), 512, 2.0);
  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(startOf(onsets[0], 2.0) - 300.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: звон до запуска атакой на старте не становится") {
  // Струна 196 Гц звенит с ноты за 2 с до запуска и в момент запуска ещё
  // громче порога тишины; через 20 мс после запуска — щелчок.
  constexpr std::size_t kPlay = 96000;
  std::vector<float> signal = clicks({kPlay + 960}, kPlay + 9600);
  for (std::size_t i = 0; i < signal.size(); ++i) {
    const double t = static_cast<double>(i) / kSampleRate;
    signal[i] += static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 196.0 * t) *
                                    std::exp(-t / 1.0));
  }
  const Bench bench(false);
  bench.feed(signal, 480, 0.0, {}, kSampleRate, 0, kPlay);
  bench.start();
  bench.feed(signal, 480, 0.0, {}, kSampleRate, kPlay);
  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(startOf(onsets[0]) - static_cast<double>(kPlay + 960)) < kStartTolerance);
}

TEST_CASE("Тренировка звука: атака до запуска запуску не достаётся") {
  // Щелчок на 4800 — до запуска на 9728.
  const Bench bench(false);
  const std::vector<float> signal = clicks({4800, 12000}, 19200);
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 0, 9728);
  bench.start();
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 9728);

  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(startOf(onsets[0]) - 12000.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: после остановки атаки не ложатся") {
  Bench bench;
  const std::vector<float> signal = clicks({4800, 12000}, 19200);
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 0, 9728);
  REQUIRE(bench.onsets().size() == 1);

  bench.trainer->stop();
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 9728);
  CHECK(bench.onsets().size() == 1);
}

TEST_CASE("Тренировка звука: новый запуск начинает поток детектора заново") {
  // Остановка и запуск без перерыва в звуке. Щелчок 2000 — в прошлом запуске,
  // щелчок 3440 через 30 мс — после нового запуска на 2560.
  const std::vector<float> signal = clicks({2000, 3440}, 9600);
  constexpr std::size_t kBlock = 512;

  const Bench bench;
  bench.feed(signal, kBlock, 0.0, {}, kSampleRate, 0, 5 * kBlock);
  REQUIRE(bench.onsets().size() == 1);
  bench.restart();
  bench.feed(signal, kBlock, 0.0, {}, kSampleRate, 5 * kBlock);

  // Поток начат заново: мёртвое время щелчка 2000 на новый запуск не идёт.
  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(startOf(onsets[0]) - 3440.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: новый запуск начинает время блоков заново") {
  // Прошлый запуск разобрал звук до 0.2 с; новый запуск идёт снова от 0 с.
  const Bench bench;
  bench.feed(clicks({}, 9600), 512);
  bench.restart();

  bench.feed(clicks({2400}, 9600), 512);
  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(startOf(onsets[0]) - 2400.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: пропавший звук занимает время") {
  // Мёртвое время 40 мс. Щелчок на 4800 и второй через 45 мс, на 6960; блок
  // 5120–5631 пропал. Пропуск отсчитан как время: второй щелчок — после
  // мёртвого времени.
  const Bench bench;
  bench.feed(clicks({4800, 6960}, 9600), 512, 0.0, {10});
  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(startOf(onsets[0]) - 4800.0) < kStartTolerance);
  CHECK(std::abs(startOf(onsets[1]) - 6960.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: пустой блок несёт только время") {
  // Блок 5120–5631 пришёл без звука: время идёт, как у пропавшего.
  const std::vector<float> signal = clicks({4800, 6960}, 9600);
  Bench bench;
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 0, 5120);
  bench.trainer->receive({.samples = {}, .start = timeAt(5120.0 / kSampleRate)});
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 5632);

  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(startOf(onsets[1]) - 6960.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: начало блока в разобранном звуке отрезается") {
  // Блок 4608–5119 приходит второй раз, начатым на 100 сэмплов раньше конца
  // разобранного звука: его начало отрезано, щелчок 4800 находится один раз.
  const std::vector<float> signal = clicks({4800, 12000}, 19200);
  const Bench bench;
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 0, 5120);
  bench.feedBlock(signal, 5020, 612);
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 5632);

  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(startOf(onsets[0]) - 4800.0) < kStartTolerance);
  CHECK(std::abs(startOf(onsets[1]) - 12000.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: блок целиком в разобранном звуке отбрасывается") {
  const std::vector<float> signal = clicks({4800}, 9600);
  const Bench bench;
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 0, 5120);
  bench.feedBlock(signal, 4608, 512);
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 5120);

  CHECK(bench.onsets().size() == 1);
}

TEST_CASE("Тренировка звука: частота — та, с которой она создана") {
  CHECK(audioAt(44100.0)->sampleRate() == *SampleRate::create(44100.0));
}

TEST_CASE("Тренировка звука: на частоте, где детектор не создаётся, — CannotStartUnit") {
  // ВЧ-фильтр детектора на 1 кГц требует частоту выше 2 кГц.
  CHECK(Audio::create(*SampleRate::create(1600.0),
                      training::core::Settings(kWindow, Settings().silenceDb)) ==
        std::unexpected(training::core::CannotStartUnit{}));
}

TEST_CASE("Тренировка звука: новый порог тишины — запуск продолжается") {
  // Тихий щелчок −40 dBFS: с порогом −50 он атака, с новым порогом −30 — нет,
  // а такты и легшие ноты запуска остаются.
  std::vector<float> quiet = clicks({4800, 14400}, 19200);
  for (float &sample : quiet)
    sample *= 0.02F;
  Bench bench;
  bench.feed(quiet, 480, 0.0, {}, kSampleRate, 0, 9600);
  REQUIRE(bench.onsets().size() == 1);

  bench.trainer->setSettings(training::core::Settings(kWindow, -30.0));
  bench.feed(quiet, 480, 0.0, {}, kSampleRate, 9600);
  CHECK(bench.trainer->result().step == training::core::training::Trainer::Step::Working);
  CHECK(bench.onsets().size() == 1);
}

TEST_CASE("Тренировка звука: с новым порогом тишины атаки ложатся со своим временем") {
  Bench bench;
  bench.feed(clicks({}, 9600), 480);
  bench.trainer->setSettings(training::core::Settings(kWindow, -60.0));
  bench.feed(clicks({2400}, 9600), 480, 0.2);
  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(startOf(onsets[0], 0.2) - 2400.0) < kStartTolerance);
}

TEST_CASE(
    "Тренировка звука: блок, начатый в конце прошлого, стыкуется без пропуска и отрезания") {
  // Блоки по 10 мс подряд с 1 с: каждый начинается в конце разобранного звука,
  // поэтому щелчок находится один раз и со своим временем.
  const Bench bench;
  bench.feed(clicks({2400}, 9600), 480, 1.0);
  const std::vector<double> onsets = bench.onsets();
  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(startOf(onsets[0], 1.0) - 2400.0) < kStartTolerance);
}

TEST_CASE("Тренировка звука: отказ запуска оставляет конец разобранного звука") {
  // Блок 4608–5119 приходит второй раз после отказа запуска: он целиком в
  // разобранном звуке и отбрасывается, щелчок 4800 находится один раз.
  const std::vector<float> signal = clicks({4800}, 9600);
  Bench bench;
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 0, 5120);
  CHECK_FALSE(bench.trainer->start().has_value());
  bench.feedBlock(signal, 4608, 512);
  bench.feed(signal, 512, 0.0, {}, kSampleRate, 5120);

  CHECK(bench.onsets().size() == 1);
}
