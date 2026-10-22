#pragma once

#include "training/core/clock.hpp"
#include "training/render/canvas.hpp"

#include <algorithm>

namespace training::render {

/// Половина ширины ползунка шкалы окна попадания и расстояние до него, на
/// котором щелчок берёт ползунок, пикселей.
inline constexpr double kHandleHalf = 4.0;
inline constexpr double kHandleReach = 6.0;

/// Раскладка шкалы окна попадания в месте: дорожка и перевод отклонений от
/// клика в пиксели и обратно. По ней шкала рисуется, а окно узнаёт, что под
/// мышью.
struct GaugeLayout {
  /// Края дорожки по горизонтали и вертикали, пикселей.
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
  double bottom = 0.0;

  /// На сколько ползунки и отметка клика выходят за дорожку сверху и снизу,
  /// пикселей.
  double reach = 0.0;

  /// Середина строки подписей делений и их кегль, пикселей.
  double labelMiddle = 0.0;
  double labelSize = 0.0;

  /// Граница шкалы: шкала от −`scale` до +`scale`; хотя бы 1 мс.
  core::Interval scale = core::Interval::kMillisecond;

  /// Место отклонения `value` на дорожке, пикселей; за шкалой — её край.
  double x(core::Interval value) const {
    return left + (std::clamp(value, -scale, scale) + scale) / (scale * 2.0) * (right - left);
  }

  /// Отклонение места `at` на дорожке; за дорожкой — дальше края шкалы.
  core::Interval valueAt(double at) const {
    return scale * ((at - left) / (right - left) * 2.0) - scale;
  }
};

/// Раскладка шкалы с границей `scale` в месте `box`: дорожка — в верхней
/// половине, подписи — под ней, по краям — место под половину подписи.
GaugeLayout gaugeLayout(const Box &box, core::Interval scale);

} // namespace training::render
