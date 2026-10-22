#pragma once

// Компенсация задержки входов у REAPER (`hit-timing`).

#include "training/core/clock.hpp"
#include "training/reaper/input.hpp"

namespace training::reaper {

/// Компенсация задержки входов — сдвиг, на который REAPER ставит запись.
struct Compensation {
  /// Компенсация для звука в реальном времени.
  core::Interval audio;

  /// Частота звуковой карты, по которой снята компенсация звука, Гц; 0 —
  /// неизвестна, и компенсация звука — 0.
  double audioSampleRate = 0.0;

  /// Задержка ввода звука: сколько звук идёт от входа звуковой карты до
  /// тренажёра; ноль, если частота неизвестна. Для замера задержки показа.
  core::Interval inputLatency;

  /// Компенсация для нот MIDI в реальном времени.
  core::Interval midi;

  /// Частота звуковой карты, по которой снята компенсация нот MIDI, Гц; 0 —
  /// неизвестна, и компенсация нот MIDI — 0.
  double midiSampleRate = 0.0;

  /// Компенсация задержки звука в реальном времени — та, на которую хост
  /// сдвигает запись.
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
  static core::Interval audioOf(int inputLatency, int outputLatency, int manualInput,
                                int manualOutput, bool driverLatency, double sampleRate);

  /// Компенсация задержки для нот MIDI в реальном времени — та, на которую
  /// хост сдвигает запись MIDI от позиции ноты в истории MIDI-входа. Задержка
  /// и ручная поправка входа в неё не входят.
  ///
  /// @param blockSize размер блока звуковой карты, сэмплов.
  /// @param outputLatency задержка выхода из `GetInputOutputLatency`, сэмплов;
  ///   ручная поправка выхода уже в ней.
  /// @param manualOutput ручная поправка выхода из настроек (`adjrecmanlat`),
  ///   сэмплов.
  /// @param driverLatency включена ли галка «Use audio driver reported
  ///   latency» (`adjreclat` ≠ 0).
  /// @param sampleRate частота дискретизации, Гц; больше нуля.
  /// @return (блок + выход) / частота при включённой галке, иначе (блок +
  ///   ручная поправка выхода) / частота.
  static core::Interval midiOf(int blockSize, int outputLatency, int manualOutput,
                               bool driverLatency, double sampleRate);

  /// Компенсация входа вида `input`: звука или нот MIDI.
  core::Interval of(Input input) const { return input == Input::Midi ? midi : audio; }

  /// Правда, если у компенсаций совпадают все поля.
  bool operator==(const Compensation &) const = default;
};

} // namespace training::reaper
