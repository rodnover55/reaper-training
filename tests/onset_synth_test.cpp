#include <doctest/doctest.h>

#include "synth.hpp"

#include "training/onset/detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <random>
#include <span>
#include <vector>

using training::onset::Detector;
using training::onset::Onset;
using training::onset::Settings;
using training::test::evenPlucks;
using training::test::Pluck;
using training::test::render;
using training::test::renderRinging;
using training::test::RingingSettings;
using training::test::Strike;
using training::test::SynthSettings;

namespace {

constexpr double kSampleRate = 48000.0;

/// 1 мс в сэмплах — допуск на начало ноты (`hit-timing`).
constexpr double kMillisecond = kSampleRate / 1000.0;

/// Порог тишины на тихом DI-входе: щелчок медиатора после ВЧ-фильтра там
/// −54…−60 dBFS.
constexpr double kQuietDiSilenceDb = -60.0;

/// Прогоняет сигнал через новый детектор с порогом тишины `silenceDb` кусками,
/// длины которых даёт `next`.
template <class Next>
std::vector<Onset> detect(const std::vector<float> &signal, double silenceDb, Next next) {
  Detector detector(Settings{.sampleRate = kSampleRate, .silenceDb = silenceDb});
  std::vector<Onset> onsets;
  const std::span<const float> all(signal);
  for (std::size_t from = 0; from < all.size();) {
    const std::size_t length = std::min(next(), all.size() - from);
    detector.process(all.subspan(from, length), onsets);
    from += length;
  }
  return onsets;
}

template <class Next> std::vector<Onset> detect(const std::vector<float> &signal, Next next) {
  return detect(signal, Settings{}.silenceDb, next);
}

std::vector<Onset> detect(const std::vector<float> &signal, double silenceDb) {
  return detect(signal, silenceDb, [] { return std::size_t{512}; });
}

std::vector<Onset> detect(const std::vector<float> &signal) {
  return detect(signal, Settings{}.silenceDb);
}

/// Звенящая си: тон и ВЧ-всплески звона, как у незаглушённой струны на DI.
const RingingSettings kRinging{.sampleRate = kSampleRate};

/// Удар по си с пачкой медиатора `pickAmplitude`.
Strike strike(std::size_t start, double pickAmplitude) {
  return {.start = start,
          .amplitude = 0.05,
          .burstAmplitude = 0.005,
          .pickAmplitude = pickAmplitude};
}

/// Амплитуда всплеска звона удара `struck` к сэмплу `at`: звон начинается после
/// пачки медиатора и затихает вместе с тоном.
double ringAt(const Strike &struck, std::size_t at) {
  const double release = static_cast<double>(struck.start) +
                         kRinging.pickBursts * kRinging.pickSpacingMs * kSampleRate / 1000.0;
  return struck.burstAmplitude *
         std::pow(kRinging.decayPerSecond, (static_cast<double>(at) - release) / kSampleRate);
}

/// Две ноты на звенящей си: второй удар через `gap` сэмплов, пачка медиатора
/// на `riseDb` выше всплесков звона первого удара.
std::vector<Strike> twoStrikes(std::size_t gap, double riseDb) {
  const Strike first = strike(4800, 0.01);
  const std::size_t second = first.start + gap;
  return {first, strike(second, ringAt(first, second) * std::pow(10.0, riseDb / 20.0))};
}

/// Одиночные щипки разных струн и громкостей, по полсекунды на щипок.
std::vector<Pluck> singlePlucks() {
  std::vector<Pluck> plucks;
  std::size_t start = 2400;
  for (const double amplitude : {0.9, 0.3, 0.05}) {
    for (const double frequency : {82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 659.3}) {
      plucks.push_back({.start = start, .frequency = frequency, .amplitude = amplitude});
      start += 24000;
    }
  }
  return plucks;
}

} // namespace

TEST_CASE("детектор на синтезе: одиночные щипки — по атаке, не дальше 1 мс") {
  const std::vector<Pluck> plucks = singlePlucks();
  const std::vector<float> signal =
      render(plucks, plucks.back().start + 24000,
             SynthSettings{.sampleRate = kSampleRate, .noiseDb = -80.0});
  const std::vector<Onset> onsets = detect(signal);

  REQUIRE(onsets.size() == plucks.size());
  for (std::size_t i = 0; i < plucks.size(); ++i) {
    CAPTURE(i);
    CHECK(std::abs(onsets[i].position - static_cast<double>(plucks[i].start)) <= kMillisecond);
  }
}

TEST_CASE("детектор на синтезе: нарезка потока не меняет результат") {
  const std::vector<Pluck> plucks = evenPlucks(2400, 12000.0, 12, {110.0, 146.8, 196.0}, 0.5);
  const std::vector<float> signal =
      render(plucks, plucks.back().start + 24000,
             SynthSettings{.sampleRate = kSampleRate, .noiseDb = -70.0, .humDb = -50.0});
  const std::vector<Onset> reference = detect(signal, [&signal] { return signal.size(); });
  REQUIRE(!reference.empty());

  std::mt19937 random(7);
  std::uniform_int_distribution<std::size_t> anyLength(1, 3000);
  for (const std::size_t chunk : {std::size_t{1}, std::size_t{64}, std::size_t{512},
                                  std::size_t{1024}, std::size_t{0}}) {
    CAPTURE(chunk);
    const std::vector<Onset> onsets = detect(signal, [chunk, &random, &anyLength] {
      return chunk > 0 ? chunk : anyLength(random);
    });
    REQUIRE(onsets.size() == reference.size());
    for (std::size_t i = 0; i < onsets.size(); ++i)
      CHECK(std::abs(onsets[i].position - reference[i].position) < 1e-9);
  }
}

