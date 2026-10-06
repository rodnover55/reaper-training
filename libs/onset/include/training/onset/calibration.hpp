#pragma once

#include "training/onset/detector.hpp"
#include "training/onset/high_band.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace training::onset {

/// Шаг калибровки входа.
enum class CalibrationStep {
  /// Гитарист глушит струны, калибровка меряет шум.
  Silence,
  /// Гитарист играет ноты по одной, калибровка меряет их атаку и пик сигнала.
  Notes,
  /// Калибровка закончена, итог — `Calibration::result`.
  Done,
};

/// Чем закончилась калибровка.
enum class Verdict {
  /// Порог тишины можно ставить.
  Good,
  /// Между шумом и самой тихой атакой меньше `kCalibrationMinGapDb`: любой
  /// порог либо пропускает шум, либо теряет слабые удары.
  NoiseTooClose,
  /// Пик сигнала не ниже `kCalibrationClipDb`: вход перегружен.
  Clipping,
  /// Шаг нот кончился по `kCalibrationNotesTimeoutSeconds` без новой ноты, и
  /// нот меньше `kCalibrationNotes`.
  NotEnoughNotes,
};

/// Сколько нот просит сыграть шаг нот.
inline constexpr int kCalibrationNotes = 8;

/// Наименьший зазор между шумом и самой тихой атакой, дБ, при котором порог
/// ставится.
inline constexpr double kCalibrationMinGapDb = 12.0;

/// Пик сигнала, dBFS, с которого вход считается перегруженным.
inline constexpr double kCalibrationClipDb = -1.0;

/// Длина шага тишины, с звука.
inline constexpr double kCalibrationSilenceSeconds = 3.0;

/// Сколько секунд звука без новой ноты обрывают шаг нот.
inline constexpr double kCalibrationNotesTimeoutSeconds = 15.0;

/// Сколько секунд звука после итога звук не отмечается `soundAfterResult`.
inline constexpr double kCalibrationCloseAfterSeconds = 5.0;

/// Наименьший уровень, dBFS: тише калибровка не меряет, цифровая тишина — он.
inline constexpr double kCalibrationFloorDb = -120.0;

/// Итог калибровки. Уровни — в dBFS; шум и атаки — по звуку выше 1 кГц, как его
/// видит поиск атаки, пик — по всему звуку.
struct CalibrationResult {
  Verdict verdict = Verdict::NotEnoughNotes;

  /// Уровень шума: наибольший уровень звука выше 1 кГц на шаге тишины.
  double noiseDb = kCalibrationFloorDb;

  /// Сколько нот найдено: от 0 до `kCalibrationNotes`.
  int notes = 0;

  /// Самая тихая и самая громкая атака. При `notes` = 0 — `kCalibrationFloorDb`.
  double softestDb = kCalibrationFloorDb;
  double loudestDb = kCalibrationFloorDb;

  /// Пик сигнала на шаге нот.
  double peakDb = kCalibrationFloorDb;

  /// Порог тишины: середина между `noiseDb` и `softestDb`, округлённая до
  /// целого, половина — от нуля. Не прижат к границам настройки. При
  /// `notes` = 0 — `kCalibrationFloorDb`.
  double thresholdDb = kCalibrationFloorDb;
};

/// Подводит итог калибровки по измеренным уровням: порог тишины и вердикт.
/// Вердикт — первый подходящий по порядку: нот меньше `kCalibrationNotes` —
/// `NotEnoughNotes`; пик не ниже `kCalibrationClipDb` — `Clipping`; зазор
/// `softestDb − noiseDb` меньше `kCalibrationMinGapDb` — `NoiseTooClose`;
/// иначе — `Good`.
///
/// @param noiseDb уровень шума, dBFS.
/// @param attacksDb уровни атак найденных нот, dBFS, в любом порядке; больше
///   `kCalibrationNotes` не бывает.
/// @param peakDb пик сигнала, dBFS.
CalibrationResult judgeCalibration(double noiseDb, std::span<const double> attacksDb,
                                   double peakDb);

/// Калибровка входа: по звуку одного канала меряет уровень шума, уровни атаки
/// нот и пик сигнала и подводит итог (`judgeCalibration`).
///
/// Звук подаётся кусками любой длины, время шагов считается в сэмплах потока:
///
/// 1. **Тишина** — `kCalibrationSilenceSeconds` звука, начиная через 0.1 с
///    после начала потока. Шум — наибольший уровень звука выше 1 кГц. Если в
///    тишине прозвучал звук громче обычного для шага больше чем на 20 дБ, шаг
///    отсчитывается заново после этого звука и его затухания
///    (`secondsSinceSound`).
/// 2. **Ноты** — пока не найдено `kCalibrationNotes` нот и ещё 1 с после
///    последней или пока `kCalibrationNotesTimeoutSeconds` нет новой ноты. Ноты
///    ищет `Detector` с порогом тишины на 6 дБ выше шума. Уровень атаки —
///    наибольший уровень звука выше 1 кГц за 5 мс от её начала, то есть щелчок
///    медиатора. Ноты сверх `kCalibrationNotes` не в счёт.
/// 3. **Итог** — `result`. Звук слушается дальше: через
///    `kCalibrationCloseAfterSeconds` после итога звук громче шума больше чем
///    на 20 дБ отмечается (`soundAfterResult`).
///
/// Ход калибровки не зависит от того, как поток нарезан на куски: те же сэмплы
/// дают те же шаги на тех же сэмплах, тот же итог и ту же отметку звука после
/// него. От нарезки зависит только `levelDb`.
///
/// Многопоточность: один экземпляр — один поток вызовов.
class Calibration {
public:
  /// Начинает калибровку с шага тишины. Выделяет память под историю звука
  /// за 0.1 с.
  ///
  /// @param sampleRate частота дискретизации потока, Гц; больше 2000.
  explicit Calibration(double sampleRate);

