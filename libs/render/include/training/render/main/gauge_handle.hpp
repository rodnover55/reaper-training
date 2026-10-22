#pragma once

#include "training/render/common/fill.hpp"
#include "training/render/common/outline.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Ползунок края окна попадания на шкале: светлая планка в тёмной рамке с
/// двумя насечками посередине.
class GaugeHandle : public Element {
public:
  /// Создаёт ползунок.
  GaugeHandle();

  /// Рисует планку во всё место кадра, насечки — по его середине.
  void draw(const Frame &frame) const override;

private:
  Fill body_;
  Outline frame_;
  Fill grip_;
};

} // namespace training::render
