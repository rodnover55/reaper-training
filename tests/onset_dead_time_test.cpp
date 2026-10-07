#include <doctest/doctest.h>

#include "training/onset/dead_time.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

using training::onset::DeadTime;
using training::onset::isNoteStart;

namespace {

/// Подаёт ноты со временем `ms`, мс, в новый `DeadTime` и возвращает времена
/// измеренных нот, мс.
std::vector<double> measured(std::initializer_list<double> ms) {
  DeadTime deadTime;
  std::vector<double> notes;
  for (const double at : ms)
    if (deadTime.add(at / 1000.0))
      notes.push_back(at);
  return notes;
}

bool noteStart(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) {
  const std::array<std::uint8_t, 3> message{status, data1, data2};
  return isNoteStart(message);
}

} // namespace

TEST_CASE("мёртвое время MIDI: 40 мс, как у поиска атак") {
  CHECK(DeadTime::kLengthSeconds == doctest::Approx(0.040));
}

TEST_CASE("мёртвое время MIDI: ноты на 3, 5, 9 и 12 мс — измерена нота на 3 мс") {
  DeadTime deadTime;
  CHECK(deadTime.add(0.003));
  CHECK_FALSE(deadTime.add(0.005));
  CHECK_FALSE(deadTime.add(0.009));
  CHECK_FALSE(deadTime.add(0.012));
  REQUIRE(deadTime.start());
  CHECK(*deadTime.start() == doctest::Approx(0.003));
}

TEST_CASE("мёртвое время MIDI: от измеренной ноты, а не от пропущенной") {
  CHECK(measured({0.0, 15.0, 30.0, 45.0, 55.0}) == std::vector<double>{0.0, 45.0});
}

TEST_CASE("мёртвое время MIDI: нота ровно на 40 мс пропущена, на 40.001 мс — измерена") {
  CHECK(measured({0.0, 40.0}) == std::vector<double>{0.0});
  CHECK(measured({0.0, 40.001}) == std::vector<double>{0.0, 40.001});
}

TEST_CASE("мёртвое время MIDI: одиночные ноты через 100 мс — измерена каждая") {
  CHECK(measured({0.0, 100.0, 200.0, 300.0}) == std::vector<double>{0.0, 100.0, 200.0, 300.0});
}

TEST_CASE("мёртвое время MIDI: нота раньше мёртвого времени — измерена") {
  CHECK(measured({10'000.0, 5'000.0, 5'010.0}) == std::vector<double>{10'000.0, 5'000.0});
}

TEST_CASE("мёртвое время MIDI: после reset() нота измерена") {
  DeadTime deadTime;
  CHECK(deadTime.add(1.000));
  deadTime.reset();
  CHECK_FALSE(deadTime.start());
  CHECK(deadTime.add(1.010));
}

TEST_CASE("начало ноты: Note On с ненулевой громкостью на любом канале") {
  CHECK(noteStart(0x99, 38, 1));
  CHECK(noteStart(0x90, 60, 127));
  CHECK(noteStart(0x9F, 60, 64));

  // Note On с громкостью 0 — конец ноты.
  CHECK_FALSE(noteStart(0x90, 60, 0));
  CHECK_FALSE(noteStart(0x80, 60, 64));
  // Педаль сустейна.
  CHECK_FALSE(noteStart(0xB0, 64, 127));
  // Pitch bend.
  CHECK_FALSE(noteStart(0xE0, 0x00, 0x40));

  const std::array<std::uint8_t, 2> shortMessage{0x90, 60};
  CHECK_FALSE(isNoteStart(shortMessage));
}
