#include "training/core/training/state/bar.hpp"

#include <iterator>
#include <utility>

namespace training::core::training::state {

Bar::Bar(std::string name, Moment start, Moment end, Meter meter, double tempo)
    : name_(std::move(name)), start_(start), end_(end), meter_(meter),
      tempo_{{Beat(), tempo}} {}

Bar::Bar(const Bar &other) : Bar(other, std::scoped_lock(other.mutex_)) {}

Bar::Bar(const Bar &other, const std::scoped_lock<std::mutex> & /*lock*/)
    : name_(other.name_), start_(other.start_), end_(other.end_), meter_(other.meter_),
      tempo_(other.tempo_), divisions_(other.divisions_), notes_(other.notes_) {}

Bar &Bar::operator=(const Bar &other) {
  if (this == &other)
    return *this;
  const std::scoped_lock lock(mutex_, other.mutex_);
  name_ = other.name_;
  start_ = other.start_;
  end_ = other.end_;
  meter_ = other.meter_;
  tempo_ = other.tempo_;
  divisions_ = other.divisions_;
  notes_ = other.notes_;
  return *this;
}

std::string Bar::name() const {
  const std::scoped_lock lock(mutex_);
  return name_;
}

Moment Bar::start() const {
  const std::scoped_lock lock(mutex_);
  return start_;
}

Moment Bar::end() const {
  const std::scoped_lock lock(mutex_);
  return end_;
}

bool Bar::contains(Moment time) const {
  const std::scoped_lock lock(mutex_);
  return holds(time);
}

Meter Bar::meter() const {
  const std::scoped_lock lock(mutex_);
  return meter_;
}

std::map<Beat, double> Bar::tempo() const {
  const std::scoped_lock lock(mutex_);
  return tempo_;
}

std::map<Beat, Moment> Bar::divisions() const {
  const std::scoped_lock lock(mutex_);
  return divisions_;
}

std::map<Beat, std::vector<Note>> Bar::notes() const {
  const std::scoped_lock lock(mutex_);
  return notes_;
}

void Bar::changeTempo(Beat beat, double tempo) {
  const std::scoped_lock lock(mutex_);
  tempo_[beat] = tempo;
}

bool Bar::addDivision(Beat beat, Moment time) {
  const std::scoped_lock lock(mutex_);
  if (time < start_ || divisions_.contains(beat))
    return false;
  const auto after = divisions_.upper_bound(beat);
  if (after != divisions_.end() && after->second <= time)
    return false;
  if (after != divisions_.begin() && std::prev(after)->second >= time)
    return false;
  divisions_.emplace_hint(after, beat, time);
  return true;
}

bool Bar::addNote(Beat beat, const Note &note) {
  const std::scoped_lock lock(mutex_);
  const auto found = divisions_.find(beat);
  if (found == divisions_.end() || !holds(found->second))
    return false;
  notes_[beat].push_back(note);
  return true;
}

bool operator==(const Bar &left, const Bar &right) {
  if (&left == &right)
    return true;
  const std::scoped_lock lock(left.mutex_, right.mutex_);
  return left.name_ == right.name_ && left.start_ == right.start_ && left.end_ == right.end_ &&
         left.meter_ == right.meter_ && left.tempo_ == right.tempo_ &&
         left.divisions_ == right.divisions_ && left.notes_ == right.notes_;
}

} // namespace training::core::training::state
