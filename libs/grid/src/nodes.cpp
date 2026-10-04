#include "training/grid/nodes.hpp"

#include <cmath>

namespace training::grid {

NodeHit nearestNode(double time, Mode mode, const Timeline &timeline) {
  const int count = divisions(mode);
  const auto beat = static_cast<std::int64_t>(std::floor(timeline.beatsAt(time)));

  // Кандидаты — узлы своего удара и первый узел следующего: он же узел номер
  // count своего.
  NodeHit best{.beat = beat, .slot = 0, .time = timeline.timeAt(static_cast<double>(beat))};
  for (int slot = 1; slot <= count; ++slot) {
    const double nodeTime =
        timeline.timeAt(static_cast<double>(beat) + static_cast<double>(slot) / count);
    if (std::abs(time - nodeTime) < std::abs(time - best.time)) {
      best = slot == count ? NodeHit{.beat = beat + 1, .slot = 0, .time = nodeTime}
                           : NodeHit{.beat = beat, .slot = slot, .time = nodeTime};
    }
  }
  return best;
}

} // namespace training::grid
