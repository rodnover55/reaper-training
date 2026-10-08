#include <doctest/doctest.h>

#include "training/trainer/fit_text.hpp"

#include <string_view>

using training::trainer::fitText;

namespace {

/// Ширина строки в знаках UTF-8.
double characters(std::string_view text) {
  double count = 0.0;
  for (const char byte : text)
    if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U)
      count += 1.0;
  return count;
}

} // namespace

TEST_CASE("Обрезка строки: текст, который помещается, — как есть") {
  CHECK(fitText("Quarters", 8.0, characters) == "Quarters");
  CHECK(fitText("Quarters", 20.0, characters) == "Quarters");
  CHECK(fitText("", 0.0, characters).empty());
}

TEST_CASE("Обрезка строки: самое длинное начало с «...», которое помещается") {
  const std::string_view text = "mode=5 is not a grid mode";
  CHECK(fitText(text, 10.0, characters) == "mode=5 ...");
  // С ещё одним знаком начало уже не поместилось бы.
  CHECK(characters("mode=5 i...") > 10.0);
  CHECK(fitText(text, 24.0, characters) == "mode=5 is not a grid ...");
}

TEST_CASE("Обрезка строки: знаки UTF-8 не разрываются") {
  // «—» и «…» — по три байта, но по одному знаку.
  CHECK(fitText("50 — in use", 7.0, characters) == "50 —...");
  CHECK(fitText("10…200 ms", 6.0, characters) == "10…...");
}

TEST_CASE("Обрезка строки: уже «...» — одно «...»") {
  CHECK(fitText("Quarters", 2.0, characters) == "...");
  CHECK(fitText("Quarters", 3.0, characters) == "...");
}
