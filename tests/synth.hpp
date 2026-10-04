#pragma once

// Синтетическая гитара для тестов поиска атаки: щипки струны с заранее
// известным началом.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace training::test {

/// Щипок струны.
struct Pluck {
  /// Номер сэмпла, с которого звучит щипок: первый ненулевой сэмпл щипка стоит
  /// ровно здесь.
  std::size_t start = 0;

  /// Частота основного тона, Гц; больше нуля и меньше половины частоты
  /// дискретизации.
  double frequency = 110.0;

  /// Наибольшая амплитуда возбуждения, доли полной шкалы.
  double amplitude = 0.5;

  /// Струна. Новый щипок той же струны глушит прошлый за 2 мс, как медиатор,
  /// упавший на звучащую струну; щипки разных струн звучат вместе.
  int string = 0;
};

/// Что звучит, кроме щипков, и как звук искажается.
struct SynthSettings {
  double sampleRate = 48000.0;

  /// Белый шум, dBFS по пику; меньше −200 — шума нет.
  double noiseDb = -240.0;

  /// Гул 50 Гц, dBFS по пику; меньше −200 — гула нет.
  double humDb = -240.0;

  /// Сила перегруза: 0 — чистый звук, больше — мягкое ограничение
  /// `tanh(drive · x) / tanh(drive)`, звук плотнее и длиннее.
  double drive = 0.0;

  /// Во сколько раз за секунду затихает струна: 0.5 — вдвое.
  double decayPerSecond = 0.3;

  /// Зерно случайных чисел: одно зерно — один и тот же сигнал.
  std::uint32_t seed = 1;
};

/// Синтезирует `length` сэмплов: щипки по Карплусу — Стронгу, шум, гул и
/// перегруз из `settings`. Щипки могут идти в любом порядке.
std::vector<float> render(const std::vector<Pluck> &plucks, std::size_t length,
                          const SynthSettings &settings);

/// Щипки ровной сеткой на одной струне: `count` нот через `interval` сэмплов,
/// первая — на `first`, частоты по кругу из `frequencies`, амплитуда
/// `amplitude`.
std::vector<Pluck> evenPlucks(std::size_t first, double interval, std::size_t count,
                              const std::vector<double> &frequencies, double amplitude);

} // namespace training::test
