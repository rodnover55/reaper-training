#pragma once

// Чтение записей для тестов: WAV, PCM 24 бит, один канал — так сохранены
// записи в `tests/data`.

#include <filesystem>
#include <vector>

namespace training::test {

/// Звук из файла.
struct Recording {
  /// Частота дискретизации, Гц.
  double sampleRate = 0.0;

  /// Сэмплы, доли полной шкалы.
  std::vector<float> samples;
};

/// Читает WAV с PCM 24 бит в одном канале. Другой формат или битый файл —
/// исключение `std::runtime_error` с объяснением.
Recording readWav(const std::filesystem::path &path);

} // namespace training::test
