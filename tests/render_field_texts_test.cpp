#include <doctest/doctest.h>

#include "training/render/main/field_texts.hpp"

using training::render::Field;
using training::render::FieldBounds;
using training::render::rangeHint;
using training::render::rejectionText;

TEST_CASE("Тексты полей: подсказка с допустимыми значениями поля") {
  CHECK(rangeHint(Field::Offset, FieldBounds{.min = -40.0, .max = 40.0, .condition = 10.0}) ==
        "-40…+40 ms at tolerance 10");
  CHECK(rangeHint(Field::Tolerance, FieldBounds{.min = 1.0, .max = 50.0, .condition = 0.0}) ==
        "1…50 ms at offset 0");
  CHECK(rangeHint(Field::Silence,
                  FieldBounds{.min = -90.0, .max = -10.0, .condition = std::nullopt}) ==
        "-90…-10 dBFS");
  CHECK(rangeHint(Field::HideBars,
                  FieldBounds{.min = 0.0, .max = 16.0, .condition = std::nullopt}) == "0…16");

  // Половины миллисекунд — с одним знаком после точки.
  CHECK(rangeHint(Field::Offset, FieldBounds{.min = -37.5, .max = 37.5, .condition = 12.5}) ==
        "-37.5…+37.5 ms at tolerance 12.5");
  CHECK(rangeHint(Field::Tolerance, FieldBounds{.min = 1.0, .max = 42.5, .condition = 7.5}) ==
        "1…42.5 ms at offset +7.5");
}

TEST_CASE("Тексты полей: сообщение о не принятом значении поля") {
  CHECK(rejectionText(Field::Silence, "-5",
                      FieldBounds{.min = -90.0, .max = -10.0, .condition = std::nullopt},
                      -40.0) ==
        "Silence -5 dBFS is out of range -90…-10 — not applied, -40 in use");
  CHECK(rejectionText(Field::Offset, "45",
                      FieldBounds{.min = -40.0, .max = 40.0, .condition = 10.0}, 0.0) ==
        "Offset 45 ms is out of range -40…+40 at tolerance 10 — not applied, 0 in use");
  CHECK(rejectionText(Field::Tolerance, "0.4",
                      FieldBounds{.min = 1.0, .max = 50.0, .condition = 0.0}, 10.0) ==
        "Tolerance 0.4 ms is out of range 1…50 at offset 0 — not applied, 10 in use");
  CHECK(rejectionText(Field::HideBars, "20",
                      FieldBounds{.min = 0.0, .max = 16.0, .condition = std::nullopt},
                      2.0) == "Hide bars 20 is out of range 0…16 — not applied, 2 in use");
}
