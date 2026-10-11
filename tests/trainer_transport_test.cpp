#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/trainer/midi_notes.hpp"
#include "training/trainer/transport.hpp"

#include <chrono>
#include <optional>
#include <vector>

using training::grid::BarRow;
using training::grid::HitWindow;
using training::test::SteadyTimeline;
using training::trainer::Compensation;
using training::trainer::CountIn;
using training::trainer::kNoRun;
using training::trainer::MidiEvent;
using training::trainer::MidiNote;
using training::trainer::MidiRead;
using training::trainer::Moment;
using training::trainer::Transport;

using namespace std::chrono_literals;

namespace {

/// Компенсация со звуком 48 кГц: атаки берутся.
constexpr Compensation kCompensation{.audio = 0.010,
                                     .audioSampleRate = 48000.0,
                                     .inputLatency = 0.005,
                                     .midi = 0.020,
                                     .midiSampleRate = 48000.0};

/// Окно попадания тестов: 0 ± 10 мс.
constexpr HitWindow kWindow{.offsetMs = 0.0, .toleranceMs = 10.0};

/// Карта темпа тестов: 120 BPM, 4/4, удар — 0.5 с.
const SteadyTimeline &timeline() {
  static const SteadyTimeline steady(120.0);
  return steady;
}

constexpr int kDevice = 3;

/// Note On устройства `kDevice` в позиции `position`, пришедший `age` секунд
/// шкалы проекта назад.
MidiEvent noteOn(double position, double age = 0.0) {
  return MidiEvent{.position = position,
                   .age = age,
                   .device = kDevice,
                   .loop = 0,
                   .size = 3,
                   .bytes = {0x90, 60, 100}};
}

/// Чтение истории MIDI-входа при идущем запуске с позицией обработки
/// `position`; `runTime` — время запуска при чтении, пусто — неизвестно.
MidiRead reading(double position, std::optional<double> runTime = std::nullopt) {
  return MidiRead{.measuring = true,
                  .device = kDevice,
                  .position = position,
                  .runTime = runTime,
                  .blockLength = 512.0 / 48000.0,
                  .hostTime = std::nullopt,
                  .compensation = 0.0};
}

/// Слышимая позиция `beats` и нота точно в узле этого удара.
void playNote(Transport &transport, double beats) {
  transport.play(beats, 0.0, kWindow, timeline());
  transport.addNote(timeline().timeAt(beats), 1.0, kWindow, timeline());
}

} // namespace

TEST_CASE("Транспорт: запуск начинается и кончается") {
  Transport transport;
  CHECK_FALSE(transport.running());
  CHECK(transport.start(Moment(1s), 0.0, kCompensation));
  CHECK(transport.running());
  CHECK(transport.stop());
  CHECK_FALSE(transport.running());
}

TEST_CASE("Транспорт: начало при идущем запуске и конец без запуска ничего не меняют") {
  Transport transport;
  CHECK_FALSE(transport.stop());

  REQUIRE(transport.start(Moment(1s), 0.0, kCompensation));
  CHECK_FALSE(transport.start(Moment(2s), 0.0, Compensation{}));
  CHECK(transport.running());
  // Момент и компенсация — первого начала.
  CHECK(transport.compensation() == kCompensation);
  CHECK(transport.takesOnset(Moment(1s)));

  REQUIRE(transport.stop());
  CHECK_FALSE(transport.stop());
}

TEST_CASE("Транспорт: компенсация снимается при начале и держится до следующего") {
  Transport transport;
  CHECK(transport.compensation() == Compensation{});

  REQUIRE(transport.start(Moment(1s), 0.0, kCompensation));
  CHECK(transport.compensation() == kCompensation);
  REQUIRE(transport.stop());
  CHECK(transport.compensation() == kCompensation);

  Compensation other = kCompensation;
  other.audio = 0.030;
  REQUIRE(transport.start(Moment(5s), 0.0, other));
  CHECK(transport.compensation() == other);
}

TEST_CASE("Транспорт: строки тактов остаются после остановки, новый запуск их очищает") {
  Transport transport;
  REQUIRE(transport.start(Moment(1s), 0.0, kCompensation));
  playNote(transport, 0.0);
  playNote(transport, 4.0);
  REQUIRE(transport.bars().rows().size() == 2);

  REQUIRE(transport.stop());
  CHECK(transport.bars().rows().size() == 2);

  REQUIRE(transport.start(Moment(10s), 0.0, kCompensation));
  CHECK(transport.bars().rows().empty());
}

