#include <doctest/doctest.h>

#include "training/grid/note_time.hpp"

using training::grid::compensation;
using training::grid::noteTime;
using training::grid::wrapIntoLoop;

TEST_CASE("время ноты: компенсация по правилу записи") {
  // Галка включена: вход и выход из GetInputOutputLatency, поправки уже в них.
  CHECK(compensation(512, 1024, 0, 0, true, 48000.0) == doctest::Approx(0.032));
  CHECK(compensation(992, 1024, 480, 0, true, 48000.0) == doctest::Approx(0.042));

  // Галка выключена: только ручные поправки.
  CHECK(compensation(992, 1024, 480, 0, false, 48000.0) == doctest::Approx(0.010));
  CHECK(compensation(512, 1024, 0, 0, false, 48000.0) == 0.0);
}

TEST_CASE("время ноты: при скорости 1.0 — позиция блока плюс смещение минус компенсация") {
  CHECK(noteTime(10.0, 480.0, 48000.0, 0.032, 1.0) == doctest::Approx(10.0 + 0.010 - 0.032));
}

TEST_CASE("время ноты: при скорости 0.8 компенсация 32 мс сдвигает на 25.6 мс шкалы") {
  CHECK(noteTime(10.0, 0.0, 48000.0, 0.032, 0.8) == doctest::Approx(10.0 - 0.0256));
  // Смещение внутри блока — реальное время: 480 сэмплов при 0.8 — 8 мс шкалы.
  CHECK(noteTime(10.0, 480.0, 48000.0, 0.0, 0.8) == doctest::Approx(10.008));
}

TEST_CASE("время ноты: заворот в петлю 10–12 с") {
  // Нота за 5 мс до начала петли пришла уже после перехода.
  CHECK(wrapIntoLoop(9.995, 10.0, 12.0) == doctest::Approx(11.995));
  CHECK(wrapIntoLoop(12.003, 10.0, 12.0) == doctest::Approx(10.003));
  CHECK(wrapIntoLoop(11.0, 10.0, 12.0) == doctest::Approx(11.0));
  CHECK(wrapIntoLoop(5.0, 10.0, 12.0) == doctest::Approx(11.0));

  // Петля без длины время не меняет.
  CHECK(wrapIntoLoop(9.995, 10.0, 10.0) == 9.995);
}
