#include <doctest/doctest.h>

#include "training/trainer/onset_search.hpp"
#include "training/trainer/transport.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using training::trainer::AudioBlock;
using training::trainer::AudioOnset;
using training::trainer::Compensation;
using training::trainer::CountIn;
using training::trainer::Moment;
using training::trainer::Transport;

using namespace std::chrono_literals;

namespace {

constexpr double kSampleRate = 48000.0;

/// Порог тишины тестов, dBFS.
constexpr double kSilenceDb = -50.0;

/// Допуск на начало атаки: 0.25 мс в сэмплах.
constexpr double kStartTolerance = 12.0;

/// Момент, когда хост отдал блок с первым сэмплом `sample` в своём счёте:
/// 100 с плюс время звука до него.
Moment momentOf(std::uint64_t sample, double rate = kSampleRate) {
  return Moment(100s) + std::chrono::round<std::chrono::nanoseconds>(
                            std::chrono::duration<double>(static_cast<double>(sample) / rate));
}

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

/// Поиск атак транспорта со строками журнала в `lines`.
struct Bench {
  std::vector<std::string> lines;
  Transport transport{[this](std::string_view line) { lines.emplace_back(line); }};
  std::vector<AudioOnset> onsets;

  /// @param started начать ли запуск с момента первого сэмпла хоста.
  explicit Bench(bool started = true) {
    if (started)
      REQUIRE(transport.start(momentOf(0), 0.0, Compensation{}));
  }

  /// Кончает запуск и начинает новый с момента сэмпла хоста `sample`.
  void restart(std::uint64_t sample) {
    REQUIRE(transport.stop());
    REQUIRE(transport.start(momentOf(sample), 0.0, Compensation{}));
  }

  /// Подаёт блок сигнала `signal`, который начинается на сэмпле `at` и длится
  /// `block` сэмплов или до конца сигнала. Номер первого сэмпла блока — `at`
  /// плюс `first`, момент блока — `momentOf` этого номера. Позиция блока —
  /// `at` в секундах плюс `origin`, а если `standing` — `origin`: транспорт
  /// стоит.
  void feedBlock(std::span<const float> signal, std::size_t at, std::size_t block,
                 double origin = 0.0, bool standing = false, std::uint64_t first = 0,
                 double rate = kSampleRate) {
    const double start = static_cast<double>(at) / rate;
    transport.onsetSearch().process(AudioBlock{.firstSample = first + at,
                                               .position = standing ? origin : origin + start,
                                               .sampleRate = rate,
                                               .monotonic = momentOf(first + at, rate)},
                                    signal.subspan(at, std::min(block, signal.size() - at)),
                                    kSilenceDb, onsets);
  }

  /// Подаёт `signal` блоками по `block` сэмплов (`feedBlock`). Блоки с
  /// номерами из `lost` пропадают.
  void feed(const std::vector<float> &signal, std::size_t block, double origin = 0.0,
            bool standing = false, std::uint64_t first = 0,
            const std::vector<std::size_t> &lost = {}, double rate = kSampleRate) {
    for (std::size_t at = 0, number = 0; at < signal.size(); at += block, ++number)
      if (std::ranges::find(lost, number) == lost.end())
        feedBlock(signal, at, block, origin, standing, first, rate);
  }

  bool logged(std::string_view prefix) const {
    return std::ranges::any_of(
        lines, [prefix](const std::string &line) { return line.starts_with(prefix); });
  }
};

/// Позиция начала атаки в сигнале, сэмплов: позиция блока плюс сдвиг.
double startOf(const AudioOnset &onset, double origin = 0.0, double rate = kSampleRate) {
  return (onset.blockPosition - origin) * rate + onset.offset;
}

} // namespace

TEST_CASE("Поиск атак: атака отдаётся с блоком, в котором началась") {
  Bench bench;
  bench.feed(clicks({4800, 12000}, 24000), 512);

  REQUIRE(bench.onsets.size() == 2);
  // 4800 — в блоке 9, который начался на сэмпле 4608.
  CHECK(bench.onsets[0].blockPosition == doctest::Approx(4608.0 / kSampleRate));
  CHECK(std::abs(bench.onsets[0].offset - 192.0) < kStartTolerance);
  CHECK(bench.onsets[0].monotonic == momentOf(4608));
  CHECK(bench.onsets[0].sampleRate == kSampleRate);
  CHECK(std::abs(startOf(bench.onsets[1]) - 12000.0) < kStartTolerance);
  // Журнал пишет момент блока атаки.
  CHECK(bench.logged("onset at 100.096000 P=0.096000 "));
}

