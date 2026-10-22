#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/hit_window.hpp"
#include "training/core/input/midi_receiver.hpp"
#include "training/core/settings.hpp"
#include "training/core/training/midi.hpp"
#include "training/core/training/state/note.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/reaper/mode.hpp"
#include "training/reaper/run_timeline.hpp"
#include "training/render/main/bar_rows.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using training::core::HitWindow;
using training::core::Interval;
using training::core::training::state::Beat;
using MidiNote = training::core::input::MidiReceiver::Note;
using training::core::Moment;
using training::core::Settings;
using training::core::training::state::Note;
using training::reaper::CountIn;
using training::reaper::Mode;
using training::reaper::Position;
using training::reaper::ProjectLoop;
using training::reaper::RunStart;
using training::reaper::RunTimeline;
using training::render::BarRow;
using training::render::BarRows;
using training::test::secondsOf;
using training::test::spanOf;
using training::test::SteadyTimeline;
using training::test::timeOf;
using Trainer = training::core::training::Midi;

namespace {

constexpr double kSampleRate = 48000.0;

/// Окно попадания тестов: 0 ± 10 мс.
constexpr HitWindow kWindow = *HitWindow::create(Interval(), 10 * Interval::kMillisecond);

/// Настройки ядра тестов: окно попадания `kWindow`, порог тишины −50 dBFS.
const Settings kSettings{kWindow, -50.0};

/// Карта темпа проекта тестов: 120 BPM, 4/4, удар — 0.5 с, такт — 2 с.
const SteadyTimeline &project() {
  static const SteadyTimeline steady(120.0);
  return steady;
}

/// Отсчёт `samples` сэмплов при 48 кГц; по тактам — `bars` секунд.
CountIn countIn(std::uint64_t samples, double bars = 0.0) {
  return CountIn{.samples = samples, .barsLength = spanOf(bars), .sampleRate = kSampleRate};
}

/// Карта запуска с позиции `position` секунд проекта: отсчёт начинается на
/// 100 с времени ядра, и T0 — 100 с плюс длина отсчёта.
struct Run {
  RunTimeline timeline{project()};

  explicit Run(double position, const CountIn &count = {}, double rate = 1.0,
               std::optional<std::int64_t> leadInEnd = std::nullopt,
               const SteadyTimeline &map = project())
      : timeline(map) {
    timeline.start(RunStart{.start = {.position = timeOf(position), .rate = rate},
                            .time = timeOf(100.0) + count.length()},
                   count, leadInEnd, kSampleRate);
  }
};

/// Позиция в такте запуска и доле.
void checkPosition(const Position &at, std::int64_t bar, double beat) {
  CHECK(at.bar == bar);
  CHECK(at.beat == doctest::Approx(beat));
}

/// Подпись такта запуска `bar` карты `run` (`RunTimeline::barAt`).
std::string nameOf(const Run &run, std::int64_t bar) {
  return run.timeline.barAt(bar, Mode::Quarters).name();
}

/// Передаёт тренировке такты запуска карты `run`, как связка в начале
/// запуска: предтакт, первый такт запуска и `more` тактов после него.
void sendBars(const Run &run, Trainer &trainer, std::int64_t more = 1) {
  const std::int64_t first = run.timeline.firstBar();
  for (std::int64_t bar = first - 1; bar <= first + more; ++bar)
    trainer.addBar(run.timeline.barAt(bar, Mode::Quarters));
}

/// Строки тактов тренировки `trainer` при слышимой позиции `heard`.
std::vector<training::render::BarView> rowsOf(const Trainer &trainer, Moment heard) {
  BarRows rows;
  rows.setBars(trainer.result().timeline, heard, true, kSettings.window());
  std::vector<training::render::BarView> views;
  for (const BarRow &row : rows.rows())
    views.push_back(row.view());
  return views;
}

} // namespace

TEST_CASE("Карта запуска: без отсчёта T0 — на позиции начала") {
  const Run run(4.0);
  checkPosition(run.timeline.positionAt(timeOf(100.0)), 2, 0.0);
  checkPosition(run.timeline.positionAt(timeOf(100.75)), 2, 1.5);
  CHECK(secondsOf(run.timeline.timeAt({.bar = 2, .beat = 1.5})) == doctest::Approx(100.75));
}