TEST_CASE("детектор на синтезе: шум и гул ниже порога тишины атак не дают") {
  // Шум −60 dBFS и гул −40 dBFS при пороге −50: гул срезает ВЧ-фильтр, шум
  // ниже порога. Щипок на 2 с — единственная атака.
  const std::vector<Pluck> plucks{{.start = 96000, .frequency = 110.0, .amplitude = 0.3}};
  const std::vector<float> signal =
      render(plucks, 144000,
             SynthSettings{.sampleRate = kSampleRate, .noiseDb = -60.0, .humDb = -40.0});
  const std::vector<Onset> onsets = detect(signal);

  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(onsets[0].position - 96000.0) <= kMillisecond);
}

TEST_CASE("детектор на синтезе: звенящая низкая струна атак не даёт") {
  // Удар и секунда звона: всплеск раз в период, 16 мс, огибающая между
  // всплесками спадает на 14 дБ. Каждый всплеск — рост вдвое за 5 мс, но не
  // выше всплеска периодом раньше.
  const Strike struck = strike(4800, 0.01);
  const std::vector<float> signal = renderRinging({struck}, struck.start + 48000, kRinging);
  const std::vector<Onset> onsets = detect(signal, kQuietDiSilenceDb);

  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(onsets[0].position - static_cast<double>(struck.start)) <= kMillisecond);
}

TEST_CASE("детектор на синтезе: щелчок медиатора и тело ноты — одна атака, на щелчке") {
  // Тихий щелчок, −57 dBFS после ВЧ-фильтра, а через 30 мс за 8 мс разгорается
  // тело ноты, по ВЧ на 10–25 дБ громче щелчка.
  const std::size_t click = 4800;
  for (const double body : {0.3, 0.05}) {
    CAPTURE(body);
    std::vector<float> signal =
        render({{.start = click + 1440, .frequency = 110.0, .amplitude = body, .riseMs = 8.0}},
               click + 24000, SynthSettings{.sampleRate = kSampleRate, .noiseDb = -80.0});
    const std::vector<float> pick = renderRinging(
        {{.start = click, .amplitude = 0.0, .burstAmplitude = 0.0, .pickAmplitude = 0.0022}},
        signal.size(), RingingSettings{.sampleRate = kSampleRate, .pickBursts = 1});
    std::ranges::transform(signal, pick, signal.begin(), std::plus{});

    const std::vector<Onset> onsets = detect(signal, kQuietDiSilenceDb);
    REQUIRE(onsets.size() == 1);
    CHECK(std::abs(onsets[0].position - static_cast<double>(click)) <= kMillisecond);
  }
}

TEST_CASE("детектор на синтезе: удар по звенящей струне находится и чуть выше звона") {
  // Пачка медиатора второго удара всего на 3 дБ выше всплесков звона — пик
  // поднимается меньше чем вдвое. Удар отличает длительность: пять всплесков
  // за 6 мс против одного за период.
  for (const std::size_t gap : {std::size_t{2880}, std::size_t{24000}}) {
    CAPTURE(gap);
    const std::vector<Strike> strikes = twoStrikes(gap, 3.0);
    const std::vector<float> signal =
        renderRinging(strikes, strikes.back().start + 48000, kRinging);
    const std::vector<Onset> onsets = detect(signal, kQuietDiSilenceDb);

    REQUIRE(onsets.size() == 2);
    for (std::size_t i = 0; i < strikes.size(); ++i) {
      CAPTURE(i);
      CHECK(std::abs(onsets[i].position - static_cast<double>(strikes[i].start)) <=
            kMillisecond);
    }
  }
}

TEST_CASE("детектор на синтезе: нарезка не меняет удар по звенящей струне") {
  // Удар по звучащей струне решается позже, чем замечен: решение тоже не должно
  // зависеть от того, где кончился кусок.
  const std::vector<Strike> strikes = twoStrikes(24000, 3.0);
  const std::vector<float> signal =
      renderRinging(strikes, strikes.back().start + 24000, kRinging);
  const std::vector<Onset> reference =
      detect(signal, kQuietDiSilenceDb, [&signal] { return signal.size(); });
  REQUIRE(reference.size() == 2);

  std::mt19937 random(11);
  std::uniform_int_distribution<std::size_t> anyLength(1, 3000);
  for (const std::size_t chunk : {std::size_t{1}, std::size_t{64}, std::size_t{960},
                                  std::size_t{1024}, std::size_t{0}}) {
    CAPTURE(chunk);
    const std::vector<Onset> onsets =
        detect(signal, kQuietDiSilenceDb,
               [chunk, &random, &anyLength] { return chunk > 0 ? chunk : anyLength(random); });
    REQUIRE(onsets.size() == reference.size());
    for (std::size_t i = 0; i < onsets.size(); ++i) {
      CHECK(onsets[i].position == reference[i].position);
      CHECK(onsets[i].levelDb == reference[i].levelDb);
    }
  }
}
