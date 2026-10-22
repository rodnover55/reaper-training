#include "training/render/calibration/calibration_panel.hpp"

#include "training/render/calibration/db_text.hpp"
#include "training/render/palette.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace training::render {
namespace {

/// Сколько шага нот без нот проходит до подсказки проверить вход.
constexpr core::Interval kQuietHint = 5 * core::Interval::kSecond;

/// Сколько после звука в тишине держится подсказка о звуке.
constexpr core::Interval kSoundHint = 2 * core::Interval::kSecond;

/// Сколько нот просит сыграть шаг нот.
constexpr int kNotes = 8;

/// Сколько шаг нот ждёт новой ноты, прежде чем подвести итог.
constexpr core::Interval kNotesTimeout = 15 * core::Interval::kSecond;

/// Сколько звука слушает шаг тишины: по нему идёт полоса хода шага.
constexpr core::Interval kSilenceLength = 3 * core::Interval::kSecond;

/// Знак минуса — тот же, что у отклонений.
constexpr std::string_view kMinus = "−";

using core::calibration::Calibrator;
using core::calibration::NotesStep;
using core::calibration::Step;

/// Номер этапа калибровки в строке шагов панели: 1 — тишина, 2 — ноты, 3 —
/// итог.
constexpr int kSilenceStage = 1;
constexpr int kNotesStage = 2;
constexpr int kResultStage = 3;

/// Номер этапа калибровки на шаге калибратора `step`: до шага нот — тишина.
int stageOf(Step step) {
  switch (step) {
  case Step::Off:
  case Step::Silence:
    return kSilenceStage;
  case Step::Notes:
    return kNotesStage;
  case Step::Done:
    break;
  }
  return kResultStage;
}

/// Самая тихая и самая громкая атака, dBFS; без нот — наименьший уровень.
struct AttackRange {
  double softestDb = core::calibration::kFloorDb;
  double loudestDb = core::calibration::kFloorDb;
};

/// Самая тихая и самая громкая из атак `attacksDb`.
AttackRange rangeOf(const std::vector<double> &attacksDb) {
  if (attacksDb.empty())
    return {};
  const auto [softest, loudest] = std::ranges::minmax_element(attacksDb);
  return {.softestDb = *softest, .loudestDb = *loudest};
}

} // namespace

CalibrationPanel::CalibrationPanel(std::string host, const core::Settings &settings)
    : host_(std::move(host)), settings_(settings), background_(kBackground),
      say_(Label::Style{.color = kBright, .align = TextAlign::TopLeft}), hint_(kNeutral) {}

PanelLayout CalibrationPanel::layoutOf(const Box &area) {
  const double width = area.right - area.left;
  const double height = area.bottom - area.top;
  const double pad = std::max(8.0, width * 0.022);
  PanelLayout layout{.left = area.left + pad,
                     .right = area.right - pad,
                     .top = area.top + height * 0.03,
                     .bottom = area.bottom - height * 0.03,
                     .width = width,
                     .height = height};
  layout.buttonsTop = area.bottom - height * 0.02 - kButtonHeight;
  return layout;
}

void CalibrationPanel::setSession(const Calibrator::State *state, const std::string &channel,
                                  double previousSilenceDb, bool heardSound) {
  channel_ = channel;
  previousSilenceDb_ = previousSilenceDb;
  heardSound_ = heardSound;
  text_ = progress_ = slots_ = table_ = false;
  scaleAt_ = ScaleAt::None;

  // Обрыв без звука — третий шаг, как итог.
  if (noSound_) {
    steps_.setCurrent(kResultStage);
    noSound();
    return;
  }

  const int stage = state ? stageOf(state->step) : kSilenceStage;
  steps_.setCurrent(stage);
  switch (stage) {
  case kSilenceStage:
    silenceStep(state);
    return;
  case kNotesStage:
    notesStep(*state);
    return;
  default:
    result(*state);
  }
}

std::string CalibrationPanel::stays() const {
  return fmt::format(" Silence threshold stays {} dBFS.", dbText(settings_.silenceDb()));
}

