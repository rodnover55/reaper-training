#include "training/trainer/loop.hpp"

#include "training/grid/note_time.hpp"

namespace training::trainer {
namespace {

/// Правда, если `position` в петле с повтором и длиной.
bool inside(double position, const Loop &loop) {
  return loop.repeat && loop.end > loop.start && position >= loop.start &&
         position <= loop.end;
}

} // namespace

double loopWrapped(double time, double position, const Loop &loop, grid::Mode mode,
                   const grid::Timeline &timeline) {
  if (!inside(position, loop))
    return time;

  const double step = 1.0 / static_cast<double>(grid::divisions(mode));
  const double half =
      (timeline.timeAt(timeline.beatsAt(loop.start) + step) - loop.start) / 2.0;
  return grid::wrapIntoLoop(time, loop.start - half, loop.end - half);
}

double loopBeats(double position, const Loop &loop, const grid::Timeline &timeline) {
  if (!inside(position, loop))
    return 0.0;
  return timeline.beatsAt(loop.end) - timeline.beatsAt(loop.start);
}

} // namespace training::trainer
