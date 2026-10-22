#pragma once

// Тренажёр в главном потоке — связка ядра тренажёра с REAPER: читает REAPER,
// передаёт данные ядру (`core::Transport` и запущенным им шагам) и исполняет
// его решения — журнал, сохранение настроек.

#include "audio_input.hpp"
#include "input_settings.hpp"
#include "journal.hpp"
#include "midi_inputs.hpp"
#include "project_timeline.hpp"
#include "settings.hpp"

#include "training/core/calibration/calibrator.hpp"
#include "training/core/clock.hpp"
#include "training/core/settings.hpp"
#include "training/core/state/work_mode.hpp"
#include "training/core/training/audio.hpp"
#include "training/core/training/midi.hpp"
#include "training/core/training/state/beat.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/core/training/trainer.hpp"
#include "training/core/transport.hpp"
#include "training/reaper/calibration.hpp"
#include "training/reaper/compensation.hpp"
#include "training/reaper/count_in.hpp"
#include "training/reaper/input_state.hpp"
#include "training/reaper/midi_event.hpp"
#include "training/reaper/midi_places.hpp"
#include "training/reaper/mode.hpp"
#include "training/reaper/run_timeline.hpp"
#include "training/render/main/file_notice.hpp"
#include "training/render/main/status_line.hpp"
#include "training/render/main/view_settings.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace training::reaper {

