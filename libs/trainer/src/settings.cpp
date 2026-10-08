#include "training/trainer/settings.hpp"

#include <fmt/format.h>

#include <cmath>
#include <string>
#include <string_view>

namespace training::trainer {
namespace {

/// Правда, если `value` — целое из `range`.
bool wholeIn(double value, const Range &range) {
  return range.contains(value) && std::floor(value) == value;
}

/// Допустимые значения поля: диапазон и то, при каких остальных настройках
/// он такой.
struct Bounds {
  /// Диапазон без единиц: «-40…+40», «1…50», «-90…-10».
  std::string range;

  /// Остальные настройки, от которых зависит диапазон: « at tolerance 10»;
  /// пусто — диапазон постоянный.
  std::string condition;
};

/// Допустимые значения поля `field` при настройках `settings`.
Bounds boundsOf(Field field, const Settings &settings) {
  using grid::halfMsText;
  using grid::MsStyle;
  switch (field) {
  case Field::Offset: {
    const Range range = settings.offsetRange();
    return {.range = fmt::format("{}…{}", halfMsText(range.min, MsStyle::Signed),
                                 halfMsText(range.max, MsStyle::Signed)),
            .condition = fmt::format(" at tolerance {}",
                                     halfMsText(settings.toleranceMs(), MsStyle::Plain))};
  }
  case Field::Tolerance: {
    const Range range = settings.toleranceRange();
    return {.range = fmt::format("{}…{}", halfMsText(range.min, MsStyle::Plain),
                                 halfMsText(range.max, MsStyle::Plain)),
            .condition = fmt::format(" at offset {}",
                                     halfMsText(settings.offsetMs(), MsStyle::Signed))};
  }
  case Field::Silence:
    break;
  }
  return {.range = fmt::format("{}…{}", std::lround(Settings::kSilenceRange.min),
                               std::lround(Settings::kSilenceRange.max)),
          .condition = {}};
}

/// Имя поля `field` в окне.
const char *nameOf(Field field) {
  switch (field) {
  case Field::Offset:
    return "Offset";
  case Field::Tolerance:
    return "Tolerance";
  case Field::Silence:
    break;
  }
  return "Silence";
}

/// Единица числа поля `field`.
const char *unitOf(Field field) { return field == Field::Silence ? "dBFS" : "ms"; }

/// Действующее значение поля `field` в настройках `settings`, как его пишет
/// поле.
std::string valueOf(Field field, const Settings &settings) {
  switch (field) {
  case Field::Offset:
    return grid::halfMsText(settings.offsetMs(), grid::MsStyle::Signed);
  case Field::Tolerance:
    return grid::halfMsText(settings.toleranceMs(), grid::MsStyle::Plain);
  case Field::Silence:
    break;
  }
  return fmt::format("{}", std::lround(settings.silenceDb()));
}

} // namespace

bool Settings::setMode(grid::Mode mode) {
  switch (mode) {
  case grid::Mode::Quarters:
  case grid::Mode::Eighths:
  case grid::Mode::EighthTriplets:
  case grid::Mode::Sixteenths:
  case grid::Mode::SixteenthTriplets:
    mode_ = mode;
    return true;
  }
  return false;
}

bool Settings::setScaleMs(double scaleMs) {
  if (!wholeIn(scaleMs, kScaleRange) || !grid::fits(window_, scaleMs))
    return false;
  scaleMs_ = scaleMs;
  return true;
}

bool Settings::setWindow(const grid::HitWindow &window) {
  if (!grid::fits(window, scaleMs_))
    return false;
  window_ = window;
  return true;
}

bool Settings::setOffsetMs(double offsetMs) {
  return setWindow({.offsetMs = offsetMs, .toleranceMs = window_.toleranceMs});
}

bool Settings::setToleranceMs(double toleranceMs) {
  return setWindow({.offsetMs = window_.offsetMs, .toleranceMs = toleranceMs});
}

Range Settings::offsetRange() const {
  const double limit = scaleMs_ - window_.toleranceMs;
  return {.min = -limit, .max = limit};
}

Range Settings::toleranceRange() const {
  return {.min = grid::kMinToleranceMs, .max = scaleMs_ - std::abs(window_.offsetMs)};
}

bool Settings::setSilenceDb(double silenceDb) {
  if (!wholeIn(silenceDb, kSilenceRange))
    return false;
  silenceDb_ = silenceDb;
  return true;
}

bool Settings::setChannel(int channel) {
  if (channel < 0)
    return false;
  channel_ = channel;
  return true;
}

std::string rangeHint(Field field, const Settings &settings) {
  const Bounds bounds = boundsOf(field, settings);
  return fmt::format("{} {}{}", bounds.range, unitOf(field), bounds.condition);
}

std::string rejectionText(Field field, std::string_view text, const Settings &settings) {
  const Bounds bounds = boundsOf(field, settings);
  return fmt::format("{} {} {} is out of range {}{} — not applied, {} in use", nameOf(field),
                     text, unitOf(field), bounds.range, bounds.condition,
                     valueOf(field, settings));
}

int audioChannel(const Settings &settings) {
  return settings.midiInput().empty() ? settings.channel() : -1;
}

bool inputChanged(const Settings &before, const Settings &after) {
  return before.channel() != after.channel() || before.midiInput() != after.midiInput() ||
         before.midiIndex() != after.midiIndex();
}

std::string inputText(const Settings &settings) {
  if (settings.midiInput().empty())
    return fmt::format("channel {}", settings.channel() + 1);
  return fmt::format("MIDI «{}» #{}", settings.midiInput(), settings.midiIndex());
}

} // namespace training::trainer
