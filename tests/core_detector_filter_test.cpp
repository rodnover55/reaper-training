#include <doctest/doctest.h>

#include "synth.hpp"

#include "training/core/detector/detector.hpp"
#include "training/core/detector/filter.hpp"
#include "training/core/detector/high_band.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using training::core::SampleRate;
using training::core::detector::Detector;
using training::core::detector::Envelope;
using training::core::detector::Filter;
using training::core::detector::HighPass;
using training::core::detector::Serial;
using training::core::detector::Settings;
using training::test::Pluck;
using training::test::render;
using training::test::SynthSettings;

namespace {

const SampleRate kRate = *SampleRate::create(48000.0);

/// Удар по струне на тихом шуме: 0.2 с.
std::vector<double> struck() {
  const std::vector<float> samples =
      render({Pluck{.start = 480, .amplitude = 0.3}}, 9600, SynthSettings{.noiseDb = -90.0});
  return {samples.begin(), samples.end()};
}

/// Сэмплы `samples` после ВЧ-фильтра детектора и огибающей, поданные пачками
/// по `block` сэмплов.
std::vector<double> filtered(std::vector<double> samples, std::size_t block) {
  HighPass hpf = *HighPass::create(Settings{}.highPassHz, kRate);
  Envelope envelope(kRate);
  for (std::size_t from = 0; from < samples.size(); from += block) {
    const std::span<double> piece =
        std::span(samples).subspan(from, std::min(block, samples.size() - from));
    hpf.process(piece);
    envelope.process(piece);
  }
  return samples;
}

/// Фильтр, который умножает сэмплы на `factor`.
class Scale final : public Filter {
public:
  Scale(double factor, SampleRate rate) : factor_(factor), rate_(rate) {}

  void process(std::span<double> samples) override {
    for (double &sample : samples)
      sample *= factor_;
  }

  SampleRate sampleRate() const override { return rate_; }

private:
  double factor_;
  SampleRate rate_;
};

/// Цепочка из фильтров, которые умножают на `factors`, частоты `rate`.
std::optional<Serial> chain(std::initializer_list<double> factors, SampleRate rate = kRate) {
  std::vector<std::unique_ptr<Filter>> filters;
  for (const double factor : factors)
    filters.push_back(std::make_unique<Scale>(factor, rate));
  return Serial::create(kRate, std::move(filters));
}

} // namespace

TEST_CASE("Цепочка фильтров: каждый следующий получает результат предыдущего") {
  std::optional<Serial> serial = chain({2.0, 3.0});
  REQUIRE(serial);
  CHECK(serial->sampleRate() == kRate);

  std::vector<double> samples{1.0, -0.5, 0.25};
  serial->process(samples);
  CHECK(samples == std::vector<double>{6.0, -3.0, 1.5});
}

TEST_CASE("Цепочка фильтров: без фильтров возвращает сэмплы как есть") {
  std::optional<Serial> serial = chain({});
  REQUIRE(serial);
  CHECK(serial->sampleRate() == kRate);

  std::vector<double> samples{1.0, -0.5, 0.25};
  serial->process(samples);
  CHECK(samples == std::vector<double>{1.0, -0.5, 0.25});
}

TEST_CASE("Цепочка фильтров: с другой частотой и с пустым фильтром не создаётся") {
  CHECK_FALSE(chain({1.0}, *SampleRate::create(44100.0)));

  std::vector<std::unique_ptr<Filter>> filters;
  filters.push_back(std::make_unique<Scale>(1.0, kRate));
  filters.push_back(std::make_unique<Scale>(1.0, *SampleRate::create(44100.0)));
  CHECK_FALSE(Serial::create(kRate, std::move(filters)));

  std::vector<std::unique_ptr<Filter>> empty;
  empty.push_back(nullptr);
  CHECK_FALSE(Serial::create(kRate, std::move(empty)));
}

TEST_CASE("Фильтры детектора: частота потока и тот же результат через Filter") {
  HighPass hpf = *HighPass::create(1000.0, kRate);
  const Envelope envelope(kRate);
  CHECK(hpf.sampleRate() == kRate);
  CHECK(envelope.sampleRate() == kRate);

  std::vector<double> direct = struck();
  std::vector<double> through = direct;
  HighPass other = *HighPass::create(1000.0, kRate);
  hpf.process(direct);
  static_cast<Filter &>(other).process(through);
  CHECK(direct == through);
}

TEST_CASE("Детектор: filter — ВЧ-фильтр и огибающая с настройками детектора, сброшенные") {
  Detector detector = *Detector::create(Settings{.sampleRate = kRate});
  const std::vector<double> signal = struck();
  const std::vector<double> expected = filtered(signal, 256);

  Serial first = detector.filter();
  CHECK(first.sampleRate() == kRate);
  std::vector<double> samples = signal;
  first.process(samples);
  CHECK(samples == expected);

  // Детектор, разобравший звук, отдаёт фильтр в сброшенном состоянии и свой
  // ход не меняет.
  const std::vector<float> stream(signal.begin(), signal.end());
  detector.process(stream);
  Serial second = detector.filter();
  std::vector<double> again = signal;
  second.process(again);
  CHECK(again == expected);
}