  /// Обрабатывает следующий кусок потока.
  void process(std::span<const float> samples);

  /// Сообщает о разрыве потока: следующий кусок не продолжает прошлый.
  /// Фильтры начинают заново; шаг тишины начинается заново, не считая это
  /// звуком (`secondsSinceSound` не меняется); на шаге нот найденные ноты
  /// остаются, а ноты, уровень атаки которых ещё не измерен, пропадают; после
  /// итога первые 0.1 с звука не слушаются.
  void discontinuity();

  /// Частота дискретизации потока, Гц, — та, что передана в конструктор.
  double sampleRate() const { return sampleRate_; }

  CalibrationStep step() const { return step_; }

  /// Сколько секунд звука осталось до конца шага тишины: от
  /// `kCalibrationSilenceSeconds` до 0. Вне шага тишины — 0.
  double silenceLeft() const;

  /// Сколько секунд звука прошло с тех пор, как шаг тишины в последний раз
  /// начался заново из-за звука; пусто, если такого не было.
  std::optional<double> secondsSinceSound() const;

  /// Уровень шума, dBFS; до конца шага тишины — `kCalibrationFloorDb`.
  double noiseDb() const { return noiseDb_; }

  /// Наибольший уровень звука выше 1 кГц за последний непустой кусок, dBFS,
  /// не ниже `kCalibrationFloorDb`; до первого куска — `kCalibrationFloorDb`.
  double levelDb() const;

  /// Уровни атаки найденных нот, dBFS, в порядке нахождения.
  const std::vector<double> &attacksDb() const { return attacks_; }

  /// Сколько секунд звука шаг нот идёт без новой ноты: с начала шага или с
  /// последней ноты. Вне шага нот — 0.
  double secondsWithoutNotes() const;

  /// Итог; пусто, пока калибровка идёт.
  const std::optional<CalibrationResult> &result() const { return result_; }

  /// Правда, если после итога прошло `kCalibrationCloseAfterSeconds` звука, а
  /// потом прозвучал звук выше 1 кГц громче шума больше чем на 20 дБ — удар
  /// по струнам. Однажды став правдой, не меняется.
  bool soundAfterResult() const { return soundAfterResult_; }

private:
  void processSilence(float sample);
  void closeWindow();
  void restartSilence();
  void startNotes();
  std::size_t processNotes(std::span<const float> samples);
  void checkpoint();
  void processDone(std::span<const float> samples);
  void finish();
  double seconds(std::uint64_t samples) const;

  double sampleRate_ = 0.0;
  HighPass highPass_;
  Envelope envelope_;

  // Сколько сэмплов: отзвон фильтра после начала потока, окно шага тишины,
  // окон в шаге, атака, история огибающей, ожидание после последней ноты и
  // обрыв без нот.
  std::uint64_t settleLength_ = 0;
  std::uint64_t windowLength_ = 0;
  std::size_t windowsPerStep_ = 0;
  std::uint64_t attackLength_ = 0;
  std::uint64_t historyLength_ = 0;
  std::uint64_t afterLastLength_ = 0;
  std::uint64_t timeoutLength_ = 0;
  std::uint64_t closeAfterLength_ = 0;

  CalibrationStep step_ = CalibrationStep::Silence;

  // Номер следующего сэмпла потока.
  std::uint64_t position_ = 0;

  // Шаг тишины: сэмплы до `settleUntil_` не в счёт; наибольшие огибающие
  // закрытых окон и текущего окна; когда шаг начат заново из-за звука.
  std::uint64_t settleUntil_ = 0;
  std::vector<double> windows_;
  double windowMax_ = 0.0;
  std::uint64_t windowFill_ = 0;
  std::optional<std::uint64_t> soundAt_;

  double noiseDb_ = kCalibrationFloorDb;
  double lastLevel_ = 0.0;

  // Шаг нот: детектор и с какого сэмпла потока он начал; огибающая последних
  // `historyLength_` сэмплов кольцом по номеру сэмпла; начала атак, уровень
  // которых ещё меряется; найденные атаки; пик; когда шаг начался или пришла
  // последняя нота; когда кончится шаг после восьмой ноты.
  std::optional<Detector> detector_;
  std::uint64_t detectorStart_ = 0;
  std::vector<double> history_;
  std::vector<std::uint64_t> pending_;
  std::vector<Onset> found_;
  std::vector<double> attacks_;
  double peak_ = 0.0;
  std::uint64_t lastNoteAt_ = 0;
  std::optional<std::uint64_t> finishAt_;

  // Итог, когда он подведён, и звук после него.
  std::optional<CalibrationResult> result_;
  std::uint64_t doneAt_ = 0;
  bool soundAfterResult_ = false;
};

} // namespace training::onset