TEST_CASE("Поиск атак: атака у конца блока — с этим блоком, хотя найдена в следующих") {
  // Блоки по 32 сэмпла: детектор замечает атаку на несколько блоков позже
  // её начала, а отдаёт с блоком начала.
  Bench bench;
  bench.feed(clicks({4800 + 30, 9600 + 31}, 14400), 32);

  REQUIRE(bench.onsets.size() == 2);
  CHECK(std::abs(startOf(bench.onsets[0]) - 4830.0) < kStartTolerance);
  CHECK(std::abs(startOf(bench.onsets[1]) - 9631.0) < kStartTolerance);
  for (const AudioOnset &onset : bench.onsets) {
    CHECK(onset.offset >= 0.0);
    CHECK(onset.offset < 32.0);
  }
}

TEST_CASE("Поиск атак: атака отсчёта — перед позицией начала на свой путь до конца "
          "отсчёта") {
  // Отсчёт 19200 сэмплов (0.4 с) перед запуском с 8 с: позиция блоков отсчёта
  // стоит на 8 с.
  Bench bench(false);
  REQUIRE(bench.transport.start(momentOf(0), 8.0, Compensation{},
                                CountIn{.samples = 19200, .sampleRate = kSampleRate}, 1.0));
  bench.feed(clicks({4800, 12000}, 19200), 512, 8.0, true);
  REQUIRE(bench.onsets.size() == 2);
  // Отсчёт начался на 8.0 − 0.4 = 7.6 с.
  CHECK(std::abs(startOf(bench.onsets[0], 7.6) - 4800.0) < kStartTolerance);
  CHECK(std::abs(startOf(bench.onsets[1], 7.6) - 12000.0) < kStartTolerance);
  CHECK(bench.onsets[0].beforeStart);
  CHECK(bench.onsets[1].beforeStart);

  // После отсчёта позиция пошла с 8 с.
  bench.feed(clicks({4800}, 9600), 512, 8.0, false, 19200);
  REQUIRE(bench.onsets.size() == 3);
  CHECK(std::abs(startOf(bench.onsets[2], 8.0) - 4800.0) < kStartTolerance);
  CHECK_FALSE(bench.onsets[2].beforeStart);
}

TEST_CASE("Поиск атак: атака отсчёта учитывает скорость запуска") {
  // Скорость 2: 19200 сэмплов отсчёта — 0.8 с шкалы.
  Bench bench(false);
  REQUIRE(bench.transport.start(momentOf(0), 8.0, Compensation{},
                                CountIn{.samples = 19200, .sampleRate = kSampleRate}, 2.0));
  bench.feed(clicks({9600}, 19200), 512, 8.0, true);
  REQUIRE(bench.onsets.size() == 1);
  // Атака за 9600 сэмплов (0.2 с) до конца отсчёта — за 0.4 с шкалы до 8 с.
  const double place =
      bench.onsets[0].blockPosition + bench.onsets[0].offset / kSampleRate * 2.0;
  CHECK(place == doctest::Approx(7.6).epsilon(1e-3));
}

TEST_CASE("Поиск атак: атака в первом блоке запуска измеряется") {
  Bench bench;
  bench.feed(clicks({4000, 12000}, 14400), 4800);
  REQUIRE(bench.onsets.size() == 2);
  CHECK(std::abs(startOf(bench.onsets[0]) - 4000.0) < kStartTolerance);
  CHECK(std::abs(startOf(bench.onsets[1]) - 12000.0) < kStartTolerance);
  CHECK_FALSE(bench.onsets[0].beforeStart);
}

TEST_CASE("Поиск атак: атака за 15 мс до конца отсчёта — за 15 мс до позиции начала") {
  Bench bench(false);
  REQUIRE(bench.transport.start(momentOf(0), 0.0, Compensation{},
                                CountIn{.samples = 19200, .sampleRate = kSampleRate}, 1.0));
  bench.feed(clicks({19200 - 720}, 19200 + 4800), 512, 0.0, true);
  REQUIRE_FALSE(bench.onsets.empty());
  const AudioOnset &onset = bench.onsets[0];
  CHECK(onset.beforeStart);
  CHECK(std::abs((onset.blockPosition * kSampleRate + onset.offset) - (-720.0)) <
        kStartTolerance);
}

