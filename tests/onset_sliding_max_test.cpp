#include <doctest/doctest.h>

#include "training/onset/sliding_max.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

using training::onset::SlidingMax;

namespace {

struct Item {
  std::uint64_t index = 0;
  double value = 0.0;
  int tag = 0;
};

} // namespace

TEST_CASE("скользящий максимум: совпадает с перебором окна") {
  // Значения из малого набора: равные идут часто, а при равных голова — самое
  // позднее.
  std::mt19937 random(3);
  std::uniform_int_distribution<int> anyValue(0, 7);
  std::vector<double> values;

  for (const std::size_t window : {std::size_t{1}, std::size_t{2}, std::size_t{17}}) {
    CAPTURE(window);
    SlidingMax<Item> max(window);
    values.clear();

    for (std::uint64_t index = 0; index < 2000; ++index) {
      values.push_back(anyValue(random));
      if (index >= window)
        max.dropBefore(index + 1 - window);
      max.push({.index = index, .value = values.back(), .tag = static_cast<int>(index)});

      const std::size_t from = index + 1 > window ? index + 1 - window : 0;
      std::uint64_t expected = from;
      for (std::size_t k = from; k <= index; ++k)
        if (values[k] >= values[expected])
          expected = k;

      REQUIRE(max.max().index == expected);
      REQUIRE(max.max().value == values[expected]);
      REQUIRE(max.max().tag == static_cast<int>(expected));
    }
  }
}

TEST_CASE("скользящий максимум: значение с тем же номером заменяет последнее") {
  SlidingMax<Item> max(4);
  max.push({.index = 0, .value = 5.0});
  max.push({.index = 1, .value = 1.0, .tag = 1});
  max.push({.index = 1, .value = 3.0, .tag = 2});
  CHECK(max.max().index == 0);

  max.dropBefore(1);
  CHECK(max.max().index == 1);
  CHECK(max.max().value == 3.0);
  CHECK(max.max().tag == 2);

  max.dropBefore(2);
  CHECK(max.empty());
}

TEST_CASE("скользящий максимум: clear опустошает окно") {
  SlidingMax<Item> max(3);
  max.push({.index = 10, .value = 1.0});
  REQUIRE_FALSE(max.empty());
  max.clear();
  CHECK(max.empty());
  max.push({.index = 0, .value = 2.0});
  CHECK(max.max().value == 2.0);
}