TEST_CASE("Транспорт: повторное начало при идущем запуске строки не трогает") {
  Transport transport;
  REQUIRE(transport.start(Moment(1s), 0.0, kCompensation));
  playNote(transport, 0.0);
  REQUIRE_FALSE(transport.start(Moment(2s), 0.0, kCompensation));
  CHECK(transport.bars().rows().size() == 1);
}

TEST_CASE("Атаки звука: без запуска отбрасываются") {
  const Transport transport;
  // Транспорт стоит: атак в строки нет.
  CHECK_FALSE(transport.takesOnset(kNoRun));
  CHECK_FALSE(transport.takesOnset(Moment(500ms)));
}

TEST_CASE("Атаки звука: атаки идущего запуска берутся") {
  Transport transport;
  REQUIRE(transport.start(Moment(10s), 0.0, kCompensation));
  CHECK(transport.takesOnset(Moment(10s)));
}

TEST_CASE("Атаки звука: после остановки отбрасываются") {
  Transport transport;
  REQUIRE(transport.start(Moment(10s), 0.0, kCompensation));
  REQUIRE(transport.stop());
  // И атаки, отданные запуску до остановки: забранное после неё в строки не
  // идёт.
  CHECK_FALSE(transport.takesOnset(Moment(10s)));
}

TEST_CASE("Атаки звука: новый запуск отсекает атаки прошлого") {
  Transport transport;
  REQUIRE(transport.start(Moment(10s), 0.0, kCompensation));
  REQUIRE(transport.stop());
  REQUIRE(transport.start(Moment(20s), 0.0, kCompensation));
  CHECK_FALSE(transport.takesOnset(Moment(10s)));
  CHECK(transport.takesOnset(Moment(20s)));
}

TEST_CASE("Атаки звука: без частоты звука у компенсации отбрасываются") {
  Transport transport;
  Compensation unknown = kCompensation;
  unknown.audioSampleRate = 0.0;
  REQUIRE(transport.start(Moment(10s), 0.0, unknown));
  CHECK_FALSE(transport.takesOnset(Moment(11s)));
}

TEST_CASE("Ноты MIDI: отсчёт запуска кладёт ноты отсчёта перед позицией начала") {
  Transport transport;
  const CountIn countIn{.samples = 192000, .sampleRate = 48000.0};
  REQUIRE(transport.start(Moment(1s), 4.0, kCompensation, countIn, 1.0));
  CHECK(transport.countIn() == countIn);

  // Отсчёт 4 с, нота через 3 с после начала запуска — за 1 с до конца
  // отсчёта; хост поставил ей позицию начала.
  const std::vector<MidiNote> taken =
      transport.midiNotes().take(std::vector{noteOn(4.0, 0.5)}, reading(4.0, 3.5));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(3.0));
  CHECK(taken[0].beforeStart);
}

TEST_CASE("Ноты MIDI: скорость запуска переводит отсчёт в секунды шкалы проекта") {
  Transport transport;
  REQUIRE(transport.start(Moment(1s), 0.0, kCompensation,
                          CountIn{.samples = 96000, .sampleRate = 48000.0}, 2.0));
  // Отсчёт 2 реальные секунды — 4 секунды шкалы при скорости 2.
  const std::vector<MidiNote> taken =
      transport.midiNotes().take(std::vector{noteOn(0.0, 0.0)}, reading(0.0, 3.0));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(-1.0));
}

TEST_CASE("Ноты MIDI: новый запуск кончает мёртвое время прошлого") {
  Transport transport;
  REQUIRE(transport.start(Moment(1s), 2.0, kCompensation));
  REQUIRE(transport.midiNotes().take(std::vector{noteOn(2.600)}, reading(2.7)).size() == 1);

  // Нота через 20 мс после прошлой — в его мёртвом времени, но в новом
  // запуске.
  REQUIRE(transport.stop());
  REQUIRE(transport.start(Moment(5s), 2.5, kCompensation));
  CHECK(transport.midiNotes().take(std::vector{noteOn(2.620)}, reading(2.7)).size() == 1);
}

TEST_CASE("Ноты до Play: нота за 100 мс до запуска — у первого удара, за 400 мс — нет") {
  // Запуск с удара 3 такта 1 — с 1.0 с, без отсчёта.
  Transport transport;
  REQUIRE(transport.start(Moment(1s), 1.0, kCompensation));
  transport.play(2.0, 0.0, kWindow, timeline());
  CHECK(transport.addNote(0.9, 1.0, kWindow, timeline(), true));
  CHECK_FALSE(transport.addNote(0.6, 1.0, kWindow, timeline(), true));

  const BarRow &row = transport.bars().rows().front();
  REQUIRE(row.beats[2].slots[0].deviation);
  CHECK(*row.beats[2].slots[0].deviation == doctest::Approx(-0.100));
  CHECK_FALSE(row.beats[1].slots[0].deviation);
}