TEST_CASE("Карта запуска: время до T0 — перед позицией начала") {
  // Нота до Play: 0.4 с до T0 — 3.6 с проекта.
  const Run run(4.0);
  checkPosition(run.timeline.positionAt(timeOf(99.6)), 1, 3.2);
}

TEST_CASE("Карта запуска: связка — первый отрезок сразу, блоки до T0 не считаются") {
  RunTimeline timeline(project());
  timeline.start(
      RunStart{.start = {.position = timeOf(4.0), .rate = 1.0}, .time = timeOf(100.0)}, {},
      std::nullopt, kSampleRate);
  checkPosition(timeline.positionAt(timeOf(100.5)), 2, 1.0);
  CHECK_FALSE(timeline.addBlock(timeOf(99.0), {.position = timeOf(20.0), .rate = 1.0}));
  checkPosition(timeline.positionAt(timeOf(100.5)), 2, 1.0);
}

TEST_CASE("Карта запуска: отсчёт кончается в T0") {
  // Отсчёт 19200 сэмплов — 0.4 с — перед позицией 8 с: начало отсчёта на
  // 100 с — за 0.4 с до неё.
  Run run(8.0, countIn(19200));
  checkPosition(run.timeline.positionAt(timeOf(100.4)), 4, 0.0);
  checkPosition(run.timeline.positionAt(timeOf(100.0)), 3, 3.2);
  // Блоки отсчёта REAPER держит на позиции начала: карту они не меняют.
  run.timeline.addBlock(timeOf(100.2), {.position = timeOf(8.0), .rate = 1.0});
  checkPosition(run.timeline.positionAt(timeOf(100.0)), 3, 3.2);
}

TEST_CASE("Карта запуска: путь по отсчёту — по скорости запуска") {
  // Скорость 2: 0.4 реальной секунды отсчёта — 0.8 с проекта.
  const Run run(8.0, countIn(19200), 2.0);
  checkPosition(run.timeline.positionAt(timeOf(100.0)), 3, 2.4);
}

TEST_CASE("Карта запуска: новый проход петли — такты с новыми номерами") {
  // Петля — такты проекта 4–5 (8–12 с), запуск с её начала.
  Run run(8.0);
  run.timeline.addBlock(timeOf(100.0), {.position = timeOf(8.0), .rate = 1.0});
  run.timeline.addBlock(timeOf(102.0), {.position = timeOf(10.0), .rate = 1.0});
  run.timeline.addBlock(timeOf(104.0), {.position = timeOf(8.0), .rate = 1.0});

  checkPosition(run.timeline.positionAt(timeOf(103.9)), 5, 3.8);
  checkPosition(run.timeline.positionAt(timeOf(104.0)), 6, 0.0);
  checkPosition(run.timeline.positionAt(timeOf(107.0)), 7, 2.0);
  CHECK(secondsOf(run.timeline.timeAt({.bar = 6, .beat = 0.0})) == doctest::Approx(104.0));
  CHECK(secondsOf(run.timeline.timeAt({.bar = 5, .beat = 0.0})) == doctest::Approx(102.0));
  CHECK(run.timeline.barOf(6).index == 6);
  CHECK(run.timeline.barOf(6).meter.beats == 4);

  // На экране такт запуска — такт проекта.
  CHECK(nameOf(run, 4) == "5");
  CHECK(nameOf(run, 6) == "5");
  CHECK(nameOf(run, 7) == "6");
}

TEST_CASE("Карта запуска: перемотка вперёд — такты после последнего") {
  Run run(8.0);
  CHECK(run.timeline.addBlock(timeOf(102.0), {.position = timeOf(20.0), .rate = 1.0}) == 5);
  checkPosition(run.timeline.positionAt(timeOf(102.5)), 5, 1.0);
  CHECK(nameOf(run, 5) == "11");
}

