#pragma once

#include "training/core/clock.hpp"
#include "training/core/settings.hpp"
#include "training/render/common/dashes.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/common/outline.hpp"
#include "training/render/element.hpp"
#include "training/render/main/gauge_handle.hpp"
#include "training/render/main/gauge_ticks.hpp"
#include "training/render/main/view_settings.hpp"
#include "training/render/palette.hpp"

namespace training::render {

/// Шкала окна попадания под полями Offset и Tolerance: дорожка вне окна,
/// полоса окна, пунктир смещения, отметка клика, ползунки краёв и подписи
/// делений. Окно попадания шкала берёт из настроек ядра, когда рисует.
class HitWindowGauge : public Element {
public:
  /// Создаёт шкалу от −50 до +50 мс для окна попадания настроек `settings`.
  ///
  /// @param settings настройки ядра; живут дольше шкалы.
  explicit HitWindowGauge(const core::Settings &settings);

  /// Меняет границу шкалы: шкала от −`scale` до +`scale`.
  void setScale(core::Interval scale);

  /// Меняет цвета диалога: ими рисуются фон шкалы и подписи делений.
  void setColors(const DialogColors &colors);

  /// Рисует шкалу в месте кадра по раскладке `gaugeLayout`.
  void draw(const Frame &frame) const override;

private:
  const core::Settings &settings_;
  core::Interval scale_ = ViewSettings::kDefaultScale.interval();
  Fill background_;
  Fill outside_;
  Fill band_;
  Outline track_;
  Dashes offset_;
  Fill zero_;
  GaugeHandle handle_;
  GaugeTicks ticks_;
};

} // namespace training::render
