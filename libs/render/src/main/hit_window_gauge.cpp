#include "training/render/main/hit_window_gauge.hpp"

#include "training/render/main/gauge_layout.hpp"

namespace training::render {

HitWindowGauge::HitWindowGauge(const core::Settings &settings)
    : settings_(settings), background_(Color{}), outside_(kOutside), band_(kBand),
      track_(kGaugeFrame), offset_(kGood, 2.0, 4.0), zero_(kBright),
      ticks_(ViewSettings::kDefaultScale.interval(), Color{}) {}

void HitWindowGauge::setScale(core::Interval scale) {
  scale_ = scale;
  ticks_.setScale(scale);
}

void HitWindowGauge::setColors(const DialogColors &colors) {
  background_.setColor(colors.face);
  ticks_.setColor(colors.text);
}

void HitWindowGauge::draw(const Frame &frame) const {
  const core::HitWindow &window = settings_.window();
  const GaugeLayout layout = gaugeLayout(frame.area, scale_);
  const Box track{
      .left = layout.left, .top = layout.top, .right = layout.right, .bottom = layout.bottom};
  background_.draw(frame);
  outside_.draw(frame.within(track));
  band_.draw(frame.within(Box{.left = layout.x(window.early()),
                              .top = layout.top,
                              .right = layout.x(window.late()),
                              .bottom = layout.bottom}));
  track_.draw(frame.within(track));

  // Пунктир смещения: штрих в 2 пикселя через каждые 4.
  const double center = layout.x(window.offset());
  offset_.draw(frame.within(Box{.left = center - 1.0,
                                .top = layout.top + 2.0,
                                .right = center + 1.0,
                                .bottom = layout.bottom - 2.0}));

  const double zero = layout.x(core::Interval());
  zero_.draw(frame.within(Box{.left = zero - 1.0,
                              .top = layout.top - layout.reach,
                              .right = zero + 1.0,
                              .bottom = layout.bottom + layout.reach}));

  for (const core::Interval edge : {window.early(), window.late()}) {
    const double at = layout.x(edge);
    handle_.draw(frame.within(Box{.left = at - kHandleHalf,
                                  .top = layout.top - layout.reach,
                                  .right = at + kHandleHalf,
                                  .bottom = layout.bottom + layout.reach}));
  }

  ticks_.draw(frame);
}

} // namespace training::render
