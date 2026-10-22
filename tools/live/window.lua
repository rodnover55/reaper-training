-- Показывает окно тренажёра, если оно закрыто, и закрывает, если открыто, —
-- действием REAPER_TRAINING_WINDOW. Возвращает, открыто ли окно теперь.
reaper.set_action_options(3)
local command = reaper.NamedCommandLookup("_REAPER_TRAINING_WINDOW")
if command == 0 then
  error("действия REAPER_TRAINING_WINDOW нет: модуль не загружен")
end
reaper.Main_OnCommand(command, 0)
return reaper.GetToggleCommandState(command) == 1 and "window shown" or "window closed"