TEST_CASE("Карта запуска: удержание после перемотки — начало на последнем удержанном блоке") {
  // Перемотка на 20 с: REAPER держит позицию два блока, звук идёт со второго.
  Run run(8.0);
  const double block = 512.0 / kSampleRate;
  CHECK(run.timeline.addBlock(timeOf(102.0), {.position = timeOf(20.0), .rate = 1.0}) == 5);
  CHECK(run.timeline.addBlock(timeOf(102.0 + block),
                              {.position = timeOf(20.0), .rate = 1.0}) == 5);
  CHECK_FALSE(run.timeline.addBlock(timeOf(102.0 + 2.0 * block),
                                    {.position = timeOf(20.0 + block), .rate = 1.0}));
  checkPosition(run.timeline.positionAt(timeOf(102.5 + block)), 5, 1.0);

  // Такт — с первого блока перехода, доли — с места, откуда пошёл звук.
  const training::core::training::state::Bar bar = run.timeline.barAt(5, Mode::Quarters);
  CHECK(nameOf(run, 5) == "11");
  CHECK(secondsOf(bar.start()) == doctest::Approx(102.0));
  CHECK(secondsOf(bar.divisions().begin()->second) == doctest::Approx(102.0 + block));

  // Позиция пошла: новая перемотка на то же место — новый отрезок.
  CHECK(run.timeline.addBlock(timeOf(103.0), {.position = timeOf(20.0), .rate = 1.0}) == 6);
}

TEST_CASE("Карта запуска: петля известна заранее — такт после её конца — её начало") {
  // Петля — такты проекта 4–5 (8–12 с), запуск с её начала.
  Run run(8.0);
  CHECK_FALSE(run.timeline.setLoop(ProjectLoop{.start = timeOf(8.0), .end = timeOf(12.0)}));
  run.timeline.predict(timeOf(105.0));
  CHECK(nameOf(run, 6) == "5");
  CHECK(secondsOf(run.timeline.timeAt({.bar = 6, .beat = 0.0})) == doctest::Approx(104.0));

  // Блок перехода подтверждает предсказание: такты прежние.
  CHECK_FALSE(run.timeline.addBlock(timeOf(104.01), {.position = timeOf(8.01), .rate = 1.0}));
  checkPosition(run.timeline.positionAt(timeOf(104.5)), 6, 1.0);
}

TEST_CASE("Карта запуска: перехода петли не было — предсказание отменяется") {
  Run run(8.0);
  CHECK_FALSE(run.timeline.setLoop(ProjectLoop{.start = timeOf(8.0), .end = timeOf(12.0)}));
  run.timeline.predict(timeOf(105.0));

  // Повтор выключили до перехода: воспроизведение идёт дальше 12 с.
  CHECK(run.timeline.addBlock(timeOf(104.01), {.position = timeOf(12.01), .rate = 1.0}) == 6);
  CHECK(nameOf(run, 6) == "7");
  checkPosition(run.timeline.positionAt(timeOf(104.5)), 6, 1.0);
}

TEST_CASE("Карта запуска: смена петли отменяет предсказанные такты") {
  Run run(8.0);
  CHECK_FALSE(run.timeline.setLoop(ProjectLoop{.start = timeOf(8.0), .end = timeOf(12.0)}));
  run.timeline.predict(timeOf(105.0));
  CHECK(run.timeline.setLoop(std::nullopt) == 6);
  CHECK(nameOf(run, 6) == "7");
  CHECK_FALSE(run.timeline.setLoop(std::nullopt));
}

TEST_CASE("Карта запуска: смена скорости — номера тактов продолжаются") {
  Run run(8.0);
  // Скорость сменилась в такте 4: такты с 5-го пора передать заново.
  CHECK(run.timeline.addBlock(timeOf(101.0), {.position = timeOf(9.0), .rate = 2.0}) == 5);
  // С 101 с проект идёт вдвое быстрее: на 102 с — 11 с, такт 5, доля 2.
  checkPosition(run.timeline.positionAt(timeOf(102.0)), 5, 2.0);
  checkPosition(run.timeline.positionAt(timeOf(100.5)), 4, 1.0);
  CHECK(nameOf(run, 5) == "6");
}

TEST_CASE("Карта запуска: такты до конца затакта — −1, −2, после петли — номер такта") {
  // Затакт кончается тактом проекта 4 (номер 5).
  Run run(4.0, {}, 1.0, 4);
  CHECK(nameOf(run, 2) == "−"
                          "2");
  CHECK(nameOf(run, 3) == "−"
                          "1");
  CHECK(nameOf(run, 4) == "5");

  // Петля вернула воспроизведение к такту проекта 2: это уже не затакт.
  run.timeline.addBlock(timeOf(104.0), {.position = timeOf(4.0), .rate = 1.0});
  const std::int64_t again = run.timeline.positionAt(timeOf(104.5)).bar;
  CHECK(nameOf(run, again) == "3");
}

