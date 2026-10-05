#include "training/grid/bars.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace training::grid {
namespace {

/// Прыжок позиции назад, ударов, после которого `play` заводит новый проход.
constexpr double kJumpBack = 0.5;

/// Насколько далеко впереди позиции, ударов, нота ещё считается ранней.
constexpr double kEarly = 1.0;

/// Погрешность перевода времени в удары: позиция ровно на начале удара может
/// прийти как «235.9999…».
constexpr double kEdge = 1e-6;

/// Строка такта `bar`: удары не начаты, значений нет.
BarRow rowOf(const Bar &bar, bool entered) {
  BarRow row;
  row.bar = bar;
  row.beats.resize(static_cast<std::size_t>(std::max(bar.meter.beats, 1)));
  row.entered = entered;
  return row;
}

/// Начинает удар в режиме `mode`, если он ещё не начат.
void start(RowBeat &beat, Mode mode) {
  if (beat.mode)
    return;
  beat.mode = mode;
  beat.slots.resize(static_cast<std::size_t>(divisions(mode)));
}

/// Видимая строка: воспроизведение в её такт входило, и она не свёрнута.
bool visible(const BarRow &row) { return row.entered && !row.folded; }

/// Пересчитывает среднее и разброс строки по её значениям.
void refresh(BarRow &row) {
  row.mean.reset();
  row.spread.reset();

  double sum = 0.0;
  int count = 0;
  for (const RowBeat &beat : row.beats)
    for (const RowSlot &slot : beat.slots)
      if (slot.deviation) {
        sum += *slot.deviation;
        ++count;
      }
  if (count < 2)
    return;

  const double mean = sum / count;
  double squares = 0.0;
  for (const RowBeat &beat : row.beats)
    for (const RowSlot &slot : beat.slots)
      if (slot.deviation)
        squares += (*slot.deviation - mean) * (*slot.deviation - mean);

  row.mean = mean;
  row.spread = std::sqrt(squares / count);
}

/// Номер узла удара в режиме `mode`, ближайшего к моменту `time`; только узлы
/// самого удара `beat`, без первого узла следующего.
int nearestSlot(double time, std::int64_t beat, Mode mode, const Timeline &timeline) {
  const int count = divisions(mode);
  int best = 0;
  double bestDistance = std::abs(time - timeline.timeAt(static_cast<double>(beat)));
  for (int slot = 1; slot < count; ++slot) {
    const double distance = std::abs(
        time - timeline.timeAt(static_cast<double>(beat) + static_cast<double>(slot) / count));
    if (distance < bestDistance) {
      best = slot;
      bestDistance = distance;
    }
  }
  return best;
}

} // namespace

bool BarRow::hasValues() const {
  return std::ranges::any_of(beats, [](const RowBeat &beat) {
    return std::ranges::any_of(beat.slots,
                               [](const RowSlot &slot) { return slot.deviation.has_value(); });
  });
}

Bars::Bars(std::size_t keep) : keep_(std::max<std::size_t>(keep, 1)) {}

BarRow *Bars::currentRow() {
  // Верхняя видимая строка — та, в которую воспроизведение вошло последней:
  // сворачивается только уходящая строка.
  const auto found = std::ranges::find_if(rows_, visible);
  return found == rows_.end() ? nullptr : &*found;
}

bool Bars::play(double beats, double loopBeats, double toleranceMs, const Timeline &timeline) {
  const bool jumpedBack = position_ && beats < *position_ - kJumpBack;
  position_ = beats;
  loopBeats_ = loopBeats;

  const auto beat = static_cast<std::int64_t>(std::floor(beats + kEdge));
  const BarRow *top = currentRow();
  const bool entering = !top || jumpedBack || beat < top->bar.firstBeat ||
                        beat >= top->bar.firstBeat + top->bar.meter.beats;
  if (entering)
    enter(timeline.barOf(beat), toleranceMs);

  BarRow &row = *currentRow();
  const auto index = static_cast<int>(beat - row.bar.firstBeat);
  currentBeat_ = index;
  for (int k = 0; k <= index && k < static_cast<int>(row.beats.size()); ++k)
    start(row.beats[static_cast<std::size_t>(k)], mode_);
  return entering;
}

void Bars::stop() {
  position_.reset();
  currentBeat_.reset();
}

void Bars::enter(const Bar &bar, double toleranceMs) {
  // Уходящая строка — верхняя видимая, под ней — следующая видимая.
  const auto leaving = std::ranges::find_if(rows_, visible);
  if (leaving != rows_.end()) {
    leaving->leftToleranceMs = toleranceMs;
    const auto below = std::find_if(leaving + 1, rows_.end(), visible);
    if (!leaving->hasValues() && below != rows_.end() && !below->hasValues()) {
      // Позднее значение может прийти только в последнюю свёрнутую строку:
      // прежние свёрнутые уже ничего не получат.
      std::erase_if(rows_, [](const BarRow &row) { return row.folded; });
      std::ranges::find_if(rows_, visible)->folded = true;
    }
  }

  // Строку ранней ноты этого такта забирает вход; строки ранних нот других
  // тактов устарели.
  BarRow fresh = rowOf(bar, true);
  const auto early = std::ranges::find_if(
      rows_, [&bar](const BarRow &row) { return !row.entered && row.bar.index == bar.index; });
  if (early != rows_.end()) {
    fresh = std::move(*early);
    fresh.entered = true;
  }
  std::erase_if(rows_, [](const BarRow &row) { return !row.entered; });

  rows_.push_front(std::move(fresh));
  trim();
}

