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

TEST_CASE("Поиск атак: при count-in позиция стоит, и атаки отбрасываются") {
  Bench bench;
  bench.feed(clicks({4800, 12000}, 19200), 512, 8.0, true);
  CHECK(bench.onsets.empty());
  CHECK(bench.logged("onset dropped: position 8.000000 is not moving"));

  // Позиция пошла: атаки берутся.
  bench.feed(clicks({4800}, 9600), 512, 8.0, false, 19200);
  REQUIRE(bench.onsets.size() == 1);
  CHECK(std::abs(startOf(bench.onsets[0], 8.0) - 4800.0) < kStartTolerance);
}

TEST_CASE("Поиск атак: атака в первом блоке запуска отбрасывается") {
  // У первого блока нет прошлого: позиция ещё не сдвигалась.
  Bench bench;
  bench.feed(clicks({4000, 12000}, 14400), 4800);
  REQUIRE(bench.onsets.size() == 1);
  CHECK(std::abs(startOf(bench.onsets[0]) - 12000.0) < kStartTolerance);
  CHECK(bench.logged("onset dropped: position 0.000000 is not moving"));
}

TEST_CASE("Поиск атак: без запуска транспорта блоки не разбираются") {
  Bench bench(false);
  bench.feed(clicks({4800, 12000}, 24000), 512);
  CHECK(bench.onsets.empty());
  CHECK(bench.lines.empty());
}

TEST_CASE("Поиск атак: блоки, взятые до начала запуска, не разбираются") {
  Bench bench(false);
  REQUIRE(bench.transport.start(momentOf(9600), 0.0, Compensation{}));
  bench.feed(clicks({4800, 12000}, 19200), 512);

  // Щелчок на 4800 — в блоке до начала: ни атаки, ни строки журнала. Первый
  // блок запуска — на 9728.
  REQUIRE(bench.onsets.size() == 1);
  CHECK(std::abs(startOf(bench.onsets[0]) - 12000.0) < kStartTolerance);
  CHECK_FALSE(bench.logged("onset dropped"));
}

TEST_CASE("Поиск атак: после остановки блоки не разбираются") {
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

TEST_CASE("Поиск атак: блок, взятый до нового начала, не входит в поток нового запуска") {
  // Остановка и запуск без перерыва в звуке: блок 9 (4608–5119) взят до
  // нового начала на 5120, а подан поиску после него. В нём щелчок, а через
  // 30 мс, в мёртвом времени после него, — щелчок нового запуска на 6240.
  const std::vector<float> signal = clicks({4800, 6240}, 9600);
  constexpr std::size_t kBlock = 512;

  Bench restarted;
  for (std::size_t at = 0; at < 9 * kBlock; at += kBlock)
    restarted.feedBlock(signal, at, kBlock);
  restarted.restart(5120);
  for (std::size_t at = 9 * kBlock; at < signal.size(); at += kBlock)
    restarted.feedBlock(signal, at, kBlock);

  // Тот же запуск без блока 9.
  Bench clean(false);
  REQUIRE(clean.transport.start(momentOf(5120), 0.0, Compensation{}));
  for (std::size_t at = 10 * kBlock; at < signal.size(); at += kBlock)
    clean.feedBlock(signal, at, kBlock);

  // Щелчок блока 9 атаки не дал, а щелчок нового запуска стоит там же, где
  // без блока 9: блок 9 детектор не видел.
  REQUIRE(clean.onsets.size() == 1);
  REQUIRE(restarted.onsets.size() == 1);
  CHECK(restarted.onsets[0].blockPosition == clean.onsets[0].blockPosition);
  CHECK(restarted.onsets[0].offset == clean.onsets[0].offset);
  CHECK(std::abs(startOf(restarted.onsets[0]) - 6240.0) < kStartTolerance);
  CHECK_FALSE(restarted.logged("onset dropped"));
}

TEST_CASE("Поиск атак: новый запуск забывает прошлый звук") {
  Bench bench;
  bench.feed(clicks({4800}, 9600), 512);
  REQUIRE(bench.onsets.size() == 1);

  bench.restart(9600);
  bench.onsets.clear();
  // Первый блок нового запуска снова без прошлого: его атака отбрасывается,
  // хотя позиция ушла от последнего блока прошлого запуска.
  bench.feed(clicks({4000, 12000}, 14400), 4800, 1.0, false, 9600);
  REQUIRE(bench.onsets.size() == 1);
  CHECK(std::abs(startOf(bench.onsets[0], 1.0) - 12000.0) < kStartTolerance);
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
