# Tasks

## 1. Часы ядра

- [x] 1.1 Тип момента `trainer::Moment` и промежутки
      `std::chrono::nanoseconds` в `libs/trainer`; `Transport::start`,
      `Transport::takesOnset`, `AudioBlock::monotonic`,
      `AudioOnset::monotonic`, `Calibrator::start` и `update` получают
      `Moment`, `kNoSoundSeconds` — промежуток; связка строит `Moment` из
      `steady_clock` одной функцией вместо двух `monotonicNow`, миллисекунды
      «shown after» считает из разности моментов (D1). Проверка — сборка;
      юнит-тесты транспорта, калибровки и поиска атак переписаны на `Moment` и
      проходят; `trainer_isolation` проходит; в публичных заголовках
      `libs/trainer` нет моментов в `double`

## 2. Ядро: запуск и режим

- [x] 2.1 `Transport` владеет `OnsetSearch` и атомарным состоянием запуска;
      `start` и `stop` записывают его; `OnsetSearch::process` читает его перед
      каждым блоком: новый момент начала — детектор и история забыты, нет
      запуска — забыты и блок не разобран, блок до начала выброшен до
      детектора; свой статус `OnsetSearch` — флаг `running_`, номер `run_`,
      методы `start` и `stop` — и `AudioOnset::run` уходят; «onset» пишет
      момент блока (D3, D5). Проверка — юнит-тесты: блоки до
      `Transport::start` и после `stop` атак не дают; после остановки и
      запуска без перерыва в звуке блок, взятый до нового начала, не даёт атак
      и не входит в поток детектора — атака первого блока после начала стоит
      там же, где без прежнего блока; «onset» пишет момент блока
- [x] 2.2 `MidiNotes` — поле `Transport`: `Transport::start` получает позицию
      начала и сам вызывает `MidiNotes::start`; `Transport` отдаёт отбор нот
      для `take` и `onInputChange` (D3). Проверка — юнит-тесты: после
      `Transport::start` с позицией P нота count-in — пришедшая раньше, чем
      позиция ушла от P, — не измеряется, а нота после сдвига измеряется;
      новый запуск кончает мёртвое время прошлого
- [x] 2.3 Функция режима звукового пути по `Transport` и `Calibrator`:
      «атаки», «калибровка», «ничего» (D1). Проверка — юнит-тесты: режим при
      идущем запуске, при стоящем транспорте со слушающей калибровкой и без
      неё; `trainer_isolation` проходит

## 3. Связка

- [x] 3.1 `AudioInput`: одно атомарное значение режима вместо
      `calibrating_`, `running_`, `runs_` и методов `setCalibrating`,
      `startRun`, `stopRun`; хук читает режим раз на блок, берёт звук по нему,
      пишет режим и момент в заголовок и `GetPlayStateEx` не зовёт; рабочий
      поток подаёт блоки в поиск атак из `Transport`, `followRun` уходит;
      `REAPERAPI_WANT_GetPlayStateEx` уходит из `src/audio_input.cpp` и
      `src/main.cpp` (D2, D3). Проверка — сборка; в `src/` нет
      `GetPlayStateEx`, `setCalibrating`, `startRun`, `stopRun`, `followRun`
- [x] 3.2 `Trainer`: поле `Transport` объявлено раньше `AudioInput`;
      `onPlayState` передаёт в `Transport::start` момент, позицию начала и
      компенсацию и `MidiNotes::start` не вызывает, отбор нот берётся у
      `Transport`; режим ядра передаётся в `AudioInput` в `onPlayState`, после
      смены настроек, в `pollCalibration`, при запуске, конце и применении
      калибровки; «start:» пишет момент начала запуска (D1, D3, D5);
      `tools/live/transport.lua`
      получает команду остановки и запуска в одном вызове. Проверка — сборка
      и тесты; вживую чёрным ящиком инструментами `tools/live/`: запуск,
      пауза, продолжение, остановка, запись, остановка и запуск в одном вызове
      — на каждый запуск строка «start:» с моментом начала, в том числе после
      остановки и запуска в одном вызове; калибровка щелчком «Calibrate…» при
      стоящем транспорте получает звук — журнал «calibration: … Hz, silence
      step»; в журнале нет «onset» с моментом блока раньше момента последнего
      «start:» перед ним; нота виртуальной MIDI-клавиатуры на воспроизведении
      — журнал «midi note»
- [x] 3.3 Имена пути уведомлений REAPER: класс `Surface` →
      `ReaperNotificationRelay`, `src/control_surface.*` →
      `src/reaper_notification_relay.*`, `registerSurface` и
      `unregisterSurface` → `registerNotificationRelay` и
      `unregisterNotificationRelay`; описание класса — невидимая поверхность
      управления REAPER (`csurf_inst`) (D6). Проверка — сборка; в `src/` и
      `CMakeLists.txt` нет `class Surface`, `control_surface`,
      `registerSurface`; вживую запуск и остановка дают «start:» в журнале

## 4. Итоговая проверка

- [x] 4.1 Все автоматические проверки всех пресетов (тесты, clang-tidy,
      clang-format, санитайзеры, изоляция библиотек) и сборка Windows под
      MSVC; вживую на Xvfb: воспроизведение, пауза, продолжение, остановка,
      запись с count-in, остановка и запуск в одном вызове, калибровка.
      Проверка — все тесты проходят; журнал без ошибок, моменты «onset» не
      раньше момента «start:» своего запуска
