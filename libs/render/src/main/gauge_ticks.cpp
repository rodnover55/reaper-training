#include "training/render/main/gauge_ticks.hpp"

#include "training/render/main/gauge_layout.hpp"

#include <fmt/format.h>

#include <cmath>
#include <string>
#include <string_view>

namespace training::render {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

/// Шаг подписей делений шкалы, мс: самый частый, при котором подписи не
/// налезают друг на друга.
double tickStep(const GaugeLayout &layout) {
  const double perMs =
      (layout.right - layout.left) / (2.0 * (layout.scale / core::Interval::kMillisecond));
  for (const double step : {5.0, 10.0, 20.0, 25.0, 50.0, 100.0})
    if (step * perMs >= layout.labelSize * 3.0)
      return step;
  return 200.0;
}

/// Подпись деления `ms` — целых миллисекунд: «+10», «−5» (U+2212), «0».
std::string tickText(double ms) {
  const long shown = std::lround(ms);
  if (shown > 0)
    return fmt::format("+{}", shown);
  if (shown < 0)
    return fmt::format("{}{}", kMinus, -shown);
  return "0";
}

} // namespace

void GaugeTicks::draw(const Frame &frame) const {
  const GaugeLayout layout = gaugeLayout(frame.area, scale_);
  const double step = tickStep(layout);
  const auto ticks =
      static_cast<int>(std::floor(layout.scale / core::Interval::kMillisecond / step));
  for (int tick = -ticks; tick <= ticks; ++tick) {
    const double ms = tick * step;
    const double at = layout.x(ms * core::Interval::kMillisecond);
    frame.canvas.useFont(layout.labelSize, tick == 0);
    frame.canvas.drawText(tickText(ms),
                          Box{.left = at - layout.labelSize * 2.0,
                              .top = layout.labelMiddle - layout.labelSize * 0.7,
                              .right = at + layout.labelSize * 2.0,
                              .bottom = layout.labelMiddle + layout.labelSize * 0.7},
                          color_, TextAlign::Center);
  }
}

} // namespace training::render