TEST_CASE("Ноты до Play: с отсчётом нота за 20 мс до первого клика — у первого клика") {
  // Отсчёт 1 такт — 96256 сэмплов, 2.005333 с — перед запуском с 4.0 с:
  // запуск начался на 1.994667 с шкалы, первый клик — на 2.0 с.
  Transport transport;
  REQUIRE(transport.start(Moment(1s), 4.0, kCompensation,
                          CountIn{.samples = 96256, .barsSeconds = 2.0, .sampleRate = 48000.0},
                          1.0));
  transport.play(timeline().beatsAt(1.994667), 0.0, kWindow, timeline());
  CHECK(transport.addNote(1.98, 1.0, kWindow, timeline(), true));
  CHECK_FALSE(transport.addNote(1.694667, 1.0, kWindow, timeline(), true));

  // Слышимая позиция в остатке округления — начало отсчёта по тактам, первый
  // клик: нота сразу в строке его такта.
  REQUIRE(transport.bars().rows().size() == 1);
  const BarRow &row = transport.bars().rows().front();
  CHECK(row.bar.index == 1);
  REQUIRE(row.beats[0].slots[0].deviation);
  CHECK(*row.beats[0].slots[0].deviation == doctest::Approx(-0.020));
}

TEST_CASE(
    "Строки тактов: отсчёт 2 такта при 110 BPM — верхняя строка «−2», такта до неё нет") {
  // Запись с такта 5; отсчёт 209920 сэмплов на 9.7 мс длиннее двух тактов:
  // он начинается в такте перед первым кликом.
  const SteadyTimeline steady(110.0);
  const double position = steady.timeAt(16.0);
  Transport transport;
  REQUIRE(transport.start(
      Moment(1s), position, kCompensation,
      CountIn{.samples = 209920, .barsSeconds = 8.0 * 60.0 / 110.0, .sampleRate = 48000.0},
      1.0, 4));

  transport.play(steady.beatsAt(position - 209920.0 / 48000.0), 0.0, kWindow, steady);
  REQUIRE(transport.bars().rows().size() == 1);
  CHECK(transport.bars().rows().front().bar.index == 2);
  CHECK(transport.bars().rows().front().leadIn == -2);
  CHECK(transport.bars().currentBeat() == 0);

  transport.play(steady.beatsAt(position - 4.3), 0.0, kWindow, steady);
  CHECK(transport.bars().rows().size() == 1);
}

TEST_CASE("Строки тактов: запуск не на узле — сверху такт, который звучит") {
  // Запуск с 3.7 удара такта 1, режим «четверти»: до узла удара 4 звучит
  // четвёртый удар такта 1.
  Transport transport;
  REQUIRE(transport.start(Moment(1s), timeline().timeAt(3.7), kCompensation));
  transport.play(3.7, 0.0, kWindow, timeline());
  REQUIRE(transport.bars().rows().size() == 1);
  CHECK(transport.bars().rows().front().bar.index == 0);
  CHECK(transport.bars().currentBeat() == 3);
}

TEST_CASE("Строки тактов: отсчёт не с начала такта — сверху такт начала отсчёта") {
  // 110 BPM, запись с последнего удара такта 5, отсчёт 2 такта: по тактам он
  // начинается на последнем ударе такта 3, первый клик — на начале такта 4.
  const SteadyTimeline steady(110.0);
  const double position = steady.timeAt(19.5);
  Transport transport;
  REQUIRE(transport.start(
      Moment(1s), position, kCompensation,
      CountIn{.samples = 209920, .barsSeconds = 8.0 * 60.0 / 110.0, .sampleRate = 48000.0},
      1.0, 4));

  transport.play(steady.beatsAt(position - 209920.0 / 48000.0), 0.0, kWindow, steady);
  REQUIRE(transport.bars().rows().size() == 1);
  CHECK(transport.bars().rows().front().bar.index == 2);
  CHECK(transport.bars().rows().front().leadIn == -2);
  CHECK(transport.bars().currentBeat() == 3);
}

TEST_CASE("Строки тактов: после начала запуска позиция по петле раньше него — как есть") {
  // Петля — такты 1–2, запуск с удара 6 такта 2.
  Transport transport;
  REQUIRE(transport.start(Moment(1s), timeline().timeAt(6.0), kCompensation));
  transport.play(6.0, 8.0, kWindow, timeline());
  transport.play(7.5, 8.0, kWindow, timeline());
  transport.play(0.5, 8.0, kWindow, timeline());
  CHECK(transport.bars().rows().front().bar.index == 0);
}
