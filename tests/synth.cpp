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
  const auto rise = static_cast<std::size_t>(pluck.riseMs * settings.sampleRate / 1000.0);

  std::size_t index = 0;
  for (std::size_t n = pluck.start; n < out.size() && n < stop; ++n) {
    double gain = 1.0;
    if (stop != out.size() && n + mute >= stop)
      gain = static_cast<double>(stop - n) / static_cast<double>(mute);

    // Нарастание считается с первого сэмпла, чтобы тот остался ненулевым.
    if (n - pluck.start < rise)
      gain *= static_cast<double>(n - pluck.start + 1) / static_cast<double>(rise);

    const double sample = line[index];
    out[n] += sample * gain;

    const std::size_t next = (index + 1) % period;
    line[index] = loss * 0.5 * (line[index] + line[next]);
    index = next;
  }
}

/// Сэмпл, ближайший к моменту `start + milliseconds`.
std::size_t after(std::size_t start, double milliseconds, double sampleRate) {
  return start + static_cast<std::size_t>(std::llround(milliseconds * sampleRate / 1000.0));
}

/// Добавляет в `out` ВЧ-всплеск с амплитудой `amplitude` от сэмпла `start` до
/// `stop`, умножая на `fade` — множитель от номера сэмпла.
template <class Fade>
void addBurst(std::vector<double> &out, std::size_t start, double amplitude, std::size_t stop,
              const RingingSettings &settings, Fade fade) {
  // Десять постоянных затухания — всплеск стих до 5e-5.
  const std::size_t length = after(0, 10.0 * settings.burstMs, settings.sampleRate);
  for (std::size_t i = 0; i < length && start + i < stop; ++i) {
    const double t = static_cast<double>(i) / settings.sampleRate;
    out[start + i] += fade(start + i) * amplitude *
                      std::sin(2.0 * std::numbers::pi * settings.burstHz * t) *
                      std::exp(-t * 1000.0 / settings.burstMs);
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

std::vector<float> renderRinging(const std::vector<Strike> &strikes, std::size_t length,
                                 const RingingSettings &settings) {
  std::vector<double> mix(length, 0.0);
  const double sampleRate = settings.sampleRate;
  const double period = sampleRate / settings.frequency;
  const auto mute = after(0, kMuteMs, sampleRate);

  for (std::size_t k = 0; k < strikes.size(); ++k) {
    const Strike &strike = strikes[k];
    const std::size_t stop =
        k + 1 < strikes.size() ? std::min(length, strikes[k + 1].start) : length;

    // Следующий удар глушит звон за 2 мс до своего начала, как в `render`.
    const auto fade = [stop, length, mute](std::size_t n) {
      return stop != length && n + mute >= stop
                 ? static_cast<double>(stop - n) / static_cast<double>(mute)
                 : 1.0;
    };

    for (int i = 0; i < settings.pickBursts; ++i)
      addBurst(mix, after(strike.start, i * settings.pickSpacingMs, sampleRate),
               strike.pickAmplitude, stop, settings, fade);

    // Звон — с конца пачки медиатора: тон с нуля и всплеск в начале каждого
    // периода, всё затухает вместе.
    const std::size_t release =
        after(strike.start, settings.pickBursts * settings.pickSpacingMs, sampleRate);
    const auto decay = [&settings, release](std::size_t n) {
      return std::pow(settings.decayPerSecond,
                      static_cast<double>(n - release) / settings.sampleRate);
    };

    for (std::size_t n = release; n < stop; ++n)
      mix[n] += fade(n) * decay(n) * strike.amplitude *
                std::sin(2.0 * std::numbers::pi * settings.frequency *
                         static_cast<double>(n - release) / sampleRate);

    for (std::size_t m = 0;; ++m) {
      const std::size_t burst =
          release + static_cast<std::size_t>(std::llround(static_cast<double>(m) * period));
      if (burst >= stop)
        break;
      addBurst(mix, burst, strike.burstAmplitude * decay(burst), stop, settings, fade);
    }
  }

  std::vector<float> out(length);
  std::ranges::transform(mix, out.begin(),
                         [](double value) { return static_cast<float>(value); });
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