void CalibrationPanel::silenceStep(const Calibrator::State *state) {
  text_ = true;
  say_.setText("Mute the strings and don't play");
  say_.setColor(kBright);
  const core::Interval left = state ? state->silenceLeft : kSilenceLength;
  if (state && heardSound_ && state->sinceSound < kSoundHint) {
    hint_.setText("Heard a sound — starting over. Rest your palm on the strings.");
    hint_.setColor(kCurrentBeat);
  } else {
    hint_.setText(fmt::format("Listening to the background noise… {} s",
                              std::ceil(left / core::Interval::kSecond)));
    hint_.setColor(kNeutral);
  }

  progress_ = true;
  progressBar_.setDone(1.0 - left / kSilenceLength);

  ScaleMarks marks{.oldThreshold = settings_.silenceDb()};
  if (state)
    marks.level = state->levelDb;
  levelScale_.setMarks(std::move(marks));
  scaleAt_ = ScaleAt::Bottom;
}

void CalibrationPanel::notesStep(const Calibrator::State &state) {
  text_ = true;
  say_.setText(fmt::format("Play {} single notes", kNotes));
  say_.setColor(kBright);
  const std::vector<double> &attacks = state.attacksDb;
  const core::Interval waited = state.sinceLastNote;
  if (attacks.empty() && waited > kQuietHint) {
    hint_.setText(fmt::format("No notes heard on {}. Is the guitar connected to this input? "
                              "Stops in {} s.",
                              channel_,
                              std::ceil((kNotesTimeout - waited) / core::Interval::kSecond)));
    hint_.setColor(kCurrentBeat);
  } else {
    hint_.setText("One at a time, muting each. Play as you practice; make two or three of "
                  "them softer.");
    hint_.setColor(kNeutral);
  }

  slots_ = true;
  noteSlots_.setAttacks(attacks, kNotes);
  levelScale_.setMarks(ScaleMarks{.level = state.levelDb,
                                  .noise = state.noiseDb,
                                  .attacks = attacks,
                                  .oldThreshold = settings_.silenceDb()});
  scaleAt_ = ScaleAt::Bottom;
}

void CalibrationPanel::noSound() {
  text_ = true;
  say_.setText("No sound from the audio device");
  say_.setColor(kBad);
  hint_.setText(
      fmt::format("Nothing came from the audio device for 2 s. {0} closes the audio "
                  "device when playback is stopped and {0} is not the active "
                  "window, if \"Close audio device when stopped and application is "
                  "inactive\" is on in Preferences → Audio. Check the audio device in "
                  "Preferences → Audio → Device, then calibrate again.",
                  host_) +
      stays());
  hint_.setColor(kNeutral);
}

void CalibrationPanel::result(const Calibrator::State &state) {
  text_ = true;
  hint_.setColor(kNeutral);
  const std::string stay = stays();
  const std::size_t notes = state.attacksDb.size();
  const AttackRange attacks = rangeOf(state.attacksDb);

  if (state.verdict == NotesStep::Verdict::NotEnoughNotes) {
    say_.setText(
        notes == 0 ? fmt::format("No notes heard on {}", channel_)
                   : fmt::format("Heard only {} of {} notes on {}", notes, kNotes, channel_));
    say_.setColor(kBad);
    hint_.setText(
        fmt::format("No new note for {} s. Check that the guitar is connected to the "
                    "input selected in Input and that its volume is up, then "
                    "calibrate again.",
                    static_cast<double>(kNotesTimeout / core::Interval::kSecond)) +
        stay);
    return;
  }

  const double gap = std::round(attacks.softestDb - state.noiseDb);
  // Слишком близкий шум — меньше 12 дБ до щелчка: число округляется вниз, чтобы
  // 11.5 дБ не читались как «12 dB».
  const double tooClose = std::floor(attacks.softestDb - state.noiseDb);
  switch (state.verdict) {
  case NotesStep::Verdict::Good:
    say_.setText(fmt::format("Silence threshold: {} → {} dBFS", dbText(previousSilenceDb_),
                             dbText(settings_.silenceDb())));
    say_.setColor(kGood);
    hint_.setText(
        fmt::format("Set halfway between the noise and the softest pick — {} dB apart.", gap));
    break;
  case NotesStep::Verdict::Clipping:
    say_.setText("The input clips");
    say_.setColor(kBad);
    hint_.setText(fmt::format("The loudest note reached {} dBFS. Turn the input gain down on "
                              "the audio interface so the hardest notes stay below {}6 dBFS, "
                              "then calibrate again.",
                              dbText(state.peakDb, 1), kMinus) +
                  stay);
    break;
  case NotesStep::Verdict::NoiseTooClose:
  case NotesStep::Verdict::NotEnoughNotes:
    say_.setText("Noise is too close to the pick");
    say_.setColor(kBad);
    hint_.setText(fmt::format("Only {} dB between the noise and the softest pick: notes will "
                              "be missed or noise will count as notes. Raise the input gain, "
                              "turn the guitar volume up, or move away from what hums, then "
                              "calibrate again.",
                              tooClose) +
                  stay);
    break;
  }

  table_ = true;
  const std::array<TableRow, 3> rows{
      TableRow{.label = "Noise above 1 kHz", .value = dbText(state.noiseDb) + " dBFS"},
      TableRow{.label = "Pick attack above 1 kHz",
               .value = fmt::format("{} … {} dBFS", dbText(attacks.softestDb),
                                    dbText(attacks.loudestDb))},
      TableRow{.label = "Signal peak", .value = dbText(state.peakDb, 1) + " dBFS"}};
  levelTable_.setRows(rows);

  ScaleMarks marks{.noise = state.noiseDb,
                   .attacks = state.attacksDb,
                   .peak = state.peakDb,
                   .oldThreshold = previousSilenceDb_};
  if (state.verdict == NotesStep::Verdict::Good)
    marks.newThreshold = settings_.silenceDb();
  levelScale_.setMarks(std::move(marks));
  scaleAt_ = ScaleAt::Buttons;
}

