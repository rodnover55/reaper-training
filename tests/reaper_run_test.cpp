#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/reaper/run.hpp"
#include "training/trainer/transport.hpp"

#include <cstdint>
#include <optional>

using training::core::HitWindow;
using training::core::Interval;
using training::core::Mode;
using training::core::Moment;
using training::grid::BarRow;
using training::reaper::CountIn;
using training::reaper::Run;
using training::test::secondsOf;
using training::test::spanOf;
using training::test::SteadyTimeline;
using training::test::timeOf;
using training::trainer::AudioOnset;
using training::trainer::Transport;

namespace {

constexpr double kSampleRate = 48000.0;

/// Окно попадания тестов: 0 ± 10 мс.
constexpr HitWindow kWindow;

/// Режим сетки тестов.
constexpr Mode kMode = Mode::Quarters;

/// Отсчёт `samples` сэмплов при 48 кГц; по тактам — `bars` секунд.
CountIn countIn(std::uint64_t samples, double bars = 0.0) {
  return CountIn{.samples = samples, .barsLength = spanOf(bars), .sampleRate = kSampleRate};
}

/// Атака из блока с первым сэмплом `firstSample` запуска, первый блок которого
/// начался на сэмпле `runStartSample`; позиция блока у REAPER — `position`, с.
AudioOnset onsetIn(std::uint64_t firstSample, std::uint64_t runStartSample,
                   double position = 0.0) {
  return AudioOnset{.blockPosition = timeOf(position),
                    .firstSample = firstSample,
                    .runStartSample = runStartSample,
                    .offset = 0.0,
                    .sampleRate = kSampleRate,
                    .levelDb = 0.0,
                    .monotonic = Moment(),
                    .run = Moment()};
}

} // namespace

TEST_CASE("Запуск: начало запуска на шкале — позиция начала минус отсчёт по скорости") {
  CHECK(Run(timeOf(8.0), CountIn{}, 1.0).begin() == timeOf(8.0));
  CHECK(secondsOf(Run(timeOf(8.0), countIn(19200), 1.0).begin()) == doctest::Approx(7.6));
  // Скорость 2: 0.4 реальной секунды отсчёта — 0.8 с шкалы.
  CHECK(secondsOf(Run(timeOf(8.0), countIn(19200), 2.0).begin()) == doctest::Approx(7.2));
  CHECK(Run(timeOf(8.0), countIn(19200), 2.0).countInLength() / Interval::kSecond ==
        doctest::Approx(0.8));
}

TEST_CASE("Запуск: блок отсчёта — перед позицией начала на свой путь до конца отсчёта") {
  // Отсчёт 19200 сэмплов (0.4 с) перед запуском с 8 с: REAPER держит позицию
  // блоков отсчёта на 8 с.
  const Run run(timeOf(8.0), countIn(19200), 1.0);
  const std::optional<Moment> place = run.blockPlace(onsetIn(4608, 0, 8.0));
  REQUIRE(place);
  CHECK(secondsOf(*place) == doctest::Approx(7.6 + 4608.0 / kSampleRate));

  // Блок, которым отсчёт кончился, — уже на позиции, которую дал REAPER.
  CHECK_FALSE(run.blockPlace(onsetIn(19200, 0, 8.0)));
}

TEST_CASE("Запуск: место блока отсчёта учитывает скорость запуска") {
  // Скорость 2: блок за 9600 сэмплов (0.2 с) до конца отсчёта — за 0.4 с шкалы
  // до 8 с.
  const Run run(timeOf(8.0), countIn(19200), 2.0);
  const std::optional<Moment> place = run.blockPlace(onsetIn(9600, 0, 8.0));
  REQUIRE(place);
  CHECK(secondsOf(*place) == doctest::Approx(7.6));
}

TEST_CASE("Запуск: блок за 15 мс до конца отсчёта — за 15 мс до позиции начала") {
  const Run run(Moment(), countIn(19200), 1.0);
  const std::optional<Moment> place = run.blockPlace(onsetIn(19200 - 720, 0));
  REQUIRE(place);
  CHECK(secondsOf(*place) * kSampleRate == doctest::Approx(-720.0));
}

TEST_CASE("Запуск: блок до Play — перед позицией начала на путь до первого блока запуска") {
  // Без отсчёта: блок 4608 — до первого блока запуска на 9728.
  const Run run(Moment(), CountIn{}, 1.0);
  const std::optional<Moment> place = run.blockPlace(onsetIn(4608, 9728));
  REQUIRE(place);
  CHECK(secondsOf(*place) * kSampleRate == doctest::Approx(4608.0 - 9728.0));

  // Первый блок запуска — на позиции, которую дал REAPER.
  CHECK_FALSE(run.blockPlace(onsetIn(9728, 9728, 0.0)));
}

