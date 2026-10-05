#include <doctest/doctest.h>

#include "synth.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

using training::test::evenPlucks;
using training::test::Pluck;
using training::test::render;
using training::test::renderRinging;
using training::test::RingingSettings;
using training::test::Strike;
using training::test::SynthSettings;

TEST_CASE("синтез: щипок начинается ровно там, где задан") {
  const std::vector<Pluck> plucks{{.start = 1000, .frequency = 110.0},
                                  {.start = 7001, .frequency = 196.0, .string = 1}};
  const std::vector<float> signal = render(plucks, 12000, SynthSettings{});

  for (std::size_t n = 0; n < 1000; ++n)
    REQUIRE(signal[n] == 0.0F);
  CHECK(signal[1000] != 0.0F);

  // Вторая струна добавляет звук ровно с 7001-го сэмпла: до него сигнал тот же,
  // что у одной первой струны.
  const std::vector<float> first = render({plucks[0]}, 12000, SynthSettings{});
  for (std::size_t n = 0; n < 7001; ++n)
    REQUIRE(signal[n] == first[n]);
  CHECK(signal[7001] != first[7001]);
}

TEST_CASE("синтез: новый щипок той же струны глушит прошлый") {
  const SynthSettings settings{};
  const std::vector<float> two = render({{.start = 0}, {.start = 4800}}, 9600, settings);
  const std::vector<float> one = render({{.start = 0}}, 9600, settings);

  // До глушения сигнал общий; к началу второго щипка первый умолк.
  CHECK(two[4000] == one[4000]);
  CHECK(std::abs(two[4799]) < std::abs(one[4799]) * 0.05F + 1e-6F);
}

TEST_CASE("синтез: шум и гул звучат и до щипков, перегруз не выходит за шкалу") {
  const SynthSettings noisy{.noiseDb = -60.0, .humDb = -40.0, .drive = 8.0};
  const std::vector<float> signal =
      render(evenPlucks(4800, 2400.0, 8, {82.4, 110.0}, 0.9), 48000, noisy);

  CHECK(signal[100] != 0.0F);
  for (const float sample : signal)
    REQUIRE(std::abs(sample) <= 1.0F);
}

TEST_CASE("синтез: ровная сетка щипков") {
  // Шестнадцатые при 160 BPM — через 93.75 мс, при 48 кГц — 4500 сэмплов.
  const std::vector<Pluck> plucks = evenPlucks(480, 4500.0, 4, {110.0, 147.0}, 0.5);
  REQUIRE(plucks.size() == 4);
  CHECK(plucks[0].start == 480);
  CHECK(plucks[3].start == 480 + 3 * 4500);
  CHECK(plucks[1].frequency == 147.0);
  CHECK(plucks[2].frequency == 110.0);
}

namespace {

/// Наибольший модуль сэмпла в `signal[from, to)`.
float peakOf(const std::vector<float> &signal, std::size_t from, std::size_t to) {
  const std::span<const float> part = std::span(signal).subspan(from, to - from);
  return std::abs(std::ranges::max(part, {}, [](float sample) { return std::abs(sample); }));
}

} // namespace

TEST_CASE("синтез: тело ноты разгорается с первого сэмпла") {
  const SynthSettings settings{};
  const std::vector<float> rising =
      render({{.start = 1000, .frequency = 110.0, .riseMs = 8.0}}, 4800, settings);
  const std::vector<float> plucked =
      render({{.start = 1000, .frequency = 110.0}}, 4800, settings);

  CHECK(rising[999] == 0.0F);
  CHECK(rising[1000] != 0.0F);

  // Первую миллисекунду звук тише щипка без нарастания, после 8 мс — тот же.
  CHECK(peakOf(rising, 1000, 1048) < 0.2F * peakOf(plucked, 1000, 1048));
  CHECK(rising[1500] == plucked[1500]);
}

TEST_CASE("синтез: звенящая струна — пачка медиатора, затем всплеск раз в период") {
  const RingingSettings settings{};
  const Strike struck{
      .start = 1000, .amplitude = 0.0, .burstAmplitude = 0.01, .pickAmplitude = 0.02};
  const std::vector<float> signal = renderRinging({struck}, 9600, settings);

  // Всплеск начинается с нуля ровно на начале удара.
  for (std::size_t n = 0; n <= 1000; ++n)
    REQUIRE(signal[n] == 0.0F);
  CHECK(signal[1001] != 0.0F);

  // Пачка — пять всплесков через 1.5 мс, звон — с конца пачки, раз в период
  // си: 778 сэмплов.
  const std::size_t release = 1000 + 360;
  CHECK(std::abs(peakOf(signal, 1000, 1072) - 0.02F * 0.76F) < 0.002F);
  CHECK(std::abs(peakOf(signal, 1288, 1360) - 0.02F * 0.76F) < 0.002F);
  CHECK(std::abs(peakOf(signal, release, release + 72) - 0.01F * 0.76F) < 0.001F);
  CHECK(peakOf(signal, release + 200, release + 700) < 1e-6F);
  CHECK(std::abs(peakOf(signal, release + 778, release + 850) - 0.01F * 0.76F) < 0.001F);
}

TEST_CASE("синтез: новый удар обрывает звон прошлого") {
  const RingingSettings settings{};
  const Strike first{
      .start = 0, .amplitude = 0.05, .burstAmplitude = 0.01, .pickAmplitude = 0.0};
  const std::vector<float> one = renderRinging({first}, 9600, settings);
  const std::vector<float> two = renderRinging(
      {first, {.start = 4800, .amplitude = 0.0, .burstAmplitude = 0.0, .pickAmplitude = 0.0}},
      9600, settings);

  // До глушения звон общий, к началу второго удара первый умолк.
  CHECK(two[4000] == one[4000]);
  CHECK(std::abs(two[4799]) < std::abs(one[4799]) * 0.05F + 1e-6F);
  CHECK(peakOf(two, 4800, 9600) == 0.0F);
}
