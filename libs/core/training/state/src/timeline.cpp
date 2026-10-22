#include "training/core/training/state/timeline.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace training::core::training::state {

Timeline::Timeline(const Timeline &other) : Timeline(other, std::scoped_lock(other.mutex_)) {}

Timeline &Timeline::operator=(const Timeline &other) {
  if (this == &other)
    return *this;
  const std::scoped_lock lock(mutex_, other.mutex_);
  bars_ = other.bars_;
  return *this;
}

std::vector<Bar> Timeline::bars() const {
  const std::scoped_lock lock(mutex_);
  return bars_;
}

void Timeline::add(const Bar &bar) {
  const std::scoped_lock lock(mutex_);
  bars_.push_back(bar);
}

bool Timeline::addNote(Position position, const Note &note) {
  const std::scoped_lock lock(mutex_);
  if (!timeOf(position))
    return false;
  return bars_[position.bar].addNote(position.division, note);
}

std::optional<Position> Timeline::nearest(Moment time) const {
  const std::scoped_lock lock(mutex_);
  if (std::ranges::none_of(bars_, [time](const Bar &bar) { return bar.holds(time); }))
    return std::nullopt;

  // Ближайшая действующая доля и её время; при равном расстоянии остаётся
  // ранняя по времени доля.
  std::optional<std::pair<Position, Moment>> best;
  for (std::size_t index = 0; index < bars_.size(); ++index)
    for (const auto &[division, at] : bars_[index].divisions_) {
      if (!acts(index, at))
        continue;
      if (best) {
        const Interval distance = (time - at).abs();
        const Interval bestDistance = (time - best->second).abs();
        if (distance > bestDistance || (distance == bestDistance && at >= best->second))
          continue;
      }
      best = {Position{.bar = index, .division = division}, at};
    }
  if (!best)
    return std::nullopt;
  return best->first;
}

std::optional<Moment> Timeline::time(Position position) const {
  const std::scoped_lock lock(mutex_);
  return timeOf(position);
}

std::optional<Moment> Timeline::timeOf(Position position) const {
  if (position.bar >= bars_.size())
    return std::nullopt;
  const auto &divisions = bars_[position.bar].divisions_;
  const auto found = divisions.find(position.division);
  if (found == divisions.end() || !acts(position.bar, found->second))
    return std::nullopt;
  return found->second;
}

bool Timeline::acts(std::size_t index, Moment at) const {
  return bars_[index].holds(at) &&
         std::none_of(bars_.begin() + static_cast<std::ptrdiff_t>(index) + 1, bars_.end(),
                      [at](const Bar &later) { return later.holds(at); });
}

} // namespace training::core::training::state