TEST_CASE("Запуск: блок до Play с отсчётом — перед позицией начала на путь до конца "
          "отсчёта") {
  // Отсчёт 19200 сэмплов после первого блока запуска на 9600, позиция начала
  // 8 с; блок за 480 сэмплов до Play.
  const Run run(timeOf(8.0), countIn(19200), 1.0);
  const std::optional<Moment> place = run.blockPlace(onsetIn(9120, 9600));
  REQUIRE(place);
  CHECK((secondsOf(*place) - 8.0) * kSampleRate == doctest::Approx(9120.0 - 9600.0 - 19200.0));
}

TEST_CASE("Запуск: слышимая позиция до начала отсчёта по тактам — это начало") {
  // Отсчёт по тактам 2 с перед позицией 4 с, отсчёт REAPER — длиннее на
  // остаток до целого блока.
  Run run(timeOf(4.0), countIn(96256, 2.0), 1.0);
  CHECK(run.heard(timeOf(1.994667)) == timeOf(2.0));
  CHECK(run.heard(timeOf(2.1)) == timeOf(2.1));
  // Дальше позиция раньше начала — по петле или перемотке — остаётся как
  // есть.
  CHECK(run.heard(timeOf(1.5)) == timeOf(1.5));
}

TEST_CASE("Запуск: без отсчёта слышимая позиция с позиции начала — как есть") {
  const SteadyTimeline steady(120.0);
  Run run(steady.beatTime(6.0), CountIn{}, 1.0);
  CHECK(run.heard(steady.beatTime(6.0)) == steady.beatTime(6.0));
  CHECK(run.heard(steady.beatTime(0.5)) == steady.beatTime(0.5));
}

TEST_CASE("Запуск: до запуска слышимая позиция — как есть") {
  Run run;
  CHECK(run.heard(timeOf(1.0)) == timeOf(1.0));
}

TEST_CASE("Ноты до Play: с отсчётом нота за 20 мс до первого клика — у первого клика") {
  // Отсчёт 1 такт — 96256 сэмплов, 2.005333 с — перед запуском с 4.0 с:
  // запуск начался на 1.994667 с шкалы, первый клик — на 2.0 с.
  const SteadyTimeline steady(120.0);
  Run run(timeOf(4.0), countIn(96256, 2.0), 1.0);
  Transport transport;
  REQUIRE(transport.start(timeOf(1.0), run.begin()));
  transport.play(run.heard(timeOf(1.994667)), Interval(), kMode, kWindow, steady);
  CHECK(transport.addNote(timeOf(1.98), 1.0, kMode, kWindow, steady, true));
  CHECK_FALSE(transport.addNote(timeOf(1.694667), 1.0, kMode, kWindow, steady, true));

  // Слышимая позиция в остатке округления — начало отсчёта по тактам, первый
  // клик: нота сразу в строке его такта.
  REQUIRE(transport.bars().rows().size() == 1);
  const BarRow &row = transport.bars().rows().front();
  CHECK(row.bar.index == 1);
  REQUIRE(row.beats[0].slots[0].deviation);
  CHECK(*row.beats[0].slots[0].deviation / Interval::kSecond == doctest::Approx(-0.020));
}

TEST_CASE(
    "Строки тактов: отсчёт 2 такта при 110 BPM — верхняя строка «−2», такта до неё нет") {
  // Запись с такта 5; отсчёт 209920 сэмплов на 9.7 мс длиннее двух тактов:
  // он начинается в такте перед первым кликом.
  const SteadyTimeline steady(110.0);
  const Moment position = steady.beatTime(16.0);
  Run run(position, countIn(209920, 8.0 * 60.0 / 110.0), 1.0);
  Transport transport;
  REQUIRE(transport.start(timeOf(1.0), run.begin(), 4));

  transport.play(run.heard(position - spanOf(209920.0 / kSampleRate)), Interval(), kMode,
                 kWindow, steady);
  REQUIRE(transport.bars().rows().size() == 1);
  CHECK(transport.bars().rows().front().bar.index == 2);
  CHECK(transport.bars().rows().front().leadIn == -2);
  CHECK(transport.bars().currentBeat() == 0);

  transport.play(run.heard(position - spanOf(4.3)), Interval(), kMode, kWindow, steady);
  CHECK(transport.bars().rows().size() == 1);
}

TEST_CASE("Строки тактов: отсчёт не с начала такта — сверху такт начала отсчёта") {
  // 110 BPM, запись с последнего удара такта 5, отсчёт 2 такта: по тактам он
  // начинается на последнем ударе такта 3, первый клик — на начале такта 4.
  const SteadyTimeline steady(110.0);
  const Moment position = steady.beatTime(19.5);
  Run run(position, countIn(209920, 8.0 * 60.0 / 110.0), 1.0);
  Transport transport;
  REQUIRE(transport.start(timeOf(1.0), run.begin(), 4));

  transport.play(run.heard(position - spanOf(209920.0 / kSampleRate)), Interval(), kMode,
                 kWindow, steady);
  REQUIRE(transport.bars().rows().size() == 1);
  CHECK(transport.bars().rows().front().bar.index == 2);
  CHECK(transport.bars().rows().front().leadIn == -2);
  CHECK(transport.bars().currentBeat() == 3);
}
