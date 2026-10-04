#include "synth.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <random>

namespace training::test {
namespace {

/// За сколько миллисекунд медиатор глушит звучащую струну.
constexpr double kMuteMs = 2.0;

double gainOf(double db) { return db < -200.0 ? 0.0 : std::pow(10.0, db / 20.0); }

/// Добавляет в `out` щипок по Карплусу — Стронгу: линия задержки длиной в
/// период тона заполняется пачкой шума, дальше каждый проход по ней —
/// усреднение соседних отсчётов с затуханием. Звук обрывается за `kMuteMs` до
/// `stop`, если тот задан.
void addPluck(std::vector<double> &out, const Pluck &pluck, std::size_t stop,
              const SynthSettings &settings, std::mt19937 &random) {
  const auto period = std::max<std::size_t>(
      2, static_cast<std::size_t>(settings.sampleRate / pluck.frequency));

  // Пачка шума чуть приглушена фильтром: медиатор возбуждает струну не
  // бесконечно ярко.
  std::uniform_real_distribution<double> uniform(-1.0, 1.0);
  std::vector<double> line(period);
  double smooth = 0.0;
  for (double &value : line) {
    smooth = 0.6 * uniform(random) + 0.4 * smooth;
    value = smooth * pluck.amplitude;
  }

  // Затухание за сэмпл из затухания за секунду.
  const double loss = std::pow(settings.decayPerSecond, 1.0 / settings.sampleRate);
  const auto mute = static_cast<std::size_t>(kMuteMs * settings.sampleRate / 1000.0);

  std::size_t index = 0;
  for (std::size_t n = pluck.start; n < out.size() && n < stop; ++n) {
    double gain = 1.0;
    if (stop != out.size() && n + mute >= stop)
      gain = static_cast<double>(stop - n) / static_cast<double>(mute);

    const double sample = line[index];
    out[n] += sample * gain;

    const std::size_t next = (index + 1) % period;
    line[index] = loss * 0.5 * (line[index] + line[next]);
    index = next;
  }
}

} // namespace

std::vector<float> render(const std::vector<Pluck> &plucks, std::size_t length,
                          const SynthSettings &settings) {
  std::mt19937 random(settings.seed);
  std::vector<double> mix(length, 0.0);

  // Щипки каждой струны по времени: следующий щипок струны глушит прошлый.
  std::map<int, std::vector<Pluck>> strings;
  for (const Pluck &pluck : plucks)
    strings[pluck.string].push_back(pluck);

  for (auto &[string, sequence] : strings) {
    std::ranges::sort(sequence, {}, &Pluck::start);
    for (std::size_t i = 0; i < sequence.size(); ++i) {
      const std::size_t stop = i + 1 < sequence.size() ? sequence[i + 1].start : length;
      addPluck(mix, sequence[i], stop, settings, random);
    }
  }

  // Шум и гул звучат и до первого щипка; перегруз — после сложения.
  std::uniform_real_distribution<double> uniform(-1.0, 1.0);
  const double noise = gainOf(settings.noiseDb);
  const double hum = gainOf(settings.humDb);
  const double norm = settings.drive > 0.0 ? std::tanh(settings.drive) : 1.0;

  std::vector<float> out(length);
  for (std::size_t n = 0; n < length; ++n) {
    double value = mix[n];
    if (noise > 0.0)
      value += noise * uniform(random);
    if (hum > 0.0)
      value += hum * std::sin(2.0 * std::numbers::pi * 50.0 * static_cast<double>(n) /
                              settings.sampleRate);
    if (settings.drive > 0.0)
      value = std::tanh(settings.drive * value) / norm;
    out[n] = static_cast<float>(value);
  }

  return out;
}

std::vector<Pluck> evenPlucks(std::size_t first, double interval, std::size_t count,
                              const std::vector<double> &frequencies, double amplitude) {
  std::vector<Pluck> plucks;
  plucks.reserve(count);
  for (std::size_t i = 0; i < count; ++i)
    plucks.push_back({.start = first + static_cast<std::size_t>(
                                           std::llround(interval * static_cast<double>(i))),
                      .frequency = frequencies[i % frequencies.size()],
                      .amplitude = amplitude});
  return plucks;
}

} // namespace training::test
