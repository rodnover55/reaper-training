#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/trainer/transport.hpp"

using training::grid::HitWindow;
using training::test::SteadyTimeline;
using training::trainer::Compensation;
using training::trainer::Transport;

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

/// Слышимая позиция `beats` и нота точно в узле этого удара.
void playNote(Transport &transport, double beats) {
  transport.play(beats, 0.0, kWindow, timeline());
  transport.addNote(timeline().timeAt(beats), 1.0, kWindow, timeline());
}

} // namespace

TEST_CASE("Транспорт: запуск начинается и кончается") {
  Transport transport;
  CHECK_FALSE(transport.running());
  CHECK(transport.start(1.0, kCompensation));
  CHECK(transport.running());
  CHECK(transport.stop());
  CHECK_FALSE(transport.running());
}

TEST_CASE("Транспорт: начало при идущем запуске и конец без запуска ничего не меняют") {
  Transport transport;
  CHECK_FALSE(transport.stop());

  REQUIRE(transport.start(1.0, kCompensation));
  CHECK_FALSE(transport.start(2.0, Compensation{}));
  CHECK(transport.running());
  // Момент и компенсация — первого начала.
  CHECK(transport.compensation() == kCompensation);
  CHECK(transport.takesOnset(1.5));

  REQUIRE(transport.stop());
  CHECK_FALSE(transport.stop());
}

TEST_CASE("Транспорт: компенсация снимается при начале и держится до следующего") {
  Transport transport;
  CHECK(transport.compensation() == Compensation{});

  REQUIRE(transport.start(1.0, kCompensation));
  CHECK(transport.compensation() == kCompensation);
  REQUIRE(transport.stop());
  CHECK(transport.compensation() == kCompensation);

  Compensation other = kCompensation;
  other.audio = 0.030;
  REQUIRE(transport.start(5.0, other));
  CHECK(transport.compensation() == other);
}

TEST_CASE("Транспорт: строки тактов остаются после остановки, новый запуск их очищает") {
  Transport transport;
  REQUIRE(transport.start(1.0, kCompensation));
  playNote(transport, 0.0);
  playNote(transport, 4.0);
  REQUIRE(transport.bars().rows().size() == 2);

  REQUIRE(transport.stop());
  CHECK(transport.bars().rows().size() == 2);

  REQUIRE(transport.start(10.0, kCompensation));
  CHECK(transport.bars().rows().empty());
}

TEST_CASE("Транспорт: повторное начало при идущем запуске строки не трогает") {
  Transport transport;
  REQUIRE(transport.start(1.0, kCompensation));
  playNote(transport, 0.0);
  REQUIRE_FALSE(transport.start(2.0, kCompensation));
  CHECK(transport.bars().rows().size() == 1);
}

TEST_CASE("Атаки звука: до запуска отбрасываются") {
  Transport transport;
  // Транспорт стоит: атак в строки нет.
  CHECK_FALSE(transport.takesOnset(0.5));

  REQUIRE(transport.start(10.0, kCompensation));
  // Блок пришёл до начала запуска — атака прошлого запуска, хотя звук шёл
  // без перерыва.
  CHECK_FALSE(transport.takesOnset(9.99));
}

TEST_CASE("Атаки звука: после запуска берутся") {
  Transport transport;
  REQUIRE(transport.start(10.0, kCompensation));
  CHECK(transport.takesOnset(10.0));
  CHECK(transport.takesOnset(12.5));
}

TEST_CASE("Атаки звука: после остановки отбрасываются") {
  Transport transport;
  REQUIRE(transport.start(10.0, kCompensation));
  REQUIRE(transport.stop());
  // И блоки, пришедшие до остановки: досчитанное после неё в строки не идёт.
  CHECK_FALSE(transport.takesOnset(15.0));
  CHECK_FALSE(transport.takesOnset(21.0));
}

TEST_CASE("Атаки звука: новый запуск отсекает блоки прошлого") {
  Transport transport;
  REQUIRE(transport.start(10.0, kCompensation));
  REQUIRE(transport.stop());
  REQUIRE(transport.start(20.0, kCompensation));
  CHECK_FALSE(transport.takesOnset(19.9));
  CHECK(transport.takesOnset(20.1));
}

TEST_CASE("Атаки звука: без частоты звука у компенсации отбрасываются") {
  Transport transport;
  Compensation unknown = kCompensation;
  unknown.audioSampleRate = 0.0;
  REQUIRE(transport.start(10.0, unknown));
  CHECK_FALSE(transport.takesOnset(11.0));
}