void CalibrationPanel::draw(const Frame &frame) const {
  const PanelLayout layout = layoutOf(frame.area);
  const double headSize = std::max(kMinFont, layout.height * 0.045);
  const double saySize = std::max(kMinFont + 4.0, layout.height * 0.095);
  const double hintSize = std::max(kMinFont, layout.height * 0.046);
  const double scaleHeight = std::max(70.0, layout.height * 0.24);
  const double fullRight = layout.left + layout.width;

  background_.draw(frame);
  steps_.draw(frame.within(Box{.left = layout.left,
                               .top = layout.top,
                               .right = fullRight,
                               .bottom = layout.top + headSize}));
  if (!text_)
    return;

  const double sayTop = layout.top + headSize * 1.2 + layout.height * 0.04;
  say_.draw(frame.within(Box{
      .left = layout.left, .top = sayTop, .right = layout.right, .bottom = sayTop + saySize}));
  const double hintTop = sayTop + saySize * 1.3;
  const Box hintArea{.left = layout.left,
                     .top = hintTop,
                     .right = layout.right,
                     .bottom = hintTop + hintSize};
  const double hintBottom = hint_.bottom(frame.canvas, hintArea);
  hint_.draw(frame.within(hintArea));

  if (progress_) {
    // Полоса хода шага над шкалой.
    const double barHeight = std::max(3.0, layout.height * 0.012);
    const double barTop = layout.bottom - scaleHeight - layout.height * 0.03 - barHeight;
    progressBar_.draw(frame.within(Box{.left = layout.left,
                                       .top = barTop,
                                       .right = layout.right,
                                       .bottom = barTop + barHeight}));
  }
  if (slots_) {
    const double top = hintBottom + layout.height * 0.04;
    noteSlots_.draw(frame.within(Box{.left = layout.left,
                                     .top = top,
                                     .right = fullRight,
                                     .bottom = top + layout.height * 0.13}));
  }
  if (table_) {
    const double top = hintBottom + layout.height * 0.03;
    const double size = std::max(kMinFont, layout.height * 0.046);
    levelTable_.draw(frame.within(
        Box{.left = layout.left, .top = top, .right = fullRight, .bottom = top + size}));
  }
  if (scaleAt_ != ScaleAt::None) {
    const double bottom =
        scaleAt_ == ScaleAt::Bottom ? layout.bottom : layout.buttonsTop - 6.0;
    levelScale_.draw(frame.within(Box{.left = layout.left + layout.width * 0.025,
                                      .top = bottom - scaleHeight,
                                      .right = layout.right - layout.width * 0.025,
                                      .bottom = bottom}));
  }
}

} // namespace training::render
