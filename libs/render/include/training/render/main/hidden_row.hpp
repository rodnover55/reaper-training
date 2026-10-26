#pragma once

#include "training/render/common/fill.hpp"
#include "training/render/common/label.hpp"
#include "training/render/common/plate.hpp"
#include "training/render/element.hpp"

#include <string>

namespace training::render {

/// Что показывает строка скрытых тактов.
struct HiddenView {
  /// Подпись тактов: имя единственного скрытого такта, «10», или первого и
  /// последнего через тире, «9–10».
  std::string label;

  /// Метка: «1 bar hidden», «2 bars hidden».
  std::string text;
};

/// Строка скрытых тактов над строками тактов: подпись в колонке номеров
/// тактов и метка, без значений и без текущего удара.
class HiddenRow : public Element {
public:
  /// Создаёт пустую строку.
  HiddenRow();

  /// Показывает скрытые такты с именами от `first` до `last`, `count`
  /// тактов.
  void setHidden(const std::string &first, const std::string &last, int count);

  /// Включает и выключает колонку номеров тактов `barNumbers`.
  void setBarNumbers(bool barNumbers) { barNumbers_ = barNumbers; }

  /// Что показывает строка.
  const HiddenView &view() const { return view_; }

  /// Рисует строку во всё место кадра: тусклую подпись в колонке номеров
  /// тактов, если она видна, и метку на подложке в начале шкалы.
  void draw(const Frame &frame) const override;

private:
  HiddenView view_;
  bool barNumbers_ = true;
  Fill background_;
  Plate plate_;
  Label label_;
  Label text_;
};

} // namespace training::render
