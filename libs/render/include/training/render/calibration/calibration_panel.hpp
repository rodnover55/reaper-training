#pragma once

#include "training/core/calibration/calibrator.hpp"
#include "training/core/settings.hpp"
#include "training/render/calibration/level_scale.hpp"
#include "training/render/calibration/level_table.hpp"
#include "training/render/calibration/note_slots.hpp"
#include "training/render/calibration/progress_bar.hpp"
#include "training/render/calibration/steps_row.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/common/label.hpp"
#include "training/render/common/paragraph.hpp"
#include "training/render/element.hpp"

#include <string>

namespace training::render {

/// Кнопки панели калибровки, пикселей; их ставит окно по `PanelLayout`.
inline constexpr int kButtonHeight = 24;
inline constexpr int kButtonGap = 8;

/// Раскладка панели калибровки в месте: края, размеры места и верх кнопок.
struct PanelLayout {
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
  double bottom = 0.0;
  double width = 0.0;
  double height = 0.0;

  /// Верх кнопок внизу панели.
  double buttonsTop = 0.0;
};

/// Панель калибровки входа вместо строк тактов (`input-calibration`): строка
/// шагов, крупная подсказка, пояснение и то, что показывает этап — ход шага
/// тишины, ячейки нот, таблицу уровней итога, шкалу уровней. Действующий
/// порог тишины панель берёт из настроек ядра.
class CalibrationPanel : public Element {
public:
  /// Создаёт панель; `host` — имя программы-хоста в пояснении к обрыву без
  /// звука.
  ///
  /// @param settings настройки ядра; живут дольше панели.
  CalibrationPanel(std::string host, const core::Settings &settings);

  /// Раскладка панели в месте `area`: отступ слева и справа — 2.2 % ширины, не
  /// меньше 8 пикселей; сверху и снизу — 3 % высоты; кнопки — у низа места.
  static PanelLayout layoutOf(const Box &area);

  /// Ставит признак «нет звука» от связки: звука от звуковой карты не было, и
  /// связка направила звук из калибровки в тренировку. Действует на следующий
  /// `setSession`.
  void setNoSound(bool noSound) { noSound_ = noSound; }

  /// Показывает сессию калибровки при действующем пороге тишины из настроек
  /// ядра: с признаком «нет звука» (`setNoSound`) — обрыв без звука, иначе по
  /// шагу калибратора — шаг тишины, шаг нот или итог.
  ///
  /// @param state состояние калибратора ядра; nullptr — звука ещё не было,
  ///   идёт шаг тишины без уровня звука.
  /// @param channel имя калибруемого канала у хоста.
  /// @param previousSilenceDb порог тишины до калибровки, dBFS.
  /// @param heardSound был ли на шаге тишины звук: тогда 2 с после него
  ///   (`core::calibration::Calibrator::State::sinceSound`) видна подсказка о
  ///   звуке.
  void setSession(const core::calibration::Calibrator::State *state,
                  const std::string &channel, double previousSilenceDb, bool heardSound);

  /// Рисует панель в месте кадра: кегли — доли высоты места.
  void draw(const Frame &frame) const override;

private:
  /// Где стоит шкала уровней.
  enum class ScaleAt {
    /// Шкалы нет.
    None,
    /// Низом у низа панели.
    Bottom,
    /// Низом над кнопками.
    Buttons,
  };

  /// Показывает шаг тишины по состоянию калибратора `state`; nullptr — шаг
  /// только начат, уровня звука нет.
  void silenceStep(const core::calibration::Calibrator::State *state);

  /// Показывает шаг нот по состоянию калибратора `state`.
  void notesStep(const core::calibration::Calibrator::State &state);

  /// Показывает обрыв калибровки без звука.
  void noSound();

  /// Показывает итог по состоянию калибратора `state` на шаге итога.
  void result(const core::calibration::Calibrator::State &state);

  /// Окончание пояснения « Silence threshold stays … dBFS.» с действующим
  /// порогом тишины — для этапов, после которых порог не меняется.
  std::string stays() const;

  std::string host_;
  const core::Settings &settings_;
  std::string channel_;
  double previousSilenceDb_ = 0.0;
  bool heardSound_ = false;
  bool noSound_ = false;
  bool text_ = false;
  bool progress_ = false;
  bool slots_ = false;
  bool table_ = false;
  ScaleAt scaleAt_ = ScaleAt::None;
  Fill background_;
  StepsRow steps_;
  Label say_;
  Paragraph hint_;
  ProgressBar progressBar_;
  NoteSlots noteSlots_;
  LevelTable levelTable_;
  LevelScale levelScale_;
};

} // namespace training::render
