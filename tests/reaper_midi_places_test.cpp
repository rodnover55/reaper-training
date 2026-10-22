#include <doctest/doctest.h>

#include "training/reaper/midi_places.hpp"

#include <optional>
#include <vector>

using training::core::Interval;
using MidiNote = training::core::input::MidiReceiver::Note;
using training::core::Moment;
using training::reaper::Input;
using training::reaper::MidiEvent;
using training::reaper::MidiPlaces;
using training::reaper::MidiRead;

namespace {

constexpr int kDevice = 3;

/// Время ядра через `seconds` секунд от точки отсчёта.
Moment timeAt(double seconds) { return Moment() + seconds * Interval::kSecond; }

/// Секунды от точки отсчёта до времени ядра `time`.
double secondsOf(Moment time) { return (time - Moment()) / Interval::kSecond; }

/// Note On устройства `kDevice` на канале 1 в позиции проекта `position`, с;
/// меньше нуля — транспорт стоял. Пришёл `age` секунд назад.
MidiEvent noteOn(double position, double age = 0.0) {
  return MidiEvent{.position = position < 0.0 ? std::nullopt : std::optional(timeAt(position)),
                   .age = age * Interval::kSecond,
                   .device = kDevice,
                   .loop = 0,
                   .size = 3,
                   .bytes = {0x90, 60, 100}};
}

/// Чтение истории со входом MIDI устройства `kDevice`.
MidiRead midi() { return MidiRead{.input = Input::Midi, .device = kDevice}; }

/// Конец взятого звука через `seconds` секунд от точки отсчёта.
std::optional<Moment> endAt(double seconds) { return timeAt(seconds); }

} // namespace

TEST_CASE("Ноты MIDI связки: время ноты — конец взятого звука минус возраст события") {
  MidiPlaces places;
  places.start();
  const std::vector<MidiNote> notes =
      places.place(std::vector{noteOn(0.5, 0.020), noteOn(0.51, 0.005)}, midi(), endAt(10.0));
  REQUIRE(notes.size() == 2);
  CHECK(secondsOf(notes[0].time) == doctest::Approx(9.980));
  CHECK(secondsOf(notes[1].time) == doctest::Approx(9.995));
}

TEST_CASE("Ноты MIDI связки: время ноты отсчёта — тоже по часам звука") {
  // REAPER во время отсчёта ставит событию позицию начала; время ноты от неё
  // не зависит.
  MidiPlaces places;
  places.start();
  const std::vector<MidiNote> notes =
      places.place(std::vector{noteOn(4.0, 0.5)}, midi(), endAt(13.0));
  REQUIRE(notes.size() == 1);
  CHECK(secondsOf(notes[0].time) == doctest::Approx(12.5));
}

TEST_CASE("Ноты MIDI связки: без конца взятого звука у нот нет времени") {
  MidiPlaces places;
  places.start();
  CHECK(places.place(std::vector{noteOn(0.5)}, midi(), std::nullopt).empty());
}

TEST_CASE("Ноты MIDI связки: не нота выбранного устройства пропускается") {
  MidiPlaces places;
  places.start();
  MidiEvent other = noteOn(0.5);
  other.device = kDevice + 1;
  MidiEvent off = noteOn(0.5);
  off.bytes = {0x80, 60, 64};
  CHECK(places.place(std::vector{other, off}, midi(), endAt(1.0)).empty());
}

TEST_CASE("Ноты MIDI связки: без запуска и при входе звуковой карты нот нет") {
  MidiPlaces places;
  CHECK(places.place(std::vector{noteOn(0.5)}, midi(), endAt(1.0)).empty());

  places.start();
  MidiRead audio = midi();
  audio.input = Input::Audio;
  CHECK(places.place(std::vector{noteOn(0.5)}, audio, endAt(1.0)).empty());

  places.stop();
  CHECK(places.place(std::vector{noteOn(1.5)}, midi(), endAt(2.0)).empty());
}

TEST_CASE("Ноты MIDI связки: нота до Play ждёт запуска и идёт раньше нот чтения") {
  MidiPlaces places;
  // Нота пришла на остановке за 2 мс до чтения: на 9.998 с.
  CHECK(places.place(std::vector{noteOn(-1.0, 0.002)}, midi(), endAt(10.0)).empty());

  places.start();
  const std::vector<MidiNote> notes =
      places.place(std::vector{noteOn(1.020, 0.020)}, midi(), endAt(10.040));
  REQUIRE(notes.size() == 2);
  CHECK(secondsOf(notes[0].time) == doctest::Approx(9.998));
  CHECK(secondsOf(notes[1].time) == doctest::Approx(10.020));
}

TEST_CASE("Ноты MIDI связки: нота до Play старше секунды забывается") {
  MidiPlaces places;
  CHECK(places.place(std::vector{noteOn(-1.0, 0.0)}, midi(), endAt(10.0)).empty());
  CHECK(places.place(std::vector<MidiEvent>{}, midi(), endAt(11.5)).empty());
  places.start();
  CHECK(places.place(std::vector<MidiEvent>{}, midi(), endAt(11.6)).empty());
}

TEST_CASE("Ноты MIDI связки: нота до Play, прочитанная при запуске, идёт сразу") {
  MidiPlaces places;
  places.start();
  // REAPER поставил ноте −1: она пришла до Play, а прочитана при запуске.
  const std::vector<MidiNote> notes =
      places.place(std::vector{noteOn(-1.0, 0.030)}, midi(), endAt(10.0));
  REQUIRE(notes.size() == 1);
  CHECK(secondsOf(notes[0].time) == doctest::Approx(9.970));
}

TEST_CASE("Ноты MIDI связки: смена входа забывает ноты до Play") {
  MidiPlaces places;
  CHECK(places.place(std::vector{noteOn(-1.0, 0.002)}, midi(), endAt(10.0)).empty());
  places.forget();
  places.start();
  CHECK(places.place(std::vector<MidiEvent>{}, midi(), endAt(10.040)).empty());
}
