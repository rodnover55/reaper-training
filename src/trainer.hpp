#pragma once

// Тренажёр в главном потоке — связка ядра тренажёра с REAPER: читает REAPER,
// передаёт данные ядру (`training::trainer`) и исполняет его решения — журнал,
// сохранение настроек.

#include "audio_input.hpp"
#include "midi_inputs.hpp"
#include "project_timeline.hpp"
#include "settings.hpp"

#include "training/trainer/calibration.hpp"
#include "training/trainer/count_in.hpp"
#include "training/trainer/measurement.hpp"
#include "training/trainer/midi_notes.hpp"
#include "training/trainer/transport.hpp"

#include <optional>
#include <vector>

namespace training::reaper {

using trainer::CalibrationSession;
using trainer::Measurement;
using trainer::Measuring;

/// Тренажёр: вход звуковой карты или вход MIDI, настройки и строки тактов.
///
/// Многопоточность: только главный поток.
class Trainer {
public:
  /// Читает сохранённые настройки (`loadSettings`), пишет в журнал каждую не
  /// принятую строку `reaper-extstate.ini` и отдаёт настройки входу и своду.
  Trainer();

  const Settings &settings() const { return settings_; }

  /// Сообщение о не принятых строках `reaper-extstate.ini` в строке
  /// состояния окна. Запуск транспорта (`onPlayState`) и итог калибровки
  /// снимают его сами; открытие окна и правки настроек в окне отмечает окно.
  trainer::FileNotice &fileNotice() { return fileNotice_; }
  const trainer::FileNotice &fileNotice() const { return fileNotice_; }

  /// Меняет и сохраняет настройки (`saveSettings`).
  /// Режим действует на удары, которые ещё не начались, окно попадания — на
  /// значения, которые появятся после смены, канал и порог — со следующего
  /// блока, вход MIDI — с нот, пришедших после смены. Смена входа кончает
  /// калибровку и ищет выбранный вход MIDI по имени (`findMidiInput`).
  void setSettings(const Settings &settings);

  /// Меняет настройки, как `setSettings`, но не сохраняет их. Для частых
  /// правок, например пока тянут ползунок шкалы; сохранить — `setSettings`
  /// по окончании.
  void previewSettings(const Settings &settings);

  /// Строки тактов последнего запуска транспорта (`trainer::Transport::bars`).
  const grid::Bars &bars() const { return transport_.bars(); }

  /// Состояние измерения (`trainer::measurementOf`) на последнем тике или
  /// после смены входа.
  const Measurement &measurement() const { return measurement_; }

  /// Выбранный вход MIDI, найденный по имени; пусто — выбран канал
  /// звуковой карты или устройства с таким именем сейчас нет.
  const std::optional<MidiInput> &midiInput() const { return midi_; }

  /// Перечитывает у REAPER сведения о выбранном входе для строки состояния:
  /// имя канала звуковой карты, есть ли выбранный вход MIDI — ищет его по имени
  /// (`findMidiInput`) и пишет в журнал, если найденное поменялось, — и включён
  /// ли он в настройках REAPER для записи на дорожки. Если вход MIDI найден под
  /// другим номером устройства, чем в настройках, ставит в настройки найденный
  /// номер и сохраняет их (`saveSettings`). На кадре эти сведения не
  /// проверяются: тренажёр перечитывает их сам при создании, смене входа и
  /// смене номеров устройств, а окно зовёт при показе настроек и по кнопке
  /// Refresh. Зовётся из главного потока.
  void refreshInputs();

  /// Следует за сменой номеров устройств MIDI, о которой сообщил REAPER:
  /// устройство с номером `from` получило номер `to`. Если это выбранный вход
  /// MIDI, тренажёр берёт его ноты под новым номером и сохраняет номер в
  /// настройках (`saveSettings`). Затем перечитывает сведения о выбранном входе
  /// для строки состояния (`refreshInputs`): со сменой номеров устройство могло
  /// появиться или пропасть. Зовётся из главного потока.
  ///
  /// @param output смена касается выходов MIDI: ничего не меняется.
  void onMidiDeviceRemap(bool output, int from, int to);

  /// Начинает калибровку выбранного входа (`trainer::Calibrator::start`).
  /// Звук входа при остановленном транспорте идёт в калибровку, а удачный
  /// итог сразу ставит порог тишины (`setSettings`) и снимает сообщение о не
  /// принятых строках (`trainer::FileNotice::onSettingsEdit`). Закрытую звуковую карту
  /// калибровка открывает (`Audio_Init`) один раз, при запуске, и после себя не
  /// закрывает. Если карта не открылась или REAPER закрыл её посреди
  /// калибровки, звука нет, и через 2 с сессия показывает итог «нет звука».
  ///
  /// @return ложь, если транспорт воспроизводит или записывает, выбран вход
  ///   MIDI или выбранного канала нет у звуковой карты: калибровка не
  ///   начата.
  bool startCalibration();

