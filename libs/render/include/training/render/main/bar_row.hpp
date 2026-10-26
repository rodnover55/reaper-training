#pragma once

#include "training/core/hit_window.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/render/common/dot.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/common/label.hpp"
#include "training/render/element.hpp"
#include "training/render/main/change_mark.hpp"
#include "training/render/main/node_value.hpp"

#include <optional>
#include <string>
#include <vector>

namespace training::render {

/// Значение узла в строке такта.
struct ValueView {
  /// Место узла в ударах от начала такта: 0 — первый удар, 1.5 — середина
  /// второго.
  double beat = 0.0;

  /// Узел стоит на ударе: значение показывается крупнее значений между
  /// ударами.
  bool onBeat = false;

  /// Отклонение: «−15», «+5», «0», «+3*» — с лишней нотой.
  Cell cell;
};

/// Маркер смены темпа или размера в строке такта.
struct MarkView {
  /// Место смены в ударах от начала такта.
  double beat = 0.0;

  /// «было → стало»: «4/4 → 7/8», «120 → 150»; смена размера и темпа вместе —
  /// «3/4 → 4/4 · 120 → 150».
  std::string text;
};

/// Что показывает строка такта (`timing-display`).
struct BarView {
  /// Подпись такта — его имя (`core::training::state::Bar::name`): «57», у такта затакта
  /// «−1».
  std::string label;

  /// Такт затакта — пре-ролла или отсчёта перед запуском: имя начинается со
  /// знака минус U+2212; строка на своём фоне.
  bool leadIn = false;

  /// Ударов в такте; шкала строки делится на них поровну.
  int beats = 4;

  /// Значения узлов по порядку мест.
  std::vector<ValueView> values;

  /// Удары, у которых нет значения на самом ударе: на их месте — точка.
  std::vector<int> dots;

  /// Удар, на котором сейчас воспроизведение, с 0; пусто — такт сейчас не
  /// звучит.
  std::optional<int> currentBeat;

  /// Маркеры смен темпа и размера по порядку мест.
  std::vector<MarkView> marks;

  /// Наибольшее число долей такта на одном ударе; не меньше 1. По нему
  /// выбирается кегль.
  int division = 1;

  /// Медиана отклонений, «+2.0», цвет — по текущему окну попадания; `Target`
  /// у медианы не бывает. Нет у верхней строки и у строки с одним значением
  /// или без значений.
  std::optional<Cell> median;

  /// IQR — межквартильный размах отклонений, «3.7», цвет `Neutral`; есть
  /// вместе с `median`.
  std::optional<Cell> iqr;
};

/// Раскладка строки такта по горизонтали и общие кегли.
struct Layout {
  /// Левый край строки и её ширина, пикселей.
  double left = 0.0;
  double width = 0.0;

  /// Высота строки.
  double rowHeight = 0.0;

  /// Колонка номеров тактов слева и колонка медианы и IQR справа;
  /// выключенная колонка — ноль.
  double labelWidth = 0.0;
  double statsWidth = 0.0;

  /// Шкала такта — всё остальное.
  double scaleWidth = 0.0;

  /// Кегль номера такта, медианы и IQR и кегль маркера смены.
  double sideSize = 0.0;
  double statsSize = 0.0;
  double markSize = 0.0;
};

/// Раскладка строки такта в месте `row`.
///
/// @param barNumbers показывать ли колонку номеров тактов.
/// @param barStats показывать ли колонку медианы и IQR.
Layout layoutOf(const Box &row, bool barNumbers, bool barStats);

/// Строка такта: номер такта, шкала по ударам, точки, значения, текущий удар,
/// маркеры смен, медиана и IQR.
class BarRow : public Element {
public:
  /// Создаёт пустую строку с обеими колонками.
  BarRow();

  /// Включает и выключает колонку номеров тактов `barNumbers` и колонку
  /// медианы и IQR `barStats`.
  void setColumns(bool barNumbers, bool barStats);

  /// Показывает такт `bar` (`timing-display`).
  ///
  /// Значение доли — нота доли с наименьшей по модулю разницей, если нот в
  /// доле несколько, — со звёздочкой: «+3*». Отклонение — целые
  /// миллисекунды, округлённые до ближайшего, со знаком: «−» (U+2212) —
  /// раньше, «+» — позже, «0» — ноль; цвет — по оценке ноты (`core::Hit`).
  /// Точка стоит на ударе, в доле которого нет ноты. Медиана и IQR
  /// (`DeviationStats`) — по одному значению на долю, когда значений больше
  /// одного, с одним знаком после запятой; цвет медианы — `Good` между краями
  /// окна `window` включительно, иначе `Bad`.
  ///
  /// Маркеры смен: в начале такта — смена размера и смена темпа против такта
  /// `previous`, последнего темпа `previous` против темпа в начале такта;
  /// внутри такта — смены темпа между соседними долями темпа такта
  /// (`core::training::state::Bar::tempo`). Смена размера и темпа в начале такта — один
  /// маркер. Темп в маркере — целое число четвертей в минуту или дробное с
  /// одним-двумя знаками после запятой.
  ///
  /// @param previous такт перед `bar` в списке тактов запуска.
  /// @param top верхняя строка — такт, в котором позиция воспроизведения: у
  ///   неё нет медианы и IQR.
  /// @param currentBeat удар, на котором сейчас воспроизведение, с 0; пусто —
  ///   такт сейчас не звучит.
  /// @param window текущее окно попадания: им красится медиана.
  void setRow(const core::training::state::Bar &bar,
              const core::training::state::Bar &previous, bool top,
              std::optional<int> currentBeat, const core::HitWindow &window);

  /// Что показывает строка.
  const BarView &view() const { return view_; }

  /// Рисует строку в месте кадра: размеры — доли высоты места.
  void draw(const Frame &frame) const override;

private:
  BarView view_;
  bool barNumbers_ = true;
  bool barStats_ = true;
  Fill leadInRow_;
  Fill leadInLabel_;
  Fill currentRow_;
  Fill currentBeat_;
  Label number_;
  Label median_;
  Label iqr_;
  std::vector<ChangeMark> marks_;
  std::vector<Dot> dots_;
  std::vector<NodeValue> values_;
};

} // namespace training::render
