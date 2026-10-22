#pragma once

#include "training/render/common/fill.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Полоса хода шага калибровки: пройденная доля — жёлтым по серой полосе.
class ProgressBar : public Element {
public:
  /// Создаёт полосу без пройденной доли.
  ProgressBar();

  /// Ставит пройденную долю `done`, от 0 до 1.
  void setDone(double done) { done_ = done; }

  /// Рисует полосу во всё место кадра, пройденную долю — от левого края.
  void draw(const Frame &frame) const override;

private:
  double done_ = 0.0;
  Fill track_;
  Fill passed_;
};

} // namespace training::render
