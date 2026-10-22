#include "training/reaper/run_timeline.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <string>

namespace training::reaper {
namespace {

/// Насколько позиция блока может разойтись с отрезком и всё ещё продолжать
/// его: позиции REAPER — секунды с округлением.
constexpr core::Interval kContinuity = core::Interval::kMillisecond;

/// Сдвиг вперёд от начала отсчёта по тактам: начало ровно на границе такта
/// или чуть раньше неё считается в такте после границы. Пре-ролл REAPER
/// начинает на полблока звука раньше такта — 5.3 мс при блоке 512 сэмплов и
/// 48 кГц, — и без допуска сверху встала бы строка такта перед ним.
constexpr core::Interval kEdge = 25 * core::Interval::kMillisecond;

/// Самая короткая петля, которую карта предсказывает: короче — не петля, а
/// ошибка, и предсказание шло бы без конца.
constexpr core::Interval kShortestLoop = 10 * core::Interval::kMillisecond;

/// Знак «минус» в подписи такта затакта: U+2212, а не дефис.
constexpr const char *kMinus = "−";

} // namespace

RunTimeline::RunTimeline(const TempoMap &project)
    : project_(project),
      segments_{Segment{.time = core::Moment(),
                        .place = {},
                        .offset = 0,
                        .firstBar = std::numeric_limits<std::int64_t>::min(),
                        .countIn = false,
                        .initial = true,
                        .from = core::Moment()}} {}

void RunTimeline::start(RunStart start, const CountIn &countIn,
                        std::optional<std::int64_t> leadInEnd, double sampleRate) {
  countIn_.emplace(project_, start.start.position, sampleRate);
  countInLength_ = countIn;
  leadInEnd_ = leadInEnd;
  sampleRate_ = sampleRate;
  // Первый отрезок — связка: с T0 проект играет с позиции начала.
  segments_ = {Segment{.time = start.time,
                       .place = start.start,
                       .offset = 0,
                       .firstBar = std::numeric_limits<std::int64_t>::min(),
                       .countIn = true,
                       .initial = true,
                       .from = start.time}};
}

std::optional<std::int64_t> RunTimeline::addBlock(core::Moment time, ProjectTime place) {
  // До конца отсчёта REAPER держит позицию блоков на позиции начала.
  if (time < segments_.front().time)
    return std::nullopt;

  const auto continues = [&](const Segment &segment) {
    return (place.position - projectAt(segment, time)).abs() <= kContinuity &&
           place.rate == segment.place.rate;
  };
  std::size_t current = indexAt(time);
  std::optional<std::int64_t> again;
  // Блок, который противоречит предсказанному отрезку, отменяет его: петли не
  // было.
  if (segments_[current].predicted && !continues(segments_[current])) {
    again = dropAfter(current - 1);
    current = indexAt(time);
  }

  Segment &segment = segments_[current];
  if (continues(segment)) {
    segment.predicted = false;
    if (place.position > segment.place.position)
      segment.moving = true;
    return again;
  }

  // Остальное меняет путь воспроизведения после блока: предсказанное по
  // прежнему пути отменяется.
  const auto earliest = [&](std::optional<std::int64_t> bar) {
    if (bar && (!again || *bar < *again))
      again = bar;
  };
  earliest(dropAfter(current));

  // Удержание после перемотки: звук с места перехода пойдёт с этого блока.
  if (segment.jump && !segment.moving &&
      (place.position - segment.place.position).abs() <= kContinuity &&
      place.rate == segment.place.rate) {
    segment.time = time;
    earliest(segment.firstBar);
    return again;
  }

  const bool continuous = (place.position - projectAt(segment, time)).abs() <= kContinuity;
  Segment next{.time = time,
               .place = place,
               .offset = segment.offset,
               .firstBar = positionAt(time).bar,
               .countIn = false,
               .initial = segment.initial,
               .from = time};
  if (!continuous) {
    // Петля или перемотка: такты отрезка — после последнего такта прошлого.
    const std::int64_t lastBar = positionAt(time - core::Interval::kNanosecond).bar;
    next.firstBar = lastBar + 1;
    next.offset = next.firstBar - reaper::barAt(project_, place.position, sampleRate_).index;
    next.initial = false;
    next.jump = true;
  }
  segments_.push_back(next);
  // Смена скорости меняет время тактов после того, в котором она случилась.
  earliest(continuous ? next.firstBar + 1 : next.firstBar);
  return again;
}

std::optional<std::int64_t> RunTimeline::setLoop(std::optional<ProjectLoop> loop) {
  if (loop == loop_)
    return std::nullopt;
  loop_ = loop;
  // Предсказанные отрезки — в конце списка: убирается всё с первого из них.
  const auto predicted = std::ranges::find_if(segments_, &Segment::predicted);
  if (predicted == segments_.end())
    return std::nullopt;
  return dropAfter(static_cast<std::size_t>(std::distance(segments_.begin(), predicted)) - 1);
}

void RunTimeline::predict(core::Moment until) {
  if (!loop_ || loop_->end - loop_->start < kShortestLoop)
    return;
  for (;;) {
    const Segment &last = segments_.back();
    if (last.place.position >= loop_->end)
      return;
    const core::Moment wrap = timeOn(last, loop_->end);
    if (wrap > until)
      return;
    // Отрезок с начала петли — как после перехода: такты после последнего.
    Segment next{.time = wrap,
                 .place = {.position = loop_->start, .rate = last.place.rate},
                 .offset = 0,
                 .firstBar = positionAt(wrap - core::Interval::kNanosecond).bar + 1,
                 .countIn = false,
                 .initial = false,
                 .jump = true,
                 .from = wrap,
                 .moving = true,
                 .predicted = true};
    next.offset = next.firstBar - reaper::barAt(project_, loop_->start, sampleRate_).index;
    segments_.push_back(next);
  }
}

core::Moment RunTimeline::heard(ProjectTime heard, core::Moment now) const {
  // Время позиции на каждом отрезке, где она есть; первый отрезок тянется
  // назад, последний — вперёд.
  const core::Moment position = heard.position;
  std::optional<core::Moment> best;
  for (std::size_t k = 0; k < segments_.size(); ++k) {
    const Segment &segment = segments_[k];
    const bool afterStart = k == 0 || position >= segment.place.position;
    const bool beforeEnd =
        k + 1 == segments_.size() || position < projectAt(segment, segments_[k + 1].time);
    if (!afterStart || !beforeEnd)
      continue;
    const core::Moment candidate = timeOn(segment, position);
    if (!best || (candidate - now).abs() < (*best - now).abs())
      best = candidate;
  }
  return best.value_or(timeOn(segments_.back(), position));
}

Position RunTimeline::positionAt(core::Moment time) const {
  const Segment &segment = segmentAt(time);
  const Position at = mapOf(segment).positionAt(projectAt(segment, time));
  return {.bar = at.bar + segment.offset, .beat = at.beat};
}

core::Moment RunTimeline::timeAt(Position position) const {
  const Segment &segment = segmentOf(position.bar);
  return timeOn(segment, mapOf(segment).timeAt(
                             {.bar = position.bar - segment.offset, .beat = position.beat}));
}

MapBar RunTimeline::barOf(std::int64_t index) const {
  const Segment &segment = segmentOf(index);
  MapBar bar = mapOf(segment).barOf(index - segment.offset);
  bar.index = index;
  return bar;
}

std::int64_t RunTimeline::firstBar() const {
  return positionAt(segments_.front().time - countInLength_.barsLength + kEdge).bar;
}

core::training::state::Bar RunTimeline::barAt(std::int64_t index, Mode mode) const {
  const Segment &segment = segmentOf(index);
  const std::int64_t projectBar = index - segment.offset;
  const MapBar map = mapOf(segment).barOf(projectBar);
  const bool leadIn = segment.initial && leadInEnd_ && projectBar < *leadInEnd_;

  core::Moment start = timeAt({.bar = index, .beat = 0.0});
  // После петли или перемотки первый такт отрезка звучит с места его начала:
  // доли — с него, а сам такт — с первого блока отрезка (см. «Удержание»).
  core::Moment divisionsFrom = start;
  if (segment.jump && segment.firstBar == index) {
    divisionsFrom = std::max(start, segment.time);
    start = std::min(divisionsFrom, segment.from);
  }

  // Конец — начало следующего такта на карте отрезка: при воспроизведении
  // подряд с него звучит следующий такт.
  const core::Moment end =
      timeOn(segment, mapOf(segment).timeAt({.bar = projectBar + 1, .beat = 0.0}));

  core::training::state::Bar bar(leadIn ? kMinus + std::to_string(*leadInEnd_ - projectBar)
                                        : std::to_string(projectBar + 1),
                                 start, end, map.meter,
                                 map.tempo.at(core::training::state::Beat()));
  for (const auto &[beat, tempo] : map.tempo)
    bar.changeTempo(beat, tempo);

  const int count = divisions(mode);
  for (int node = 0; node < map.meter.beats * count; ++node) {
    const double place = static_cast<double>(node) / count;
    const core::Moment time = timeAt({.bar = index, .beat = place});
    if (time >= divisionsFrom)
      bar.addDivision(core::training::state::Beat(place), time);
  }
  return bar;
}

const TempoMap &RunTimeline::mapOf(const Segment &segment) const {
  if (segment.countIn && countIn_)
    return *countIn_;
  return project_;
}

core::Moment RunTimeline::projectAt(const Segment &segment, core::Moment time) {
  return segment.place.position + (time - segment.time) * segment.place.rate;
}

core::Moment RunTimeline::timeOn(const Segment &segment, core::Moment position) {
  return segment.time + (position - segment.place.position) / segment.place.rate;
}

const RunTimeline::Segment &RunTimeline::segmentAt(core::Moment time) const {
  const auto after = std::ranges::upper_bound(segments_, time, {}, &Segment::time);
  return after == segments_.begin() ? segments_.front() : *std::prev(after);
}

std::size_t RunTimeline::indexAt(core::Moment time) const {
  const auto after = std::ranges::upper_bound(segments_, time, {}, &Segment::time);
  return after == segments_.begin()
             ? 0
             : static_cast<std::size_t>(std::distance(segments_.begin(), after)) - 1;
}

std::optional<std::int64_t> RunTimeline::dropAfter(std::size_t index) {
  if (index + 1 >= segments_.size())
    return std::nullopt;
  const std::int64_t bar = segments_[index + 1].firstBar;
  segments_.erase(segments_.begin() + static_cast<std::ptrdiff_t>(index) + 1, segments_.end());
  return bar;
}

const RunTimeline::Segment &RunTimeline::segmentOf(std::int64_t bar) const {
  const auto after = std::ranges::upper_bound(segments_, bar, {}, &Segment::firstBar);
  return after == segments_.begin() ? segments_.front() : *std::prev(after);
}

} // namespace training::reaper