TEST_CASE("Такт для ядра: начало — первый удар, доли — по режиму сетки") {
  const Run run(8.0);
  const training::core::training::state::Bar bar = run.timeline.barAt(4, Mode::Sixteenths);
  CHECK(bar.name() == "5");
  CHECK(secondsOf(bar.start()) == doctest::Approx(100.0));
  CHECK(bar.meter().beats == 4);
  REQUIRE(bar.divisions().size() == 16);
  CHECK(secondsOf(bar.divisions().at(Beat(1.25))) == doctest::Approx(100.625));
  CHECK(bar.notes().empty());
}

TEST_CASE("Такт для ядра: после петли с середины такта — с места начала отрезка") {
  // Петля с 9.0 с — удара 2 такта проекта 4 — на 104 с.
  Run run(8.0);
  const std::optional<std::int64_t> again =
      run.timeline.addBlock(timeOf(104.0), {.position = timeOf(9.0), .rate = 1.0});
  REQUIRE(again);
  const training::core::training::state::Bar bar = run.timeline.barAt(*again, Mode::Quarters);
  CHECK(bar.name() == "5");
  CHECK(secondsOf(bar.start()) == doctest::Approx(104.0));
  REQUIRE(bar.divisions().size() == 2);
  CHECK(secondsOf(bar.divisions().begin()->second) == doctest::Approx(104.0));
  CHECK(bar.divisions().begin()->first == Beat(2.0));
}

TEST_CASE("Карта запуска: первый такт запуска — такт начала отсчёта по тактам") {
  // Отсчёт 1 такт по тактам — 2 с — перед позицией 4.0 с: такт запуска 1.
  const Run counted(4.0, countIn(96256, 2.0));
  CHECK(counted.timeline.firstBar() == 1);
  // Без отсчёта — такт позиции начала.
  const Run plain(4.0);
  CHECK(plain.timeline.firstBar() == 2);
}

TEST_CASE("Карта запуска: пре-ролл на полблока раньше такта — первый такт после границы") {
  // Пре-ролл 1 такт к позиции 8 с: REAPER начинает на 256 сэмплов раньше 6 с.
  const Run preroll(6.0 - 256.0 / kSampleRate, {}, 1.0, 4);
  CHECK(preroll.timeline.firstBar() == 3);
  CHECK(nameOf(preroll, 3) == "−"
                              "1");
}

TEST_CASE("Карта запуска: слышимая позиция до первого клика — по сетке отсчёта, как есть") {
  // Отсчёт по тактам 2 с перед позицией 4 с, отсчёт REAPER — длиннее на
  // остаток до целого блока: начало отсчёта на 100 с — за 2.005333 с до T0.
  const Run run(4.0, countIn(96256, 2.0));
  CHECK(secondsOf(run.timeline.heard({.position = timeOf(1.994667), .rate = 1.0},
                                     timeOf(100.0))) == doctest::Approx(100.0));
  CHECK(secondsOf(run.timeline.heard({.position = timeOf(2.1), .rate = 1.0}, timeOf(100.1))) ==
        doctest::Approx(100.0 + 96256.0 / kSampleRate - 1.9));
  CHECK(secondsOf(run.timeline.heard({.position = timeOf(1.5), .rate = 1.0}, timeOf(100.1))) ==
        doctest::Approx(100.0 + 96256.0 / kSampleRate - 2.5));
}

TEST_CASE(
    "Карта запуска: после петли позиция прошлого прохода — во времени прошлого прохода") {
  Run run(8.0);
  run.timeline.addBlock(timeOf(104.0), {.position = timeOf(8.0), .rate = 1.0});
  // Конец взятого звука — 104.005 с, а слышно ещё конец первого прохода.
  CHECK(secondsOf(run.timeline.heard({.position = timeOf(11.99), .rate = 1.0},
                                     timeOf(104.005))) == doctest::Approx(103.99));
  CHECK(secondsOf(run.timeline.heard({.position = timeOf(8.01), .rate = 1.0},
                                     timeOf(104.03))) == doctest::Approx(104.01));
}

