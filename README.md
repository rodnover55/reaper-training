# reaper-training

Расширение (extension plugin) для REAPER на C++: тренажёр точности игры под
метроном. Расширение берёт звук гитары со входа звуковой карты и находит в нём
ноты или берёт ноты со входа MIDI и показывает, на сколько миллисекунд каждая
отклонилась от клика.

Как поставить — в [руководстве по установке](docs/install.md), как
пользоваться — в [руководстве](docs/usage.md). Ниже — сборка и проверки для
разработки.

## Команды

| Команда | Для чего |
|---|---|
| `cmake --preset default` | Подготовить сборку. Нужно один раз в свежем клоне и после правок в `CMakeLists.txt`. |
| `cmake --build --preset default` | Собрать плагин и тесты. |
| `ctest --preset quick` | Проверить правку на ходу — без долгих линтеров и сборки под санитайзерами. |
| `cmake --workflow --preset push-check` | Полная проверка перед push. |
| `cmake --build build --target format` | Привести форматирование в порядок, когда упал `clang_format`. |
| `cmake --workflow --preset sanitize` | Прогнать тесты под ASan и UBSan, если есть подозрение на порчу памяти. |
| `cmake --install build` | Поставить плагин в REAPER и попробовать вживую. |
| `cmake --preset debug && cmake --build --preset debug && cmake --install build/debug` | Поставить отладочную сборку: отладочные действия `reaper-training (debug): …` и функции `TrainingDebug_*` для скриптов проверок. |
| `cmake --workflow --preset release` | Собрать модуль так, как его собирает CI для Releases, и прогнать тесты. Файл — в `build/release`. |
| `cmake --build --preset installer` | После `release` — собрать установщик для своей системы. Файл — в `build/release/installer`. Нужен инструмент установщика: Inno Setup на Windows, Xcode Command Line Tools на macOS, makeself на Linux; без него сборка цели падает с подсказкой, что поставить. |

Полный список — `cmake --list-presets=<configure|build|test|workflow>`. Личные
пресеты кладутся в `CMakeUserPresets.json`, он в репозиторий не попадает.

## Журнал и консоль

Любая сборка пишет журнал `reaper-training.log` в папке ресурсов REAPER:
загрузку с версией и временем сборки, найденные ноты, смены настроек, ход
калибровки. Если при запуске журнал больше 5 МБ, он становится
`reaper-training.log.1`, а прежний `.1` пропадает.

В консоль REAPER расширение не пишет ничего. Для тестовых сборок это
включает скрытая настройка — в окне её нет:

```ini
; reaper-extstate.ini в папке ресурсов REAPER, правится при закрытом REAPER
[reaper_training]
console_log=1
```

Тогда при запуске в консоли появляется строка `reaper-training <версия>
loaded (сборка <время>)`, а отладочные действия пишут туда свои результаты.
Из скрипта то же — `reaper.SetExtState("reaper_training", "console_log", "1",
true)`, выключить — `reaper.DeleteExtState("reaper_training", "console_log",
true)`; действует со следующего запуска.

## Граница шкалы окна попадания

Шкала окна попадания рядом с полями Offset и Tolerance идёт от −50 до +50 мс,
и окно попадания за неё не выходит. Границу задаёт скрытая настройка — в окне
её нет:

```ini
; reaper-extstate.ini в папке ресурсов REAPER, правится при закрытом REAPER
[reaper_training]
window_scale_ms=100
```

Целое число миллисекунд от 10 до 200; вне этих границ прижимается к ним, без
настройки — 50. Сохранённое окно попадания, которое в шкалу не помещается,
при запуске сужается и сдвигается к нулю. Расширение эту настройку только
читает.

## Сборки и релизы

GitHub Actions (`.github/workflows/build.yml`) на каждый push собирает модуль
пресетом `release`, прогоняет тесты и собирает установщик пресетом
`installer`:

| Система | Где собирается | Файл | Установщик |
|---|---|---|---|
| Linux x86_64 | Ubuntu 22.04, GCC 13 | `reaper_training.so` | `reaper-training-<версия>-linux-x86_64.run` — makeself |
| Windows x64 | MSVC | `reaper_training.dll` | `reaper-training-<версия>-windows-x64-setup.exe` — Inno Setup |
| macOS 12+, arm64 и x86_64 одним файлом | Apple clang | `reaper_training.dylib` | `reaper-training-<версия>-macos.pkg` — pkgbuild и productbuild |