TEST_CASE("Поиск атак: атака на 300-м сэмпле первого блока — на 6.25 мс") {
  Bench bench;
  bench.feed(clicks({300}, 9600), 512);
  REQUIRE(bench.onsets.size() == 1);
  CHECK(std::abs(startOf(bench.onsets[0]) - 300.0) < kStartTolerance);
}

TEST_CASE("Поиск атак: атака через 2 мс после Play находится — поток идёт и до Play") {
  // Поиск слушает вход и на остановке: до Play — тишина, щелчок — через 100
  // сэмплов после первого блока запуска на 9600.
  Bench bench(false);
  REQUIRE(bench.transport.start(momentOf(9600), 0.0, Compensation{}));
  bench.feed(clicks({9700}, 19200), 480);
  REQUIRE(bench.onsets.size() == 1);
  CHECK(std::abs(startOf(bench.onsets[0], 0.2) - 100.0) < kStartTolerance);
  CHECK_FALSE(bench.onsets[0].beforeStart);
  CHECK(bench.onsets[0].run == momentOf(9600));
}

TEST_CASE("Поиск атак: звон до Play атакой на старте не становится") {
  // Струна 196 Гц звенит с ноты за 2 с до Play и в момент Play ещё громче
  // порога тишины; через 20 мс после Play — щелчок. Атака струны старше
  // секунды и запуска не ждёт.
  constexpr std::size_t kPlay = 96000;
  std::vector<float> signal = clicks({kPlay + 960}, kPlay + 9600);
  for (std::size_t i = 0; i < signal.size(); ++i) {
    const double t = static_cast<double>(i) / kSampleRate;
    signal[i] += static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 196.0 * t) *
                                    std::exp(-t / 1.0));
  }
  Bench bench(false);
  REQUIRE(bench.transport.start(momentOf(kPlay), 0.0, Compensation{}));
  bench.feed(signal, 480);
  REQUIRE(bench.onsets.size() == 1);
  CHECK(std::abs(startOf(bench.onsets[0], 2.0) - 960.0) < kStartTolerance);
  CHECK_FALSE(bench.onsets[0].beforeStart);
}

TEST_CASE("Поиск атак: без запуска атаки не отдаются") {
  Bench bench(false);
  bench.feed(clicks({4800, 12000}, 24000), 512);
  CHECK(bench.onsets.empty());
  CHECK(bench.lines.empty());
}

TEST_CASE("Поиск атак: атака до Play отдаётся с первым блоком запуска перед позицией "
          "начала") {
  Bench bench(false);
  REQUIRE(bench.transport.start(momentOf(9600), 0.0, Compensation{}));
  bench.feed(clicks({4800, 12000}, 19200), 512);

  // Щелчок на 4800 — до первого блока запуска на 9728: на шкале он перед
  // позицией начала на путь до этого блока.
  REQUIRE(bench.onsets.size() == 2);
  CHECK(bench.onsets[0].beforeStart);
  CHECK(std::abs(startOf(bench.onsets[0]) - (4800.0 - 9728.0)) < kStartTolerance);
  CHECK_FALSE(bench.onsets[1].beforeStart);
  CHECK(std::abs(startOf(bench.onsets[1]) - 12000.0) < kStartTolerance);
  CHECK(bench.logged("onset at"));
}

TEST_CASE("Поиск атак: атака до Play с отсчётом — перед позицией начала на путь до конца "
          "отсчёта") {
  // Отсчёт 19200 сэмплов после Play на 9600, позиция начала 8 с; щелчок за
  // 240 сэмплов (5 мс) до Play.
  Bench bench(false);
  REQUIRE(bench.transport.start(momentOf(9600), 8.0, Compensation{},
                                CountIn{.samples = 19200, .sampleRate = kSampleRate}, 1.0));
  bench.feed(clicks({9360}, 9600 + 19200), 480, 8.0, true);
  REQUIRE(bench.onsets.size() == 1);
  CHECK(bench.onsets[0].beforeStart);
  CHECK(std::abs(startOf(bench.onsets[0], 8.0) - (9360.0 - 9600.0 - 19200.0)) <
        kStartTolerance);
}

