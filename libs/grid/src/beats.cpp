#include "training/grid/beats.hpp"

#include "training/grid/nodes.hpp"

#include <algorithm>
#include <cmath>

namespace training::grid {
namespace {

/// Открытая строка удара `beat` в `rows` — время удара ещё не прошло — или
/// пустой указатель, если такой нет.
BeatRow *openRow(std::deque<BeatRow> &rows, std::int64_t beat) {
  const auto found = std::ranges::find_if(
      rows, [beat](const BeatRow &row) { return row.beat == beat && !row.closed; });
  return found == rows.end() ? nullptr : &*found;
}

/// Пересчитывает `complete`, `mean` и `spread` строки по её узлам.
void refresh(BeatRow &row) {
  row.complete = std::ranges::all_of(
      row.slots, [](const Slot &slot) { return slot.deviation.has_value() || slot.missing; });
  row.mean.reset();
  row.spread.reset();
  if (!row.complete || row.slots.size() < 2)
    return;

  double sum = 0.0;
  int played = 0;
  for (const Slot &slot : row.slots) {
    if (slot.deviation) {
      sum += *slot.deviation;
      ++played;
    }
  }
  if (played == 0)
    return;

  const double mean = sum / played;
  double squares = 0.0;
  for (const Slot &slot : row.slots) {
    if (slot.deviation)
      squares += (*slot.deviation - mean) * (*slot.deviation - mean);
  }

  row.mean = mean;
  row.spread = std::sqrt(squares / played);
}

} // namespace

Beats::Beats(std::size_t keep) : keep_(std::max<std::size_t>(keep, 1)) {}

void Beats::addNote(double time, double rate, const Timeline &timeline) {
  // Режим удара — тот, в котором удар начался: если у удара ближайшего узла
  // уже есть открытая строка, узел ищется заново в её режиме.
  Mode mode = mode_;
  NodeHit hit = nearestNode(time, mode, timeline);
  for (int attempt = 0; attempt < 2; ++attempt) {
    const BeatRow *existing = openRow(rows_, hit.beat);
    if (!existing || existing->mode == mode)
      break;
    mode = existing->mode;
    hit = nearestNode(time, mode, timeline);
  }

  BeatRow *row = openRow(rows_, hit.beat);
  if (row && row->mode != mode)
    return;

  if (!row) {
    BeatRow fresh;
    fresh.beat = hit.beat;
    fresh.label = timeline.barBeat(hit.beat);
    fresh.mode = mode;
    fresh.slots.resize(static_cast<std::size_t>(divisions(mode)));
    rows_.push_front(std::move(fresh));
    row = &rows_.front();
  }

  Slot &slot = row->slots[static_cast<std::size_t>(hit.slot)];
  const double deviation = (time - hit.time) / rate;
  if (slot.deviation) {
    slot.extra = true;
    if (std::abs(deviation) < std::abs(*slot.deviation))
      slot.deviation = deviation;
  } else {
    slot.deviation = deviation;
    slot.missing = false;
  }
  refresh(*row);

  while (rows_.size() > keep_)
    rows_.pop_back();
}

void Beats::advance(double now, const Timeline &timeline) {
  for (BeatRow &row : rows_) {
    if (row.closed)
      continue;

    const int count = divisions(row.mode);
    const auto beat = static_cast<double>(row.beat);
    for (int index = 0; index < count; ++index) {
      Slot &slot = row.slots[static_cast<std::size_t>(index)];
      if (slot.deviation || slot.missing)
        continue;

      // Середина между узлом и следующим: у последнего узла удара следующий —
      // первый узел следующего удара, то есть узел номер count.
      const double node = timeline.timeAt(beat + static_cast<double>(index) / count);
      const double next = timeline.timeAt(beat + static_cast<double>(index + 1) / count);
      if (now > (node + next) / 2.0)
        slot.missing = true;
    }

    // Окно удара кончается там же, где окно его последнего узла.
    const double last = timeline.timeAt(beat + static_cast<double>(count - 1) / count);
    const double following = timeline.timeAt(beat + 1.0);
    if (now > (last + following) / 2.0)
      row.closed = true;

    refresh(row);
  }
}

} // namespace training::grid
