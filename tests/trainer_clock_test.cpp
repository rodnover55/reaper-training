#include <doctest/doctest.h>

#include "training/trainer/clock.hpp"

#include <chrono>

using training::trainer::Moment;
using training::trainer::momentText;

using namespace std::chrono_literals;

TEST_CASE("Часы ядра: момент в журнале — секунды с шестью знаками") {
  // Доли микросекунды отбрасываются.
  CHECK(momentText(Moment(12s + 345678ns)) == "12.000345");
  CHECK(momentText(Moment{}) == "0.000000");
  CHECK(momentText(Moment(-1500ms)) == "-1.500000");
  // Микросекунды не теряются и через сутки работы часов.
  CHECK(momentText(Moment(std::chrono::hours(30) + 1us)) == "108000.000001");
}