TEST_CASE("Поиск атак: после остановки атаки не отдаются") {
  Bench bench;
  const std::vector<float> signal = clicks({4800, 12000}, 19200);
  bench.feed(std::vector(signal.begin(), signal.begin() + 9600), 512);
  REQUIRE(bench.onsets.size() == 1);

  // Блоки, взятые до остановки, но поданные после, тоже.
  REQUIRE(bench.transport.stop());
  bench.lines.clear();
  bench.feed(signal, 512);
  CHECK(bench.onsets.size() == 1);
  CHECK(bench.lines.empty());
}

TEST_CASE("Поиск атак: блок, взятый до нового начала, — звук до Play нового запуска") {
  // Остановка и запуск без перерыва в звуке: блок 9 (4608–5119) взят до
  // нового начала на 5120, а подан поиску после него. В нём щелчок, а через
  // 30 мс — второй щелчок, на 6240.
  const std::vector<float> signal = clicks({4800, 6240}, 9600);
  constexpr std::size_t kBlock = 512;

  Bench restarted;
  for (std::size_t at = 0; at < 9 * kBlock; at += kBlock)
    restarted.feedBlock(signal, at, kBlock);
  restarted.restart(5120);
  for (std::size_t at = 9 * kBlock; at < signal.size(); at += kBlock)
    restarted.feedBlock(signal, at, kBlock);

  // Поток не прерывался: щелчок 4800 — атака до Play нового запуска, за 320
  // сэмплов до позиции начала, а щелчок 6240 — в её мёртвом времени.
  REQUIRE(restarted.onsets.size() == 1);
  CHECK(restarted.onsets[0].beforeStart);
  CHECK(restarted.onsets[0].run == momentOf(5120));
  CHECK(std::abs(startOf(restarted.onsets[0]) - (-320.0)) < kStartTolerance);
}

TEST_CASE("Поиск атак: поток не прерывается на новом запуске") {
  Bench bench;
  bench.feed(clicks({4800}, 9600), 512);
  REQUIRE(bench.onsets.size() == 1);

  bench.restart(9600);
  bench.onsets.clear();
  // Атаки нового запуска — на своём месте, в том числе в первом блоке.
  bench.feed(clicks({4000, 12000}, 14400), 4800, 1.0, false, 9600);
  REQUIRE(bench.onsets.size() == 2);
  CHECK(std::abs(startOf(bench.onsets[0], 1.0) - 4000.0) < kStartTolerance);
  CHECK(std::abs(startOf(bench.onsets[1], 1.0) - 12000.0) < kStartTolerance);
  CHECK(bench.onsets[0].run == momentOf(9600));
}

TEST_CASE("Поиск атак: пропавший звук занимает время") {
  // Мёртвое время 40 мс. Щелчок на 4800 и второй через 45 мс, на 6960; блок
  // 5120–5631 пропал. Пропуск отсчитан как время: второй щелчок — после
  // мёртвого времени. Склей детектор блоки, второй щелчок оказался бы через
  // 34 мс после первого — в мёртвом времени.
  Bench bench;
  bench.feed(clicks({4800, 6960}, 9600), 512, 0.0, false, 0, {10});
  REQUIRE(bench.onsets.size() == 2);
  CHECK(std::abs(startOf(bench.onsets[0]) - 4800.0) < kStartTolerance);
  CHECK(std::abs(startOf(bench.onsets[1]) - 6960.0) < kStartTolerance);
  CHECK(bench.onsets[1].blockPosition == doctest::Approx(6656.0 / kSampleRate));
}

TEST_CASE("Поиск атак: смена частоты — новый детектор в том же запуске") {
  Bench bench;
  bench.feed(clicks({4800}, 9600), 512);
  REQUIRE(bench.onsets.size() == 1);
  bench.onsets.clear();

  constexpr double kOtherRate = 44100.0;
  bench.feed(clicks({4410, 13230}, 17640), 441, 1.0, false, 9600, {}, kOtherRate);
  REQUIRE(bench.onsets.size() == 2);
  CHECK(bench.onsets[0].sampleRate == kOtherRate);
  CHECK(std::abs(startOf(bench.onsets[0], 1.0, kOtherRate) - 4410.0) < kStartTolerance);
  CHECK(std::abs(startOf(bench.onsets[1], 1.0, kOtherRate) - 13230.0) < kStartTolerance);
}