  /// Кончает сессию калибровки: Cancel, Close, Finish, закрытие окна. Порог
  /// тишины не меняется — поставленный удачной калибровкой остаётся. Без
  /// сессии ничего не делает.
  void endCalibration();

  /// Ставит порог тишины, посчитанный калибровкой, хотя она его не поставила
  /// («Apply anyway»), сохраняет настройки, снимает сообщение о не принятых
  /// строках и кончает сессию. Ничего не делает, если итога нет или в нём нет
  /// ни одной ноты.
  void applyCalibration();

  /// Сессия калибровки; пусто, если её нет.
  const std::optional<CalibrationSession> &calibration() const {
    return calibrator_.session();
  }

  /// Правда, если сессия калибровки есть и калибровка кончилась: подвела итог
  /// или оборвалась без звука.
  bool calibrationFinished() const { return calibrator_.finished(); }

  /// Порог тишины для «Apply anyway» (`trainer::Calibrator::anywaySilenceDb`).
  std::optional<double> anywaySilenceDb() const { return calibrator_.anywaySilenceDb(); }

  /// Отмечает перемену транспорта, о которой сообщил REAPER. Запуск идёт,
  /// пока идёт воспроизведение и транспорт не на паузе: запись REAPER всегда
  /// ведёт с воспроизведением. Начало запуска (`trainer::Transport::start`)
  /// снимает компенсацию задержки, позицию начала, отсчёт перед запуском по
  /// настройкам проекта и затакт — пре-ролл или отсчёт, — начинает поиск атак
  /// и отбор нот MIDI, прерывает калибровку и снимает сообщение о не принятых
  /// строках (`trainer::FileNotice::onTransportStart`); конец запуска
  /// (`trainer::Transport::stop`) кончает поиск атак и оставляет строки.
  /// Зовётся из главного потока, когда транспорт меняется.
  ///
  /// @param play идёт воспроизведение.
  /// @param pause транспорт на паузе.
  /// @param record идёт запись: отсчёт и пре-ролл — те, что перед записью.
  void onPlayState(bool play, bool pause, bool record);

  /// Ставит скорость воспроизведения проекта, о которой сообщил REAPER. Она
  /// действует на ноты, забранные после вызова, и на строку состояния.
  /// Зовётся из главного потока.
  ///
  /// @param rate скорость, больше нуля; 1 — обычная.
  void onPlayRate(double rate);

  /// Сообщает строкам тактов слышимую позицию воспроизведения, забирает новые
  /// атаки, ноты MIDI и звук калибровки, ставит ноты в строки и обновляет
  /// состояние. Зовётся таймером главного потока.
  ///
  /// @return правда, если строки или состояние изменились и окно пора
  ///   перерисовать.
  bool poll();

private:
  explicit Trainer(const trainer::SettingsRead &read);

  void apply(const Settings &settings);
  void lookupMidiInput();
  void updateMeasurement();
  void skipMidiHistory();
  void readMidiHistory();
  void followPlayback();
  bool takeOnsets();
  bool takeMidiNotes();
  bool pollCalibration();
  void setSilence(double silenceDb);
  void updateAudioMode();

  // Поля стоят в порядке, который не оставляет дыр выравнивания. Транспорт —
  // раньше входа звуковой карты: рабочий поток входа подаёт блоки в поиск
  // атак транспорта, и транспорт создаётся раньше этого потока и разрушается
  // после его остановки.
  trainer::Transport transport_;
  double rate_ = 1.0;
  AudioInput input_;
  ProjectTimeline timeline_;
  // Позиция начала идущего запуска, с.
  double startPosition_ = 0.0;
  // Компенсация задержки сейчас: та, что будет действовать при запуске.
  trainer::Compensation current_;
  // События истории MIDI-входа, прочитанные на тике, от старых к новым.
  std::vector<trainer::MidiEvent> midiEvents_;
  // Сэмплы куска калибровки.
  std::vector<float> chunkSamples_;
  std::optional<MidiInput> midi_;
  // Карта темпа последнего запуска с сеткой отсчёта перед позицией начала: по
  // ней лежат ноты отсчёта и ноты, сыгранные до Play, — и без отсчёта. Пусто —
  // запусков ещё не было.
  std::optional<trainer::CountInTimeline> countInTimeline_;
  Settings settings_;
  trainer::FileNotice fileNotice_;
  trainer::InputState inputs_;
  Measurement measurement_;
  trainer::Calibrator calibrator_;
  // Номер последнего прочитанного события истории MIDI-входа.
  int midiSequence_ = 0;
  // Слышимая позиция дошла до позиции начала: отсчёт позади.
  bool countInPassed_ = true;
};

/// Тренажёр расширения; есть между `initTrainer` и `shutdownTrainer`.
Trainer &trainer();

/// Создаёт тренажёр и регистрирует таймер главного потока. Зовётся при
/// загрузке расширения.
void initTrainer(reaper_plugin_info_t *rec);

/// Снимает таймер и уничтожает тренажёр. Зовётся при выгрузке расширения.
void shutdownTrainer();

} // namespace training::reaper
