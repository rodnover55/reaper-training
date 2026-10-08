#include <doctest/doctest.h>

#include "training/trainer/settings.hpp"

using training::trainer::audioChannel;
using training::trainer::inputChanged;
using training::trainer::inputText;
using training::trainer::Settings;

TEST_CASE("Настройки: канал звука и смена входа") {
  CHECK(audioChannel(Settings{.channel = 1}) == 1);
  CHECK(audioChannel(Settings{.channel = 1, .midiInput = "KeyStep"}) == -1);

  const Settings channel{.channel = 1};
  CHECK_FALSE(inputChanged(channel, Settings{.channel = 1, .silenceDb = -30.0}));
  CHECK(inputChanged(channel, Settings{.channel = 0}));
  const Settings keyStep{.channel = 1, .midiInput = "KeyStep", .midiIndex = 2};
  CHECK(inputChanged(channel, keyStep));
  Settings renumbered = keyStep;
  renumbered.midiIndex = 3;
  CHECK(inputChanged(keyStep, renumbered));
}

TEST_CASE("Настройки: выбранный вход для журнала") {
  // Канал — с 1, как в списке Input.
  CHECK(inputText(Settings{.channel = 0}) == "channel 1");
  CHECK(inputText(Settings{.channel = 3}) == "channel 4");
  // Вход MIDI — имя и номер устройства; канал звука при нём не пишется.
  CHECK(inputText(Settings{.channel = 3, .midiInput = "KeyStep 37", .midiIndex = 2}) ==
        "MIDI «KeyStep 37» #2");
}
