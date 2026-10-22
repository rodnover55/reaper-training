-- Транспорт REAPER, как кнопками: arg[1] — play, record, pause, stop или
-- restart. restart — остановка и запуск в одном вызове: REAPER сообщает обе
-- перемены, а звук идёт без перерыва. Запись — действием Transport: Record
-- (1013). Без дорожки, готовой к записи, REAPER показал бы окно «Record
-- Warning», поэтому перед записью первая дорожка готовится к записи со входа
-- виртуальной MIDI-клавиатуры; если дорожек нет, она создаётся. Выход из
-- записи — действием 40667, которое не спрашивает, сохранять ли записанное.
-- Возвращает состояние транспорта после команды (GetPlayState).

-- Вход дорожки: MIDI (4096), устройство 62 — виртуальная клавиатура, все
-- каналы.
local VIRTUAL_KEYBOARD = 4096 + 62 * 32

local command = arg[1]
if command == "play" then
  reaper.OnPlayButton()
elseif command == "record" then
  if reaper.CountTracks(0) == 0 then
    reaper.InsertTrackAtIndex(0, true)
  end
  local track = reaper.GetTrack(0, 0)
  reaper.SetMediaTrackInfo_Value(track, "I_RECINPUT", VIRTUAL_KEYBOARD)
  reaper.SetMediaTrackInfo_Value(track, "I_RECARM", 1)
  reaper.Main_OnCommand(1013, 0)
elseif command == "pause" then
  reaper.OnPauseButton()
elseif command == "stop" then
  if reaper.GetPlayState() & 4 ~= 0 then
    reaper.Main_OnCommand(40667, 0)
  else
    reaper.OnStopButton()
  end
elseif command == "restart" then
  reaper.OnStopButton()
  reaper.OnPlayButton()
else
  error("транспорт: play, record, pause, stop или restart, а не " .. tostring(command))
end
return "play state " .. reaper.GetPlayState() .. ", position " .. reaper.GetPlayPosition()
