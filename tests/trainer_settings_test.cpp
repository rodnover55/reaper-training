#include <doctest/doctest.h>

#include "training/trainer/settings.hpp"

#include <string>

using training::grid::HitWindow;
using training::grid::Mode;
using training::trainer::audioChannel;
using training::trainer::Field;
using training::trainer::inputChanged;
using training::trainer::inputText;
using training::trainer::Range;
using training::trainer::rangeHint;
using training::trainer::rejectionText;
using training::trainer::Settings;

namespace {

/// Настройки по умолчанию с каналом `channel`.
Settings onChannel(int channel) {
  Settings settings;
  REQUIRE(settings.setChannel(channel));
  return settings;
}

/// Настройки по умолчанию с каналом `channel` и входом MIDI `name` устройства
/// `index`.
Settings onMidi(int channel, const std::string &name, int index) {
  Settings settings = onChannel(channel);
  settings.setMidiInput(name);
  settings.setMidiIndex(index);
  return settings;
}

/// Настройки по умолчанию с окном попадания `offsetMs` ± `toleranceMs`.
Settings withWindow(double offsetMs, double toleranceMs) {
  Settings settings;
  REQUIRE(settings.setWindow({.offsetMs = offsetMs, .toleranceMs = toleranceMs}));
  return settings;
}

} // namespace

TEST_CASE("Настройки: канал звука и смена входа") {
  CHECK(audioChannel(onChannel(1)) == 1);
  CHECK(audioChannel(onMidi(1, "KeyStep", -1)) == -1);

  const Settings channel = onChannel(1);
  Settings quieter = channel;
  REQUIRE(quieter.setSilenceDb(-30.0));
  CHECK_FALSE(inputChanged(channel, quieter));
  CHECK(inputChanged(channel, onChannel(0)));
  const Settings keyStep = onMidi(1, "KeyStep", 2);
  CHECK(inputChanged(channel, keyStep));
  Settings renumbered = keyStep;
  renumbered.setMidiIndex(3);
  CHECK(inputChanged(keyStep, renumbered));
}

TEST_CASE("Настройки: выбранный вход для журнала") {
  // Канал — с 1, как в списке Input.
  CHECK(inputText(onChannel(0)) == "channel 1");
  CHECK(inputText(onChannel(3)) == "channel 4");
  // Вход MIDI — имя и номер устройства; канал звука при нём не пишется.
  CHECK(inputText(onMidi(3, "KeyStep 37", 2)) == "MIDI «KeyStep 37» #2");
}

TEST_CASE("Настройки: по умолчанию") {
  const Settings settings;
  CHECK(settings.mode() == Mode::Quarters);
  CHECK(settings.scaleMs() == 50.0);
  CHECK(settings.window() == HitWindow{.offsetMs = 0.0, .toleranceMs = 10.0});
  CHECK(settings.silenceDb() == -50.0);
  CHECK(settings.channel() == 0);
  CHECK(settings.midiInput().empty());
  CHECK(settings.midiIndex() == -1);
}

TEST_CASE("Настройки: режим — один из пяти") {
  for (const Mode mode : {Mode::Quarters, Mode::Eighths, Mode::EighthTriplets,
                          Mode::Sixteenths, Mode::SixteenthTriplets}) {
    Settings settings;
    CHECK(settings.setMode(mode));
    CHECK(settings.mode() == mode);
  }

  Settings settings;
  REQUIRE(settings.setMode(Mode::Eighths));
  const Settings before = settings;
  // Режим вне перечисления — как из файла настроек с mode=5.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): это и проверяется
  CHECK_FALSE(settings.setMode(static_cast<Mode>(5)));
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): это и проверяется
  CHECK_FALSE(settings.setMode(static_cast<Mode>(0)));
  CHECK(settings == before);
}

TEST_CASE("Настройки: граница шкалы — целое от 10 до 200, окно в шкале") {
  Settings settings;
  CHECK(settings.setScaleMs(10.0));
  CHECK(settings.scaleMs() == 10.0);
  CHECK(settings.setScaleMs(200.0));
  CHECK(settings.scaleMs() == 200.0);

  const Settings before = settings;
  CHECK_FALSE(settings.setScaleMs(9.0));
  CHECK_FALSE(settings.setScaleMs(201.0));
  CHECK_FALSE(settings.setScaleMs(30.5));
  CHECK(settings == before);

  SUBCASE("окно за краем новой шкалы") {
    Settings shifted = withWindow(40.0, 10.0);
    const Settings wide = shifted;
    CHECK_FALSE(shifted.setScaleMs(30.0));
    CHECK(shifted == wide);

    Settings centered = withWindow(0.0, 10.0);
    CHECK(centered.setScaleMs(30.0));
    CHECK(centered.scaleMs() == 30.0);
  }
}

