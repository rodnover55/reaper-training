#include "actions.hpp"

#include "journal.hpp"
#include "trainer.hpp"
#include "window.hpp"

#include <array>
#include <optional>

namespace training::reaper {
namespace {

int (*hostRegister)(const char *name, void *infostruct) = nullptr;

struct Action {
  custom_action_register_t registration;
  /// Режим, который действие включает; пусто — действие окна.
  std::optional<grid::Mode> mode;
  int command = 0;
};

std::array<Action, 6> actions{{
    {.registration = {0, "REAPER_TRAINING_WINDOW", "reaper-training: Show/hide timing trainer",
                      nullptr},
     .mode = std::nullopt},
    {.registration = {0, "REAPER_TRAINING_MODE_QUARTERS", "reaper-training: Mode: quarters",
                      nullptr},
     .mode = grid::Mode::Quarters},
    {.registration = {0, "REAPER_TRAINING_MODE_EIGHTHS", "reaper-training: Mode: eighths",
                      nullptr},
     .mode = grid::Mode::Eighths},
    {.registration = {0, "REAPER_TRAINING_MODE_EIGHTH_TRIPLETS",
                      "reaper-training: Mode: eighth triplets", nullptr},
     .mode = grid::Mode::EighthTriplets},
    {.registration = {0, "REAPER_TRAINING_MODE_SIXTEENTHS",
                      "reaper-training: Mode: sixteenths", nullptr},
     .mode = grid::Mode::Sixteenths},
    {.registration = {0, "REAPER_TRAINING_MODE_SIXTEENTH_TRIPLETS",
                      "reaper-training: Mode: sixteenth triplets", nullptr},
     .mode = grid::Mode::SixteenthTriplets},
}};

const Action *actionOf(int command) {
  if (command == 0)
    return nullptr;
  for (const Action &action : actions)
    if (action.command == command)
      return &action;
  return nullptr;
}

bool onAction(KbdSectionInfo * /*section*/, int command, int /*val*/, int /*val2*/,
              int /*relmode*/, HWND /*hwnd*/) {
  const Action *action = actionOf(command);
  if (!action)
    return false;

  journal("action {}", action->registration.idStr);
  if (!action->mode) {
    toggleWindow();
    return true;
  }

  Settings settings = trainer().settings();
  settings.mode = *action->mode;
  trainer().setSettings(settings);
  showSettings();
  refreshWindow();
  return true;
}

/// Состояние переключателя для меню: 1 — включено, 0 — выключено, −1 —
/// действие не наше.
int toggleState(int command) {
  const Action *action = actionOf(command);
  if (!action)
    return -1;
  if (!action->mode)
    return windowShown() ? 1 : 0;
  return trainer().settings().mode == *action->mode ? 1 : 0;
}

} // namespace

void registerActions(reaper_plugin_info_t *rec) {
  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;
  for (Action &action : actions)
    action.command = hostRegister("custom_action", &action.registration);

  hostRegister("hookcommand2", reinterpret_cast<void *>(onAction));
  hostRegister("toggleaction", reinterpret_cast<void *>(toggleState));
}

void unregisterActions() {
  if (!hostRegister)
    return;

  hostRegister("-toggleaction", reinterpret_cast<void *>(toggleState));
  hostRegister("-hookcommand2", reinterpret_cast<void *>(onAction));
  for (Action &action : actions)
    hostRegister("-custom_action", &action.registration);
}

} // namespace training::reaper