Исходники установщиков — в `installer/`, почему они такие — в
`openspec/changes/archive/*-add-installers/design.md`. Затем каждый
установщик ставится на чистой машине, где ничего не собиралось
(`installer/*/ci-check.*`): без REAPER — установщик должен отказать, потом
с настоящим REAPER — установка, повторная установка, портативный REAPER,
удаление. Задача «Файлы релиза» складывает три модуля, три установщика и
`SHA256SUMS.txt` в артефакт `release` прогона.

Линтеры в CI не идут: их результат зависит от версии инструментов, и они
остаются в `push-check`.

macOS 12 — потолок MacBook Pro Early 2015, на котором тренажёр должен
работать. Машин с macOS 12 в GitHub Actions нет, поэтому CI проверяет только,
что файл объявляет 12.0 наименьшей системой для обоих процессоров
(`vtool -show-build`); вживую файл проверяется на самом Mac. Собирать на Mac с
macOS 12 не выйдет: самый новый Xcode для неё не тянет C++23 в нужном объёме.

Чтобы модуль грузился на чужих машинах, пресет `release` включает
`REAPER_TRAINING_STATIC_RUNTIME`: на Linux libstdc++ вкомпонована и скрыта
(`cmake/reaper_training.map`), на Windows рантайм MSVC статический.

Релиз:

1. Поднять версию в `project()` в `CMakeLists.txt` и закоммитить.
2. Поставить тег с той же версией и отправить его:
   ```sh
   git tag v0.2.0
   git push origin v0.2.0
   ```

Тег запускает ту же сборку и проверки и после них создаёт релиз из артефакта
`release`: три установщика, три модуля и `SHA256SUMS.txt`. Описание — `.github/release-notes.md` и список изменений,
собранный GitHub. Если тег не совпадает с версией в `CMakeLists.txt`, релиз не
создаётся. Тег с суффиксом (`v0.2.0-rc1`) даёт предварительный релиз.

## Windows на Linux

`bash tools/setup-windows-env.sh` ставит на Ubuntu окружение, в котором модуль
для Windows собирается настоящим MSVC и проверяется в REAPER для Windows под
Wine: Wine, MSVC через [msvc-wine](https://github.com/mstorsjo/msvc-wine),
шрифты Microsoft и REAPER той же версии, что в CI, со звуковой системой Dummy
Audio. Системные пакеты скрипт ставит через sudo, скачивание MSVC просит
принять лицензию Microsoft, остальное ложится в
`~/.local/share/reaper-training-windows`. Сборка:

```sh
. ~/.local/share/reaper-training-windows/env.sh
CC=cl CXX=cl cmake -S . -B build/wine-debug -G Ninja -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded \
  -DREAPER_TRAINING_STATIC_RUNTIME=ON -DREAPER_TRAINING_BUILD_TESTS=OFF \
  -DREAPER_TRAINING_WERROR=ON
cmake --build build/wine-debug --target reaper_training
```

Модуль кладётся в `UserPlugins` папки ресурсов REAPER —
`prefix/drive_c/users/<пользователь>/AppData/Roaming/REAPER` в той же папке, —
а REAPER запускает `reaper.sh`. Он задаёт русскую локаль: функции Win32 без
UNICODE читают текст в кодировке cp1251, как на русской Windows, и ошибки
кодировки видны так же. При каждом запуске REAPER показывает напоминание об
ознакомительной лицензии — его закрывает кнопка Still Evaluating.

## Инструменты

Для сборки нужны компилятор с C++23, CMake 3.29 и новее и Ninja. REAPER SDK,
WDL, {fmt} и doctest скачиваются при конфигурации.

Строки форматирует только {fmt}: на macOS 12 системная библиотека C++ не
форматирует дробные числа. За этим следит проверка `no_std_format`.

Остальные проверки подключаются сами, если инструмент есть в системе, и
пропускаются, если нет:

```sh
sudo apt install clang clang-tidy clang-format
```

clang-tidy идёт через кеш результатов [ctcache](https://github.com/matus-chochlik/ctcache):
неизменённый файл не анализируется заново, поэтому полный обход занимает минуты
только в первый раз. Скрипту нужен Python 3; без Python clang-tidy работает без
кеша. Кеш лежит в `build/ctcache/<версия clang-tidy>`; сбросить — удалить этот
каталог.
