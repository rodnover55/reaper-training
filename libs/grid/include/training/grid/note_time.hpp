#pragma once

// Время ноты на шкале проекта по тому же правилу, по которому хост ставит
// запись (design.md D2, findings.md R2).

namespace training::grid {

/// Компенсация задержки в реальных секундах — та, на которую хост сдвигает
/// запись.
///
/// @param inputLatency, outputLatency задержки из `GetInputOutputLatency`,
///   сэмплов; ручные поправки уже в них.
/// @param manualInput, manualOutput ручные поправки входа и выхода из
///   настроек (`adjrecmanlatin`, `adjrecmanlat`), сэмплов.
/// @param driverLatency включена ли галка «Use audio driver reported
///   latency» (`adjreclat` ≠ 0).
/// @param sampleRate частота дискретизации, Гц; больше нуля.
/// @return (вход + выход) / частота при включённой галке, иначе (ручная
///   поправка входа + выхода) / частота.
double compensation(int inputLatency, int outputLatency, int manualInput, int manualOutput,
                    bool driverLatency, double sampleRate);

/// Компенсация задержки для нот MIDI в реальных секундах — та, на которую
/// хост сдвигает запись MIDI от позиции ноты в истории MIDI-входа (design.md
/// D2 изменения add-midi-input). Задержка и ручная поправка входа в неё не
/// входят.
///
/// @param blockSize размер блока звуковой карты, сэмплов.
/// @param outputLatency задержка выхода из `GetInputOutputLatency`, сэмплов;
///   ручная поправка выхода уже в ней.
/// @param manualOutput ручная поправка выхода из настроек (`adjrecmanlat`),
///   сэмплов.
/// @param driverLatency включена ли галка «Use audio driver reported
///   latency» (`adjreclat` ≠ 0).
/// @param sampleRate частота дискретизации, Гц; больше нуля.
/// @return (блок + выход) / частота при включённой галке, иначе (блок + ручная
///   поправка выхода) / частота.
double midiCompensation(int blockSize, int outputLatency, int manualOutput, bool driverLatency,
                        double sampleRate);

/// Время ноты на шкале проекта, с: `blockPosition + (offset / sampleRate −
/// compensation) × rate`.
///
/// @param blockPosition позиция обработки блока, с.
/// @param offset начало атаки от начала блока, сэмплов (дробное).
/// @param sampleRate частота дискретизации, Гц; больше нуля.
/// @param compensation компенсация задержки, реальные секунды.
/// @param rate скорость воспроизведения проекта; больше нуля.
double noteTime(double blockPosition, double offset, double sampleRate, double compensation,
                double rate);

/// Заворачивает время в петлю [loopStart, loopEnd): прибавляет или вычитает
/// длину петли, пока время не окажется внутри. Петля нулевой или
/// отрицательной длины время не меняет.
double wrapIntoLoop(double time, double loopStart, double loopEnd);

} // namespace training::grid
