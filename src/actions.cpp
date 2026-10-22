#include "actions.hpp"

#include "journal.hpp"
#include "trainer.hpp"
#include "window.hpp"

#include "training/reaper/mode.hpp"

#include <array>
#include <optional>

namespace training::reaper {
namespace {

int (*hostRegister)(const char *name, void *infostruct) = nullptr;

/// Действие тренажёра в списке действий REAPER: показать или спрятать окно
/// или включить режим.
struct Action {
  custom_action_register_t registration;
  /// Режим, который действие включает; пусто — действие окна.
  std::optional<Mode> mode;
  int command = 0;
};

std::array<Action, 6> actions{{
    {.registration = {0, "REAPER_TRAINING_WINDOW", "reaper-training: Show/hide timing trainer",
                      nullptr},
     .mode = std::nullopt},
    {.registration = {0, "REAPER_TRAINING_MODE_QUARTERS", "reaper-training: Mode: quarters",
                      nullptr},
     .mode = Mode::Quarters},
    {.registration = {0, "REAPER_TRAINING_MODE_EIGHTHS", "reaper-training: Mode: eighths",
                      nullptr},
     .mode = Mode::Eighths},
    {.registration = {0, "REAPER_TRAINING_MODE_EIGHTH_TRIPLETS",
                      "reaper-training: Mode: eighth triplets", nullptr},
     .mode = Mode::EighthTriplets},
    {.registration = {0, "REAPER_TRAINING_MODE_SIXTEENTHS",
                      "reaper-training: Mode: sixteenths", nullptr},
     .mode = Mode::Sixteenths},
    {.registration = {0, "REAPER_TRAINING_MODE_SIXTEENTH_TRIPLETS",
                      "reaper-training: Mode: sixteenth triplets", nullptr},
     .mode = Mode::SixteenthTriplets},
}};

/// Действие с номером команды `command`, который REAPER дал ему при
/// регистрации; nullptr — номер 0 или команда не наша.
const Action *actionOf(int command) {
  if (command == 0)
    return nullptr;
  for (const Action &action : actions)
    if (action.command == command)
      return &action;
  return nullptr;
}

/// Обработчик команд REAPER (`hookcommand2`): выполняет действие тренажёра и
/// пишет его в журнал. Действие окна показывает или прячет окно; действие
/// режима ставит режим в настройки, показывает их в окне и перерисовывает его.
/// Раздел действий, значения управления и окно-источник не используются.
///
/// @return правда, если команда наша и выполнена; ложь — команду обрабатывает
///   REAPER или другое расширение.
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

  trainer().setGridMode(*action->mode);
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
  return trainer().gridMode() == *action->mode ? 1 : 0;
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
