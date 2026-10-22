-- Переменная настроек REAPER: arg[1] — имя, arg[2] — новое значение; без
-- значения только читает. Ставит её публичным set_config_var_string, как из
-- окна настроек, например count-in перед записью: projmetroen 23. Возвращает
-- значение до и после.
local name, value = arg[1], arg[2]
if not name then
  error("переменная: имя и значение, например projmetroen 23")
end
local _, before = reaper.get_config_var_string(name)
if value then
  if not reaper.set_config_var_string(name, value, 0) then
    error("переменную " .. name .. " не поставить")
  end
end
local _, after = reaper.get_config_var_string(name)
return name .. ": " .. tostring(before) .. " -> " .. tostring(after)
