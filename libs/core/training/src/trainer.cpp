#include "training/core/training/trainer.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace training::core::training {

std::expected<void, InvalidStep> Trainer::start() {
  const std::scoped_lock lock(mutex_);
  if (state_.step != Step::Off)
    return std::unexpected(InvalidStep{});
  state_ = State{.step = Step::Working, .timeline = {}};
  waiting_.clear();
  return {};
}

void Trainer::stop() {
  const std::scoped_lock lock(mutex_);
  state_.step = Step::Off;
  waiting_.clear();
}

Trainer::State Trainer::result() const {
  const std::scoped_lock lock(mutex_);
  return state_;
}

void Trainer::setSettings(const Settings &settings) {
  const HitWindow window = settings.window();
  const std::scoped_lock lock(mutex_);
  hitWindow_ = window;
}

void Trainer::addBar(const state::Bar &bar) {
  const std::scoped_lock lock(mutex_);
  state_.timeline.add(bar);
  // `Trainer::addBar#place-waiting-notes`: ждущие ноты — по порядку времени.
  std::vector<Moment> again = std::exchange(waiting_, {});
  std::ranges::sort(again);
  for (const Moment time : again)
    place(time);
}

void Trainer::process(const Note &note) {
  const std::scoped_lock lock(mutex_);
  if (state_.step != Step::Working)
    return;
  place(note.time);
}

bool Trainer::working() const {
  const std::scoped_lock lock(mutex_);
  return state_.step == Step::Working;
}

void Trainer::place(Moment time) {
  // Ближайшая доля ищется, когда пришёл такт после времени ноты: его доли
  // тоже участвуют в поиске.
  const bool after = std::ranges::any_of(
      state_.timeline.bars(), [time](const state::Bar &bar) { return bar.start() > time; });
  const std::optional<state::Position> position =
      after ? state_.timeline.nearest(time) : std::nullopt;
  // Ближайшая доля действует, и время у неё есть всегда.
  const std::optional<Moment> at = position ? state_.timeline.time(*position) : std::nullopt;
  if (!position || !at) {
    waiting_.push_back(time);
    return;
  }

  const Interval deviation = time - *at;
  (void)state_.timeline.addNote(*position,
                                {.deviation = deviation, .hit = hitWindow_.hit(deviation)});
}

} // namespace training::core::training
