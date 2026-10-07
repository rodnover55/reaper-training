#pragma once

// Тренажёр в главном потоке: атаки со входа → время на шкале → строки тактов
// для окна (архив add-timing-trainer, design.md D2, D3; design.md D1–D5).

#include "audio_input.hpp"
#include "midi_inputs.hpp"
#include "project_timeline.hpp"
#include "settings.hpp"

#include "training/grid/bars.hpp"
#include "training/onset/calibration.hpp"
#include "training/onset/dead_time.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace training::reaper {

/// Что сейчас делает измерение.
enum class Measuring {
  /// Транспорт остановлен или на паузе.
  Waiting,
  /// Воспроизведение или запись: ноты меряются.
  Running,
  /// Выбранного входа нет: канала у звуковой карты или устройства MIDI.
  NoInput,
  /// Выбранный вход MIDI не включён в настройках REAPER для записи на
  /// дорожки.
  NotEnabled,
};

/// Сведения для строки состояния окна.
struct Status {
  Measuring measuring = Measuring::Waiting;

  /// Имя выбранного входа: входного канала (`GetInputChannelName`) или
  /// устройства MIDI с псевдонимом (`MidiInput::name`); пусто — входа нет.
  std::string inputName;

  /// Правда, если выбран вход MIDI.
  bool midi = false;

  /// Компенсация задержки выбранного входа, мс реального времени: при
  /// воспроизведении — снятая при запуске, на остановке — та, что будет
  /// действовать при запуске.
  double compensationMs = 0.0;

  /// Скорость воспроизведения проекта.
  double rate = 1.0;

  /// Правда, если калибровку входа прервал запуск воспроизведения или
  /// записи; держится, пока транспорт не остановится.
  bool calibrationCancelled = false;
};

/// Сессия калибровки входа: от запуска калибровки до Close, Cancel или
/// «Apply anyway» (`input-calibration`).
struct CalibrationSession {
  /// Ход калибровки; пусто, пока не пришёл первый звук: до него неизвестна
  /// частота дискретизации.
  std::optional<onset::Calibration> calibration;

  /// Порог тишины до калибровки, dBFS.
  double previousSilenceDb = 0.0;

  /// Имя калибруемого входного канала (`GetInputChannelName`).
  std::string channelName;

  /// Правда, если звук от звуковой карты не приходил 2 с и калибровка
  /// оборвана без итога.
  bool noSound = false;

  /// Правда, если калибровка удалась и сама поставила порог тишины.
  bool applied = false;
};

/// Тренажёр: вход звуковой карты или вход MIDI, настройки и строки тактов.
///
/// Многопоточность: только главный поток.
class Trainer {
public:
  /// Читает сохранённые настройки и отдаёт их входу и своду.
  Trainer();

  const Settings &settings() const { return settings_; }

  /// Меняет и сохраняет настройки (`saveSettings`), прижав их к границам.
  /// Режим действует на удары, которые ещё не начались, окно попадания — на
  /// значения, которые появятся после смены, канал и порог — со следующего
  /// блока, вход MIDI — с нот, пришедших после смены. Смена входа кончает
  /// калибровку и ищет выбранный вход MIDI по имени (`findMidiInput`).
  void setSettings(const Settings &settings);

  /// Меняет настройки, как `setSettings`, но не сохраняет их. Для частых
  /// правок, например пока тянут ползунок шкалы; сохранить — `setSettings`
  /// по окончании.
  void previewSettings(const Settings &settings);

  /// Строки тактов последнего запуска транспорта.
  const grid::Bars &bars() const { return bars_; }

  /// Кладёт в строки тактов ноту в момент `time` шкалы, с, как будто её только
  /// что нашёл поиск атак. Нужна для проверок окна без гитары. Пока
  /// транспорт стоит, ничего не делает.
  void addTestNote(double time);

  const Status &status() const { return status_; }

  /// Атаки последнего запуска транспорта в порядке нахождения — для сверки с
  /// записанным айтемом.
  const std::vector<HookOnset> &lastRun() const { return lastRun_; }

  /// Времена нот MIDI последнего запуска транспорта на шкале проекта, с, в
  /// порядке прихода — для сверки с записанным MIDI-айтемом. Время — до
  /// заворота в петлю; ноты, пропущенные в мёртвом времени, сюда не попадают.
  const std::vector<double> &lastMidiRun() const { return lastMidiRun_; }

  /// Выбранный вход MIDI, найденный по имени; пусто — выбран канал
  /// звуковой карты или устройства с таким именем сейчас нет.
  const std::optional<MidiInput> &midiInput() const { return midi_; }

  /// Ищет выбранный вход MIDI по имени среди входов, которые есть сейчас
  /// (`findMidiInput`), и пишет в журнал, если найденное поменялось. Без
  /// вызова найденный вход обновляется раз в секунду, на тике `poll`.
  void lookupMidiInput();

  AudioInput &input() { return input_; }

  /// Начинает калибровку выбранного входа; прежняя сессия кончается без
  /// изменений. Звук входа при остановленном транспорте идёт в калибровку, а
  /// удачный итог сразу ставит порог тишины (`setSettings`). После итога звук
  /// слушается дальше: удар по струнам через 5 с кончает сессию
  /// (`onset::Calibration::soundAfterResult`). Пока сессия слушает вход,
  /// закрытую звуковую карту она открывает (`Audio_Init`) и после себя не
  /// закрывает.
  ///
  /// @return ложь, если транспорт воспроизводит или записывает, выбран вход
  ///   MIDI или выбранного канала нет у звуковой карты: калибровка не
  ///   начата.
  bool startCalibration();

