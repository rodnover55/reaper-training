#include <doctest/doctest.h>

#include "training/core/detector/sliding_max.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

using training::core::detector::SlidingMax;

namespace {

/// Значение с меткой: метка различает равные значения.
struct Item {
  double number = 0.0;
  int tag = 0;

  double value() const { return number; }
};

using Max = SlidingMax<Item>;

/// Максимум окна `max` на сэмпле `sample`: кусок из одного сэмпла.
Max::SampleValue maxOf(Max &max, Max::SampleValue sample) {
  max.process(std::span(&sample, 1));
  return sample;
}

} // namespace

TEST_CASE("скользящий максимум: окна на ноль сэмплов нет") { CHECK_FALSE(Max::create(0)); }

TEST_CASE("скользящий максимум: совпадает с перебором окна") {
  // Значения из малого набора: равные идут часто, а из равных максимум —
  // самое раннее.
  std::mt19937 random(3);
  std::uniform_int_distribution<int> anyValue(0, 7);
  std::vector<double> values;

  for (const std::size_t window : {std::size_t{1}, std::size_t{2}, std::size_t{17}}) {
    CAPTURE(window);
    auto max = Max::create(window);
    REQUIRE(max);
    values.clear();

    for (std::uint64_t index = 0; index < 2000; ++index) {
      values.push_back(anyValue(random));
      const Max::SampleValue found =
          maxOf(*max, {.index = index,
                       .value = {.number = values.back(), .tag = static_cast<int>(index)}});

      const std::size_t from = index + 1 > window ? index + 1 - window : 0;
      std::uint64_t expected = from;
      for (std::size_t k = from; k <= index; ++k)
        if (values[k] > values[expected])
          expected = k;

      REQUIRE(found.index == expected);
      REQUIRE(found.value.value() == values[expected]);
      REQUIRE(found.value.tag == static_cast<int>(expected));
    }
  }
}

TEST_CASE("скользящий максимум: из равных наибольших — самый ранний сэмпл") {
  auto max = Max::create(4);
  REQUIRE(max);
  CHECK(maxOf(*max, {.index = 0, .value = {.number = 1.0}}).index == 0);
  CHECK(maxOf(*max, {.index = 1, .value = {.number = 1.0}}).index == 0);
  CHECK(maxOf(*max, {.index = 2, .value = {.number = 1.0}}).index == 0);
  CHECK(maxOf(*max, {.index = 4, .value = {.number = 0.5}}).index == 1);
}

TEST_CASE("скользящий максимум: кусок сэмплов даёт максимум на каждом") {
  auto max = Max::create(2);
  REQUIRE(max);
  std::vector<Max::SampleValue> maxima{{.index = 0, .value = {.number = 3.0}},
                                       {.index = 1, .value = {.number = 1.0}},
                                       {.index = 2, .value = {.number = 2.0}},
                                       {.index = 3, .value = {.number = 0.0}}};
  max->process(maxima);

  CHECK(maxima[0].index == 0);
  CHECK(maxima[1].index == 0);
  CHECK(maxima[2].index == 2);
  CHECK(maxima[3].index == 2);
}

TEST_CASE("скользящий максимум: reset начинает вычисление заново") {
  auto max = Max::create(3);
  REQUIRE(max);
  maxOf(*max, {.index = 10, .value = {.number = 5.0}});
  max->reset();
  const Max::SampleValue found = maxOf(*max, {.index = 0, .value = {.number = 2.0}});
  CHECK(found.index == 0);
  CHECK(found.value.value() == 2.0);
}