/// Тренажёр: вход звуковой карты или вход MIDI, настройки и такты запуска.
///
/// Многопоточность: только главный поток.
//
// Поля стоят по смыслу, а не по выравниванию: тренажёр один на модуль, и
// лишние 64 байта не стоят перестановки, при которой калибровка
// (`calibration_`) создавалась бы раньше транспорта, которым она пользуется.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class Trainer {
public:
  /// Читает сохранённые настройки (`loadSettings`), пишет в журнал каждую не
  /// принятую строку `reaper-extstate.ini` и отдаёт настройки входу и ядру.
  Trainer();

  /// Действующие настройки ядра. Ссылка верна всё время жизни тренажёра:
  /// смена настроек меняет тот же объект.
  const core::Settings &settings() const { return settings_; }

  /// Выбранный вход у REAPER: канал звуковой карты и устройство MIDI.
  const InputSettings &inputSettings() const { return inputSettings_; }

  /// Режим сетки, по которому связка строит доли тактов для ядра
  /// (`RunTimeline::barAt`).
  Mode gridMode() const { return gridMode_; }

  /// Ставит режим сетки и сохраняет настройки (`saveSettings`). Режим
  /// действует на такты, которые ещё не переданы ядру.
  void setGridMode(Mode mode);

  /// Настройки отрисовки окна.
  const render::ViewSettings &view() const { return view_; }

  /// Сообщение о не принятых строках `reaper-extstate.ini` в строке
  /// состояния окна. Запуск транспорта (`onPlayState`) и итог калибровки
  /// снимают его сами; открытие окна и правки настроек в окне отмечает окно.
  render::FileNotice &fileNotice() { return fileNotice_; }

  /// Сообщение о не принятых строках `reaper-extstate.ini` — только для
  /// чтения.
  const render::FileNotice &fileNotice() const { return fileNotice_; }

  /// Меняет и сохраняет настройки ядра `settings` и выбранный вход `input`
  /// (`saveSettings`). Окно попадания действует на ноты, которые лягут после
  /// смены, канал и порог — со следующего блока, вход MIDI — с нот, пришедших
  /// после смены. Смена входа — вида входа, канала или входа MIDI — кончает
  /// калибровку и ищет выбранный вход MIDI по имени (`findMidiInput`).
  void setSettings(const core::Settings &settings, const InputSettings &input);

  /// Меняет и сохраняет настройки ядра `settings`, как `setSettings` с
  /// прежним выбранным входом.
  void setSettings(const core::Settings &settings);

  /// Меняет настройки ядра, как `setSettings`, но не сохраняет их. Для частых
  /// правок, например пока тянут ползунок шкалы; сохранить — `setSettings`
  /// по окончании.
  void previewSettings(const core::Settings &settings);

  /// Меняет и сохраняет настройки отрисовки окна (`saveSettings`).
  void setView(const render::ViewSettings &view);

  /// Копия тактов последней тренировки с нотами
  /// (`core::training::Trainer::State::timeline`); тактов нет, если тренировок
  /// не было.
  core::training::state::Timeline timeline() const;

  /// Слышимая позиция воспроизведения во времени ядра на последнем тике
  /// запуска (`RunTimeline::heard`); после остановки — последняя слышимая
  /// позиция запуска. Пусто — запусков не было.
  std::optional<core::Moment> heard() const { return heard_; }

  /// Данные REAPER для строки состояния окна на последнем тике или после
  /// смены входа: выбранный вход, компенсация задержки и скорость.
  const render::StatusLine &status() const { return status_; }

  /// Режим работы тренажёра: запуск — тренировка идёт
  /// (`core::training::Trainer::Step::Working`), калибровка — звук входа
  /// идёт в сессию калибровки, иначе ожидание.
  core::state::WorkMode mode() const;

  /// Правда, если калибровку прервал запуск транспорта и он ещё идёт
  /// (`Calibration::cancelledByPlayback`).
  bool calibrationCancelled() const { return calibration_.cancelledByPlayback(); }

  /// Выбранный вход MIDI, найденный по имени; пусто — выбран канал
  /// звуковой карты или устройства с таким именем сейчас нет.
  const std::optional<MidiInput> &midiInput() const { return midi_; }

  /// Перечитывает у REAPER сведения о выбранном входе для строки состояния:
  /// имя канала звуковой карты, есть ли выбранный вход MIDI — ищет его по имени
  /// (`findMidiInput`) и пишет в журнал, если найденное поменялось, — и включён
  /// ли он в настройках REAPER для записи на дорожки, — и по ним пересчитывает
  /// строку состояния (`updateStatus`). Если вход MIDI найден под
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

  /// Начинает калибровку выбранного входа (`Calibration::start`) и запоминает
  /// порог тишины до неё (`calibrationFromDb`).
  /// Звук входа при остановленном транспорте идёт в калибровку, а удачный
  /// итог сразу ставит порог тишины (`setSettings`) и снимает сообщение о не
  /// принятых строках (`render::FileNotice::onSettingsEdit`). Закрытую звуковую карту
  /// калибровка открывает (`Audio_Init`) один раз, при запуске, и после себя не
  /// закрывает. Если карта не открылась или REAPER закрыл её посреди
  /// калибровки, звука нет, и через `kNoSoundTime` тренажёр направляет звук
  /// из калибровки в тренировку и ставит признак «нет звука»
  /// (`calibrationNoSound`).
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

  /// Правда, если сессия калибровки есть (`Calibration::listening`).
  bool calibrating() const { return calibration_.listening(); }

  /// Состояние калибратора ядра в сессии калибровки; пусто, если сессии нет
  /// или в ней ещё не было звука (`Calibration::state`).
  std::optional<core::calibration::Calibrator::State> calibrationState() const {
    return calibration_.state();
  }

  /// Правда, если на шаге тишины калибровки был звук
  /// (`Calibration::heardSound`).
  bool calibrationHeardSound() const { return calibration_.heardSound(); }

  /// Порог тишины, dBFS, который стоял в настройках при запуске последней
  /// калибровки.
  double calibrationFromDb() const { return calibrationFromDb_; }

  /// Имя выбранного канала звуковой карты у хоста.
  const std::string &channelName() const { return inputs_.channelName; }

  /// Правда, если сессия калибровки есть и калибровка кончилась: подвела итог
  /// или оборвалась без звука.
  bool calibrationFinished() const { return calibration_.finished() || calibrationNoSound(); }

  /// Правда, если сессия калибровки есть и тренажёр оборвал её без звука:
  /// звука от звуковой карты не было `kNoSoundTime`, и звук в калибровку
  /// больше не идёт.
  bool calibrationNoSound() const { return noSound_ && calibration_.listening(); }

  /// Порог тишины для «Apply anyway» (`Calibration::anywaySilenceDb`).
  std::optional<double> anywaySilenceDb() const { return calibration_.anywaySilenceDb(); }

  /// Отмечает перемену транспорта, о которой сообщил REAPER. Запуск идёт,
  /// пока идёт воспроизведение и транспорт не на паузе: запись REAPER всегда
  /// ведёт с воспроизведением. Начало запуска начинает запуск транспорта
  /// (`core::Transport::start`), снимает компенсацию задержки, отсчёт перед
  /// запуском по настройкам проекта и затакт — пре-ролл или отсчёт, —
  /// создаёт связку запуска `RunStart` [время проекта начала (P0, скорость),
  /// T0] для карты темпа запуска (`RunTimeline::start`) и хука
  /// (`AudioInput::onStart`) — T0 = `core::Moment()`, — начинает ноты MIDI
  /// (`MidiPlaces::start`), прерывает калибровку и снимает сообщение о не
  /// принятых строках (`render::FileNotice::onTransportStart`); конец запуска
  /// (`core::Transport::stop`) останавливает тренировку и оставляет строки.
  /// Тренировку запуск начинает, когда у ядра есть транспорт под частоту
  /// звуковой карты (`syncTransport`): до первого блока звука её начинает
  /// `poll`. Начало и конец запуска перерисовывают окно (`refreshWindow`).
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

  /// Создаёт транспорт ядра под частоту звуковой карты (`syncTransport`),
  /// отдаёт карте темпа запуска места блоков звука, передаёт ядру такты
  /// запуска и запоминает слышимую позицию, подаёт ноты MIDI и звук
  /// калибровки, пишет в журнал ноты, легшие в такты, и обновляет состояние.
  /// Зовётся таймером главного потока.
  ///
  /// @return правда, если строки или состояние изменились и окно пора
  ///   перерисовать.
  bool poll();

