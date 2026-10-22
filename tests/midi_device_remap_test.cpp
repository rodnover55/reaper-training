#include <doctest/doctest.h>

#include "midi_inputs.hpp"

using training::reaper::remappedDevice;

TEST_CASE("remappedDevice: вход с прежним номером получает новый") {
  CHECK(remappedDevice(2, false, 2, 5) == 5);
}

TEST_CASE("remappedDevice: чужой номер не меняется") {
  CHECK(remappedDevice(3, false, 2, 5) == 3);
}

TEST_CASE("remappedDevice: смена номеров выходов входы не трогает") {
  CHECK(remappedDevice(2, true, 2, 5) == 2);
}
