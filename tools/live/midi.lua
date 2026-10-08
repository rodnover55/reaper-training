-- Нота виртуальной MIDI-клавиатуры REAPER: arg[1] — номер ноты, arg[2] —
-- громкость, 0 — конец ноты; канал 1. Нота попадает в историю MIDI-входа, как
-- нажатая клавиша.
local note = tonumber(arg[1])
local velocity = tonumber(arg[2])
if not note or not velocity then
  error("нота: номер и громкость, например 60 100")
end
reaper.StuffMIDIMessage(0, 0x90, note, velocity)
return "note " .. note .. " velocity " .. velocity .. " at " .. reaper.GetPlayPosition()