private:
  /// Заводит тренажёр с прочитанными настройками `read`: пишет в журнал
  /// каждую не принятую строку, отдаёт настройки входу звука и ядру, ищет
  /// входы и отмечает историю входа MIDI прочитанной.
  explicit Trainer(const SettingsRead &read);

  /// Ставит настройки ядра `settings` ядру и выбранный вход `input` без
  /// сохранения под замком настроек входа звука (`AudioInput::lockSettings`).
  /// Смена входа — вида входа, канала, входа MIDI или номера его устройства —
  /// кончает калибровку, сообщается разбору нот MIDI, отмечает историю входа
  /// MIDI прочитанной, заново ищет входы, сразу обновляет сводку строки
  /// состояния и пишется в журнал.
  void apply(const core::Settings &settings, const InputSettings &input);

  /// Сохраняет действующие настройки ядра, выбранный вход, режим сетки и
  /// настройки отрисовки (`saveSettings`).
  void save() const;

  /// Ищет выбранный вход MIDI среди устройств REAPER по имени и номеру из
  /// настроек (`findMidiInput`). Вход, найденный под другим номером, меняет
  /// и сохраняет номер в настройках. Найденный или пропавший вход пишется в
  /// журнал, если он изменился.
  void lookupMidiInput();

  /// Пересчитывает данные REAPER для строки состояния — выбранный вход,
  /// компенсацию задержки и скорость; заново проверяет, есть ли выбранный
  /// канал звуковой карты.
  void updateStatus();

  /// Отмечает всю историю входа MIDI REAPER прочитанной: события до этого
  /// момента нотами не станут.
  void skipMidiHistory();

  /// Читает из истории входа MIDI REAPER события после прочитанных в
  /// `midiEvents_`, от старых к новым, и отмечает их прочитанными. Возраст
  /// события переводится в реальное время по частоте компенсации MIDI; при
  /// неизвестной частоте возраст не задаётся.
  void readMidiHistory();

  /// Отдаёт карте темпа запуска места блоков звука, взятых при запуске, —
  /// время блока и время проекта (позиция блока, скорость) —
  /// (`RunTimeline::addBlock`). Петля, перемотка и смена скорости отмечают
  /// такты, которые пора передать ядру заново.
  void takePlaces();

  /// Передаёт ядру предтакт и первый такт запуска (`RunTimeline::firstBar`)
  /// по режиму сетки (`gridMode`).
  ///
  /// @return такт запуска, который ядру передаётся следующим (`nextBar_`).
  std::int64_t sendFirstBars();

  /// Запоминает слышимую позицию во времени ядра (`RunTimeline::heard`) и
  /// передаёт ядру такты запуска (`RunTimeline::barAt`) по режиму сетки
  /// (`gridMode`): в первый раз — предтакт и первый такт запуска, дальше —
  /// каждый такт, как только начался взятый звук такта перед ним. Вход
  /// слышимой позиции в новый такт пишется в журнал.
  void sendBars();

  /// Читает новые события входа MIDI REAPER, берёт из них ноты со временем
  /// ядра (`MidiPlaces::place`), вычитает компенсацию MIDI и подаёт ноты ядру
  /// (`core::training::Midi::receive`). История читается заново, если за чтение
  /// REAPER начал новый блок звука, — до трёх попыток. Каждая нота пишется в
  /// журнал.
  ///
  /// @return правда, если ядро получило хоть одну ноту.
  bool takeMidiNotes();

  /// Пишет в журнал разницы нот, легших в доли тактов
  /// (`core::training::Trainer::State::timeline`): ноты, которых не было в
  /// журнале на прошлом вызове.
  void journalPlaced();

  /// Подаёт калибровке звук, накопленный входом; удачный итог ставит и
  /// сохраняет порог тишины (`setSilence`) один раз — когда он появился. Если
  /// звука не было `kNoSoundTime` по часам ОС (`momentNow`) от начала сессии
  /// или от последнего куска, а итога нет, направляет звук из калибровки в
  /// тренировку (`AudioInput::toTraining`), пишет это в журнал и ставит
  /// признак «нет звука» (`calibrationNoSound`); сессия остаётся.
  ///
  /// @return правда, если калибровка получила звук или появился признак «нет
  ///   звука»: окно пора перерисовать.
  bool pollCalibration();

  /// Ставит и сохраняет порог тишины, снимает сообщение о не принятых
  /// строках и показывает настройки в окне.
  ///
  /// @param silenceDb порог тишины, dBFS, допустимый для настроек.
  void setSilence(double silenceDb);

  /// Правда, если идёт тренировка (`core::training::Trainer::Step::Working`).
  bool running() const;

  /// Запускает тренировку по выбранному виду входа (`core::Transport::start`):
  /// звук входа запуска `audioRun_` идёт тренировке звука, ноты MIDI —
  /// тренировке MIDI. Такты запуска тренировка получает заново (`sendBars`).
  /// Без транспорта тренировка ждёт его (`syncTransport`).
  void startTraining();

  /// Создаёт транспорт ядра под частоту последнего блока звука
  /// (`AudioInput::sampleRate`), когда транспорта нет или его частота другая
  /// (`core::Transport.sampleRate`), и отдаёт его калибровке. Идущая или
  /// ждущая транспорта тренировка запускается на новом транспорте заново.
  void syncTransport();

  // Обработчики событий ядра — раньше калибровки: она держит их по указателю.
  CalibrationJournal calibrationJournal_;
  // Транспорт ядра под частоту звуковой карты; пусто — блоков звука ещё не
  // было.
  std::unique_ptr<core::Transport> transport_;
  // Тренировка, которую запустил транспорт последней; пусто — тренировок не
  // было. Вид входа — в `audioTraining_` или `midiTraining_`. Тренировки и
  // выбранный вход — раньше входа звуковой карты: рабочий поток входа подаёт
  // блоки тренировке звука, а хук читает выбранный вход; они создаются
  // раньше этих потоков и разрушаются после их остановки.
  std::shared_ptr<core::training::Trainer> training_;
  std::shared_ptr<core::training::Audio> audioTraining_;
  std::shared_ptr<core::training::Midi> midiTraining_;
  // Идёт запуск транспорта REAPER (`onPlayState`).
  bool playing_ = false;
  // Запуск начался без транспорта ядра: тренировку начнёт `syncTransport`.
  bool trainingPending_ = false;
  // Сколько нот каждой доли каждого такта тренировки уже в журнале: по
  // индексу такта.
  std::vector<std::map<core::training::state::Beat, std::size_t>> journaled_;
  core::Settings settings_;
  InputSettings inputSettings_;
  Mode gridMode_ = Mode::Quarters;
  render::ViewSettings view_;
  double rate_ = 1.0;
  AudioInput input_;

  // Номер запуска у входа звука (`AudioInput::onStart`): места блоков с другим
  // номером на карту запуска не ложатся.
  std::uint64_t audioRun_ = 0;
  ProjectTimeline timeline_;
  // Карта темпа последнего запуска: по ней строятся такты запуска для ядра.
  RunTimeline runTimeline_;
  // Следующий такт запуска для ядра; пусто — запуск ещё не передал ни
  // одного такта.
  std::optional<std::int64_t> nextBar_;
  // Такт запуска, в который последним вошла слышимая позиция.
  std::optional<std::int64_t> enteredBar_;
  std::optional<core::Moment> heard_;
  MidiPlaces midiPlaces_;
  // Компенсация задержки сейчас: та, что будет действовать при запуске.
  Compensation current_;
  // Компенсация задержки, снятая при начале последнего запуска: держится до
  // конца запуска, как постоянный сдвиг у записи; до первого запуска —
  // нулевая.
  Compensation started_;
  // События истории MIDI-входа, прочитанные на тике, от старых к новым.
  std::vector<MidiEvent> midiEvents_;
  // Сэмплы куска калибровки.
  std::vector<float> chunkSamples_;
  std::optional<MidiInput> midi_;
  render::FileNotice fileNotice_;
  InputState inputs_;
  render::StatusLine status_;
  Calibration calibration_;
  // Порог тишины при запуске последней калибровки: его показывает итог.
  double calibrationFromDb_ = 0.0;
  // Часы ОС (`momentNow`) начала сессии калибровки или последнего куска её
  // звука; пусто — калибровок не было.
  std::optional<core::Moment> lastCalibrationSound_;
  // Сессию калибровки тренажёр оборвал без звука.
  bool noSound_ = false;
  // Номер последнего прочитанного события истории MIDI-входа.
  int midiSequence_ = 0;
};

/// Тренажёр расширения; есть между `initTrainer` и `shutdownTrainer`.
Trainer &trainer();

/// Создаёт тренажёр и регистрирует таймер главного потока. Зовётся при
/// загрузке расширения.
void initTrainer(reaper_plugin_info_t *rec);

/// Снимает таймер и уничтожает тренажёр. Зовётся при выгрузке расширения.
void shutdownTrainer();

} // namespace training::reaper