TEST_CASE("Настройки: окно попадания — допуск не меньше 1 мс, края в шкале") {
  Settings settings;
  CHECK(settings.setWindow({.offsetMs = 40.0, .toleranceMs = 10.0}));
  CHECK(settings.setWindow({.offsetMs = 0.0, .toleranceMs = 1.0}));
  CHECK(settings.setWindow({.offsetMs = 7.5, .toleranceMs = 12.5}));
  CHECK(settings.window() == HitWindow{.offsetMs = 7.5, .toleranceMs = 12.5});

  const Settings before = settings;
  CHECK_FALSE(settings.setWindow({.offsetMs = 40.5, .toleranceMs = 10.0}));
  CHECK_FALSE(settings.setWindow({.offsetMs = 0.0, .toleranceMs = 0.5}));
  CHECK_FALSE(settings.setWindow({.offsetMs = 0.0, .toleranceMs = 50.5}));
  CHECK(settings == before);
}

TEST_CASE("Настройки: смещение — в шкале при прежнем допуске") {
  Settings settings;
  CHECK(settings.offsetRange() == Range{.min = -40.0, .max = 40.0});
  CHECK(settings.setOffsetMs(-40.0));
  CHECK(settings.setOffsetMs(40.0));
  CHECK(settings.window() == HitWindow{.offsetMs = 40.0, .toleranceMs = 10.0});

  const Settings before = settings;
  CHECK_FALSE(settings.setOffsetMs(40.5));
  CHECK_FALSE(settings.setOffsetMs(-40.5));
  CHECK_FALSE(settings.setOffsetMs(45.0));
  CHECK(settings == before);
}

TEST_CASE("Настройки: допуск — от 1 мс до края шкалы при прежнем смещении") {
  Settings settings;
  CHECK(settings.toleranceRange() == Range{.min = 1.0, .max = 50.0});
  CHECK(settings.setToleranceMs(1.0));
  CHECK(settings.setToleranceMs(50.0));
  CHECK(settings.toleranceMs() == 50.0);

  Settings before = settings;
  CHECK_FALSE(settings.setToleranceMs(0.5));
  CHECK_FALSE(settings.setToleranceMs(50.5));
  CHECK(settings == before);

  Settings shifted = withWindow(40.0, 10.0);
  CHECK(shifted.toleranceRange() == Range{.min = 1.0, .max = 10.0});
  before = shifted;
  CHECK_FALSE(shifted.setToleranceMs(20.0));
  CHECK(shifted == before);
}

TEST_CASE("Настройки: порог тишины — целое от −90 до −10") {
  Settings settings;
  CHECK(settings.setSilenceDb(-90.0));
  CHECK(settings.setSilenceDb(-10.0));
  CHECK(settings.silenceDb() == -10.0);

  const Settings before = settings;
  CHECK_FALSE(settings.setSilenceDb(-91.0));
  CHECK_FALSE(settings.setSilenceDb(-9.0));
  CHECK_FALSE(settings.setSilenceDb(-50.5));
  CHECK(settings == before);
}

TEST_CASE("Настройки: канал — не меньше 0") {
  Settings settings;
  CHECK(settings.setChannel(0));
  CHECK(settings.setChannel(5));

  const Settings before = settings;
  CHECK_FALSE(settings.setChannel(-1));
  CHECK(settings == before);
}

TEST_CASE("Настройки: подсказка с допустимыми значениями поля") {
  const Settings settings;
  CHECK(rangeHint(Field::Offset, settings) == "-40…+40 ms at tolerance 10");
  CHECK(rangeHint(Field::Tolerance, settings) == "1…50 ms at offset 0");
  CHECK(rangeHint(Field::Silence, settings) == "-90…-10 dBFS");

  // Диапазон — при действующих значениях остальных настроек.
  const Settings shifted = withWindow(7.5, 12.5);
  CHECK(rangeHint(Field::Offset, shifted) == "-37.5…+37.5 ms at tolerance 12.5");
  CHECK(rangeHint(Field::Tolerance, shifted) == "1…42.5 ms at offset +7.5");
}

TEST_CASE("Настройки: сообщение о не принятом значении поля") {
  Settings settings;
  REQUIRE(settings.setSilenceDb(-40.0));
  CHECK(rejectionText(Field::Silence, "-5", settings) ==
        "Silence -5 dBFS is out of range -90…-10 — not applied, -40 in use");

  CHECK(rejectionText(Field::Offset, "45", Settings{}) ==
        "Offset 45 ms is out of range -40…+40 at tolerance 10 — not applied, 0 in use");
  CHECK(rejectionText(Field::Tolerance, "0.4", Settings{}) ==
        "Tolerance 0.4 ms is out of range 1…50 at offset 0 — not applied, 10 in use");
}
