#include "training/trainer/clock.hpp"

#include <fmt/format.h>

#include <chrono>

namespace training::trainer {

std::string momentText(Moment moment) {
  // Целыми, а не через double: в double наносекунды от точки отсчёта теряют
  // младшие знаки. Доли микросекунды отбрасываются: порядок моментов в
  // записи остаётся тем же, хотя равными могут стать разные моменты.
  const std::chrono::nanoseconds since = moment.time_since_epoch();
  const bool negative = since < std::chrono::nanoseconds::zero();
  const std::chrono::nanoseconds magnitude = negative ? -since : since;
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(magnitude);
  const auto fraction =
      std::chrono::duration_cast<std::chrono::microseconds>(magnitude - seconds);
  return fmt::format("{}{}.{:06}", negative ? "-" : "", seconds.count(), fraction.count());
}

} // namespace training::trainer
