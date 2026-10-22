#include <doctest/doctest.h>

#include "input_settings.hpp"

using training::reaper::InputSettings;

TEST_CASE("Выбранный вход: по умолчанию") {
  const InputSettings input;
  CHECK(input.channel() == 0);
  CHECK(input.midiInput().empty());
  CHECK(input.midiIndex() == -1);
}

TEST_CASE("Выбранный вход: канал — от 0") {
  InputSettings input;
  CHECK(input.setChannel(0));
  CHECK(input.setChannel(5));
  CHECK(input.channel() == 5);

  const InputSettings before = input;
  CHECK_FALSE(input.setChannel(-1));
  CHECK(input == before);
}