BarRow *Bars::rowFor(const Bar &bar, double nodeBeats, bool create) {
  if (!position_)
    return nullptr;

  // Место узла относительно позиции: по прямой и по кругу петли.
  const double ahead = nodeBeats - *position_;
  const double around = loopBeats_ > 0.0 ? std::remainder(ahead, loopBeats_) : ahead;
  const BarRow *top = currentRow();
  const bool topBar = top && top->bar.index == bar.index;

  if (around > 0.0) {
    if (around > kEarly)
      return nullptr;
    if (topBar && ahead >= 0.0)
      return currentRow();

    // Ранняя нота к такту или проходу, который ещё не начался.
    const auto early = std::ranges::find_if(rows_, [&bar](const BarRow &row) {
      return !row.entered && row.bar.index == bar.index;
    });
    if (early != rows_.end())
      return &*early;
    if (!create)
      return nullptr;
    rows_.push_front(rowOf(bar, false));
    return &rows_.front();
  }

  if (topBar && ahead <= 0.0)
    return currentRow();

  // Узел позади позиции в другом такте или в прошлом проходе такта
  // верхней строки — новейшая строка этого такта, кроме верхней.
  for (BarRow &row : rows_)
    if (row.entered && row.bar.index == bar.index && &row != top)
      return &row;
  return nullptr;
}

void Bars::addNote(double time, double rate, double toleranceMs, const Timeline &timeline) {
  if (!position_)
    return;

  const double at = timeline.beatsAt(time);
  const auto beat = static_cast<std::int64_t>(std::floor(at));
  const Bar bar = timeline.barOf(beat);

  // Режим удара — из его строки, если удар уже начат там, иначе текущий.
  Mode mode = mode_;
  if (const BarRow *guess = rowFor(bar, at, false))
    if (const auto &started =
            guess->beats[static_cast<std::size_t>(beat - bar.firstBeat)].mode)
      mode = *started;

  // Ближайший узел: узлы удара и первый узел следующего удара.
  const int count = divisions(mode);
  int slot = nearestSlot(time, beat, mode, timeline);
  const double next = timeline.timeAt(static_cast<double>(beat + 1));
  const double slotTime =
      timeline.timeAt(static_cast<double>(beat) + static_cast<double>(slot) / count);
  std::int64_t nodeBeat = beat;
  Bar nodeBar = bar;
  if (std::abs(time - next) < std::abs(time - slotTime)) {
    nodeBeat = beat + 1;
    slot = 0;
    if (nodeBeat >= bar.firstBeat + bar.meter.beats)
      nodeBar = timeline.barOf(nodeBeat);
  }

  const double nodeBeats =
      static_cast<double>(nodeBeat) + static_cast<double>(slot) / divisions(mode);
  BarRow *row = rowFor(nodeBar, nodeBeats, true);
  if (!row)
    return;

  RowBeat &cells = row->beats[static_cast<std::size_t>(nodeBeat - row->bar.firstBeat)];
  start(cells, nodeBeat == beat ? mode : mode_);
  const Mode beatMode = cells.mode.value_or(mode);
  if (beatMode != mode && slot != 0)
    // Строка узла начала удар в другом режиме, чем строка, по которой искали
    // узел: узел ищется заново в её режиме.
    slot = nearestSlot(time, nodeBeat, beatMode, timeline);

  const double nodeTime = timeline.timeAt(static_cast<double>(nodeBeat) +
                                          static_cast<double>(slot) / divisions(beatMode));
  const double deviation = (time - nodeTime) / rate;
  RowSlot &target = cells.slots[static_cast<std::size_t>(slot)];
  if (target.deviation) {
    target.extra = true;
    if (std::abs(deviation) < std::abs(*target.deviation)) {
      target.deviation = deviation;
      target.toleranceMs = toleranceMs;
    }
  } else {
    target.deviation = deviation;
    target.toleranceMs = toleranceMs;
  }

  row->folded = false;
  refresh(*row);
  trim();
}

void Bars::clear() {
  rows_.clear();
  position_.reset();
  currentBeat_.reset();
  loopBeats_ = 0.0;
}

void Bars::trim() {
  std::size_t seen = 0;
  for (auto it = rows_.begin(); it != rows_.end(); ++it) {
    if (visible(*it) && ++seen == keep_) {
      rows_.erase(it + 1, rows_.end());
      return;
    }
  }
}

} // namespace training::grid
