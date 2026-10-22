#pragma once

// Общие для шагов калибровки переводы: время в сэмплы и амплитуда в dBFS.

#include "training/core/calibration/step.hpp"
#include "training/core/clock.hpp"
#include "training/core/input/audio_block.hpp"
#include "training/core/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace training::core::calibration {

/// Длина `length` в сэмплах при частоте `rate` с округлением до целого; хотя
/// бы один сэмпл.
inline std::uint64_t samplesOf(Interval length, SampleRate rate) {
  return static_cast<std::uint64_t>(
      std::max(1LL, std::llround(length / Interval::kSecond * rate.hz())));
}

/// Длительность `samples` сэмплов при частоте `rate`.
inline Interval lengthOf(std::uint64_t samples, SampleRate rate) {
  return static_cast<double>(samples) / rate.hz() * Interval::kSecond;
}

/// Амплитуда `amplitude` в dBFS, не ниже `kFloorDb`; нулевая или
/// отрицательная амплитуда — `kFloorDb`.
inline double decibels(double amplitude) {
  return amplitude > 0.0 ? std::max(kFloorDb, 20.0 * std::log10(amplitude)) : kFloorDb;
}

/// Уровень `db` dBFS в долях полной шкалы.
inline double amplitudeOf(double db) { return std::pow(10.0, db / 20.0); }

/// Копирует сэмплы `samples` в `buffer` числами двойной точности: по ним идут
/// фильтры. Память буфера растёт до самого длинного блока и дальше не
/// выделяется.
inline std::span<double> copied(std::span<const float> samples, std::vector<double> &buffer) {
  buffer.resize(std::max(buffer.size(), samples.size()));
  const std::span<double> copy(buffer.data(), samples.size());
  std::ranges::copy(samples, copy.begin());
  return copy;
}

/// Время конца блока `block` при частоте `rate` — время сэмпла после
/// последнего.
inline Moment endOf(const input::AudioBlock &block, SampleRate rate) {
  return block.start + lengthOf(block.samples.size(), rate);
}

/// Сколько сэмплов частоты `rate` пропущено между концом полученного звука
/// `heardEnd` и началом блока `block`, в целых сэмплах: расхождение меньше
/// половины сэмпла — округление времени. Ноль, когда звука ещё не было или
/// блок начинается не позже конца полученного звука.
inline std::uint64_t missedOf(const std::optional<Moment> &heardEnd,
                              const input::AudioBlock &block, SampleRate rate) {
  if (!heardEnd)
    return 0;
  const long long away =
      std::llround((block.start - *heardEnd) / Interval::kSecond * rate.hz());
  return away > 0 ? static_cast<std::uint64_t>(away) : 0;
}

/// Подаёт в `process` `count` сэмплов тишины кусками до 1024 сэмплов.
template <typename Process> void silence(std::uint64_t count, Process &&process) {
  static constexpr std::array<float, 1024> kZeros{};
  while (count > 0) {
    const auto length =
        static_cast<std::size_t>(std::min<std::uint64_t>(count, kZeros.size()));
    process(std::span<const float>(kZeros).first(length));
    count -= length;
  }
}

} // namespace training::core::calibration