TEST_CASE("Ноты до Play: с отсчётом нота за 20 мс до первого клика — у первого клика") {
  // Отсчёт 1 такт по тактам — 2 с, у REAPER — 96256 сэмплов, 2.005333 с, —
  // перед запуском с 4.0 с: первый клик — на 2.0 с проекта.
  const Run run(4.0, countIn(96256, 2.0), 1.0, 2);
  Trainer trainer(kSettings.window());
  REQUIRE(trainer.start().has_value());
  sendBars(run, trainer);

  const Moment firstClick = run.timeline.timeAt({.bar = 1, .beat = 0.0});
  CHECK(secondsOf(firstClick) == doctest::Approx(100.0 + 96256.0 / kSampleRate - 2.0));
  // Ноты MIDI идут по времени.
  trainer.receive(std::vector<MidiNote>{{.time = firstClick - spanOf(0.305)},
                                        {.time = firstClick - spanOf(0.020)}});

  // Список тактов: предтакт, такт 1 («−1»), такт 2.
  const training::core::training::state::Timeline timeline = trainer.result().timeline;
  const auto &bars = timeline.bars();
  REQUIRE(bars.size() == 3);
  CHECK(bars[1].name() == "−"
                          "1");
  const std::vector<Note> first = bars[1].notes().at(Beat(0.0));
  REQUIRE(first.size() == 1);
  CHECK(first.front().deviation / Interval::kSecond == doctest::Approx(-0.020));
  // Нота за 305 мс ближе к последнему удару предтакта: она в нём.
  CHECK(bars[0].notes().size() == 1);
}

TEST_CASE(
    "Строки тактов: отсчёт 2 такта при 110 BPM — верхняя строка «−2», такта до неё нет") {
  // Запись с такта 5; отсчёт 209920 сэмплов на 9.7 мс длиннее двух тактов:
  // он начинается в такте перед первым кликом.
  const SteadyTimeline steady(110.0);
  const Moment position = steady.beatTime(16.0);
  const Run run(secondsOf(position), countIn(209920, 8.0 * 60.0 / 110.0), 1.0, 4, steady);
  CHECK(run.timeline.firstBar() == 2);
  Trainer trainer(kSettings.window());
  REQUIRE(trainer.start().has_value());
  sendBars(run, trainer);

  // До первого клика слышимая позиция — в предтакте: строк нет.
  const Moment early = run.timeline.heard(
      {.position = position - spanOf(209920.0 / kSampleRate), .rate = 1.0}, timeOf(100.0));
  CHECK(rowsOf(trainer, early).empty());

  // С первого клика сверху — такт «−2».
  const Moment click = run.timeline.heard(
      {.position = position - spanOf(8.0 * 60.0 / 110.0 - 0.001), .rate = 1.0},
      timeOf(100.02));
  const auto views = rowsOf(trainer, click);
  REQUIRE(views.size() == 1);
  CHECK(views.front().label == "−"
                               "2");
  CHECK(views.front().currentBeat == 0);

  CHECK(rowsOf(trainer, run.timeline.heard({.position = position - spanOf(4.3), .rate = 1.0},
                                           timeOf(100.1)))
            .size() == 1);
}

TEST_CASE("Строки тактов: отсчёт не с начала такта — сверху такт начала отсчёта") {
  // 110 BPM, запись с середины последнего удара такта 5, отсчёт 2 такта: по
  // тактам он начинается на середине последнего удара такта 3.
  const SteadyTimeline steady(110.0);
  const Moment position = steady.beatTime(19.5);
  const Run run(secondsOf(position), countIn(209920, 8.0 * 60.0 / 110.0), 1.0, 4, steady);
  CHECK(run.timeline.firstBar() == 2);
  Trainer trainer(kSettings.window());
  REQUIRE(trainer.start().has_value());
  sendBars(run, trainer);

  // Отсчёт по тактам начинается на середине последнего удара такта «−2», а
  // отсчёт REAPER — на 9.7 мс раньше, в том же ударе: сверху такт «−2» с
  // последним ударом и до начала отсчёта по тактам, и после.
  const Moment early = run.timeline.heard(
      {.position = position - spanOf(209920.0 / kSampleRate), .rate = 1.0}, timeOf(100.0));
  const auto views = rowsOf(trainer, early);
  REQUIRE(views.size() == 1);
  CHECK(views.front().label == "−"
                               "2");
  CHECK(views.front().currentBeat == 3);

  const Moment counted = run.timeline.heard(
      {.position = position - spanOf(8.0 * 60.0 / 110.0 - 0.001), .rate = 1.0},
      timeOf(100.02));
  CHECK(rowsOf(trainer, counted).front().currentBeat == 3);
}
