#include "wav.hpp"

#include <fmt/format.h>
#include <fmt/std.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string_view>

namespace training::test {
namespace {

/// Полная шкала 24-битного сэмпла.
constexpr double kFullScale = 8388608.0;

/// Беззнаковое число из `size` байт little-endian, начиная с `offset`.
std::uint32_t littleEndian(std::span<const std::uint8_t> bytes, std::size_t offset,
                           std::size_t size) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < size; ++i)
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
  return value;
}

/// Стоит ли с `offset` четырёхбуквенный код `id`.
bool hasId(std::span<const std::uint8_t> bytes, std::size_t offset, std::string_view id) {
  for (std::size_t i = 0; i < id.size(); ++i)
    if (bytes[offset + i] != static_cast<std::uint8_t>(id[i]))
      return false;
  return true;
}

} // namespace

Recording readWav(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error(fmt::format("не открывается {}", path));

  std::vector<std::uint8_t> bytes;
  for (auto byte = std::istreambuf_iterator<char>(file);
       byte != std::istreambuf_iterator<char>(); ++byte)
    bytes.push_back(static_cast<std::uint8_t>(*byte));

  const std::span<const std::uint8_t> all(bytes);
  if (all.size() < 12 || !hasId(all, 0, "RIFF") || !hasId(all, 8, "WAVE"))
    throw std::runtime_error(fmt::format("{}: не WAV", path));

  Recording recording;
  bool hasFormat = false;
  bool hasData = false;

  // Файл — цепочка блоков: код, длина, тело; тело нечётной длины добито байтом.
  for (std::size_t offset = 12; offset + 8 <= all.size();) {
    const std::size_t size = littleEndian(all, offset + 4, 4);
    const std::size_t body = offset + 8;
    if (body + size > all.size())
      throw std::runtime_error(fmt::format("{}: блок обрезан", path));

    if (hasId(all, offset, "fmt ")) {
      const std::uint32_t format = littleEndian(all, body, 2);
      const std::uint32_t channels = littleEndian(all, body + 2, 2);
      const std::uint32_t bits = littleEndian(all, body + 14, 2);
      if (format != 1 || channels != 1 || bits != 24)
        throw std::runtime_error(fmt::format(
            "{}: нужен PCM 24 бит в одном канале, а тут формат {}, каналов {}, бит {}", path,
            format, channels, bits));
      recording.sampleRate = littleEndian(all, body + 4, 4);
      hasFormat = true;
    } else if (hasId(all, offset, "data")) {
      if (!hasFormat)
        throw std::runtime_error(fmt::format("{}: звук раньше формата", path));
      recording.samples.reserve(size / 3);
      for (std::size_t at = body; at + 3 <= body + size; at += 3) {
        // 24 бита со знаком: старший бит третьего байта — знак.
        const auto value = static_cast<std::int32_t>(littleEndian(all, at, 3) << 8) >> 8;
        recording.samples.push_back(static_cast<float>(value / kFullScale));
      }
      hasData = true;
    }

    offset = body + size + (size & 1);
  }

  if (!hasData)
    throw std::runtime_error(fmt::format("{}: нет звука", path));
  return recording;
}

} // namespace training::test