  /// Начинает калибровку, как `startCalibration()`, но звук берёт не со
  /// входа, а из `samples` в темпе реального времени. Для проверок без
  /// гитары.
  ///
  /// @param samples звук одного канала, доли полной шкалы.
  /// @param sampleRate его частота дискретизации, Гц; больше 2000.
  bool startCalibration(std::vector<float> samples, double sampleRate);

  /// Кончает сессию калибровки: Cancel, Close, закрытие окна. Порог тишины не
  /// меняется — поставленный удачной калибровкой остаётся. Без сессии ничего
  /// не делает.
  void endCalibration();

  /// Ставит порог тишины, посчитанный калибровкой, хотя она его не поставила
  /// («Apply anyway»), и кончает сессию. Ничего не делает, если итога нет или
  /// в нём нет ни одной ноты.
  void applyCalibration();

  /// Сессия калибровки; пусто, если её нет.
  const std::optional<CalibrationSession> &calibration() const { return session_; }

  /// Правда, если сессия есть и показывает итог: калибровка закончилась или
  /// оборвалась без звука.
  bool calibrationFinished() const {
    return session_ && (calibrationFinished_ || session_->noSound);
  }

  /// Сообщает строкам тактов слышимую позицию воспроизведения, забирает новые
  /// атаки или ноты MIDI, ставит их в строки и обновляет состояние. Зовётся
  /// таймером главного потока.
  ///
  /// @return правда, если строки или состояние изменились и окно пора
  ///   перерисовать.
  bool poll();

private:
  void apply(const Settings &settings);
  void start();
  void takeCompensation();
  Status currentStatus(bool playing, int channels);
  void skipMidiHistory();
  void readMidiHistory();
  std::optional<double> movedAgo();
  bool pollMidi(bool playing);
  double loopBeats(double heard) const;
  void followPlayback();
  double loopWrapped(double time, double blockPosition) const;
  void stopCalibrationOnPlayback(bool playing);
  bool pollCalibration();
  void openAudioDevice(std::chrono::steady_clock::time_point now);
  void feedCalibration(std::span<const float> samples, double sampleRate, bool gap);
  void finishCalibration();

  /// Звук для калибровки без гитары (`startCalibration(samples, sampleRate)`)
  /// и сколько его уже подано.
  struct TestSound {
    std::vector<float> samples;
    double sampleRate = 0.0;
    std::chrono::steady_clock::time_point startedAt;
    std::size_t fed = 0;
  };

  /// Событие истории MIDI-входа REAPER (`MIDI_GetRecentInputEvent`).
  struct MidiEvent {
    /// Позиция на шкале проекта, с; −1 — транспорт стоял.
    double position = -1.0;
    /// Сэмплов от прихода события до опроса истории, не больше нуля.
    int timestamp = 0;
    /// Номер устройства в младших 16 битах, бит 0x10000 — вход только для
    /// управления.
    int device = 0;
    /// Номер прохода петли.
    int loop = 0;
    /// Длина сообщения, байт.
    int size = 0;
    /// Первые байты сообщения, не больше трёх.
    std::array<std::uint8_t, 3> bytes{};
  };

  // Поля стоят в порядке, который не оставляет дыр выравнивания.
  AudioInput input_;
  ProjectTimeline timeline_;
  double sampleRate_ = 0.0;
  double compensation_ = 0.0;
  double inputLatency_ = 0.0;
  double rate_ = 1.0;
  // Компенсация записи MIDI и частота звуковой карты для неё, снятые при
  // запуске транспорта.
  double midiCompensation_ = 0.0;
  double midiSampleRate_ = 0.0;
  // Позиция обработки при запуске транспорта: пока она не сдвинулась, идёт
  // count-in, и нот MIDI нет (design.md D3).
  double startPosition_ = 0.0;
  // Когда от хука или звука без гитары пришёл последний звук калибровки.
  std::chrono::steady_clock::time_point lastSound_;
  // Когда калибровка последний раз пробовала открыть закрытую звуковую карту.
  std::chrono::steady_clock::time_point lastOpenAttempt_;
  // Когда последний раз искали вход MIDI по имени.
  std::chrono::steady_clock::time_point lastMidiLookup_;
  std::vector<HookOnset> lastRun_;
  std::vector<double> lastMidiRun_;
  // События истории MIDI-входа, прочитанные на тике, от новых к старым.
  std::vector<MidiEvent> midiEvents_;
  std::optional<MidiInput> midi_;
  onset::DeadTime deadTime_;
  // Сэмплы куска калибровки.
  std::vector<float> chunkSamples_;
  Settings settings_;
  std::optional<TestSound> testSound_;
  Status status_;
  grid::Bars bars_;
  std::optional<CalibrationSession> session_;
  std::uint32_t minRun_ = 0;
  std::uint32_t latestRun_ = 0;
  // Номер последнего прочитанного события истории MIDI-входа.
  int midiSequence_ = 0;
  // Проход петли последней ноты MIDI: с новым проходом мёртвое время
  // кончается.
  int midiLoop_ = 0;
  bool wasPlaying_ = false;
  // Сдвинулась ли позиция с запуска транспорта.
  bool positionMoved_ = false;
  // Подведён ли итог калибровки; прервал ли её запуск транспорта.
  bool calibrationFinished_ = false;
  bool calibrationCancelled_ = false;
};

/// Тренажёр расширения; есть между `initTrainer` и `shutdownTrainer`.
Trainer &trainer();

/// Создаёт тренажёр и регистрирует таймер главного потока. Зовётся при
/// загрузке расширения.
void initTrainer(reaper_plugin_info_t *rec);

/// Снимает таймер и уничтожает тренажёр. Зовётся при выгрузке расширения.
void shutdownTrainer();

} // namespace training::reaper
