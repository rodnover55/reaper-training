#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/trainer/midi_notes.hpp"
#include "training/trainer/transport.hpp"

#include <chrono>
#include <vector>

using training::grid::HitWindow;
using training::test::SteadyTimeline;
using training::trainer::Compensation;
using training::trainer::MidiEvent;
using training::trainer::MidiNote;
using training::trainer::MidiNotes;
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
/// `position`.
MidiRead reading(double position) {
  return MidiRead{
      .measuring = true, .device = kDevice, .position = position, .compensation = 0.0};
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
  CHECK(transport.takesOnset(Moment(1500ms)));

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

TEST_CASE("Атаки звука: до запуска отбрасываются") {
  Transport transport;
  // Транспорт стоит: атак в строки нет.
  CHECK_FALSE(transport.takesOnset(Moment(500ms)));

  REQUIRE(transport.start(Moment(10s), 0.0, kCompensation));
  // Блок пришёл до начала запуска — атака прошлого запуска, хотя звук шёл
  // без перерыва.
  CHECK_FALSE(transport.takesOnset(Moment(9990ms)));
}

TEST_CASE("Атаки звука: после запуска берутся") {
  Transport transport;
  REQUIRE(transport.start(Moment(10s), 0.0, kCompensation));
  CHECK(transport.takesOnset(Moment(10s)));
  CHECK(transport.takesOnset(Moment(12500ms)));
}

TEST_CASE("Атаки звука: после остановки отбрасываются") {
  Transport transport;
  REQUIRE(transport.start(Moment(10s), 0.0, kCompensation));
  REQUIRE(transport.stop());
  // И блоки, пришедшие до остановки: досчитанное после неё в строки не идёт.
  CHECK_FALSE(transport.takesOnset(Moment(15s)));
  CHECK_FALSE(transport.takesOnset(Moment(21s)));
}

TEST_CASE("Атаки звука: новый запуск отсекает блоки прошлого") {
  Transport transport;
  REQUIRE(transport.start(Moment(10s), 0.0, kCompensation));
  REQUIRE(transport.stop());
  REQUIRE(transport.start(Moment(20s), 0.0, kCompensation));
  CHECK_FALSE(transport.takesOnset(Moment(19900ms)));
  CHECK(transport.takesOnset(Moment(20100ms)));
}

TEST_CASE("Атаки звука: без частоты звука у компенсации отбрасываются") {
  Transport transport;
  Compensation unknown = kCompensation;
  unknown.audioSampleRate = 0.0;
  REQUIRE(transport.start(Moment(10s), 0.0, unknown));
  CHECK_FALSE(transport.takesOnset(Moment(11s)));
}

TEST_CASE("Ноты MIDI: count-in отсчитывается от позиции начала запуска") {
  Transport transport;
  REQUIRE(transport.start(Moment(1s), 4.0, kCompensation));
  MidiNotes &notes = transport.midiNotes();

  // Позиция стоит на 4 с: нота не измеряется.
  CHECK(notes.take(std::vector{noteOn(4.0, 0.010)}, reading(4.0)).empty());

  // Позиция ушла от 4 с на 10 мс: нота 15 мс назад пришла раньше сдвига, нота
  // 5 мс назад — после.
  const std::vector<MidiNote> taken =
      notes.take(std::vector{noteOn(4.0, 0.015), noteOn(4.005, 0.005)}, reading(4.010));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].event.position == doctest::Approx(4.005));
}

TEST_CASE("Ноты MIDI: новый запуск кончает мёртвое время прошлого") {
  Transport transport;
  REQUIRE(transport.start(Moment(1s), 2.0, kCompensation));
  REQUIRE(transport.midiNotes().take({}, reading(2.5)).empty());
  REQUIRE(transport.midiNotes().take(std::vector{noteOn(2.600)}, reading(2.7)).size() == 1);

  // Нота через 20 мс после прошлой — в его мёртвом времени, но в новом
  // запуске.
  REQUIRE(transport.stop());
  REQUIRE(transport.start(Moment(5s), 2.5, kCompensation));
  REQUIRE(transport.midiNotes().take({}, reading(2.55)).empty());
  CHECK(transport.midiNotes().take(std::vector{noteOn(2.620)}, reading(2.7)).size() == 1);
}
