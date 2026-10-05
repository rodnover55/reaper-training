# reaper-training

Расширение (extension plugin) для REAPER на C++: тренажёр точности игры под
метроном. Расширение берёт звук гитары со входа звуковой карты, находит в нём
ноты и показывает, на сколько миллисекунд каждая отклонилась от клика.

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
| `cmake --preset debug && cmake --build --preset debug && cmake --install build/debug` | Поставить отладочную сборку: журнал `reaper-training.log` в папке ресурсов REAPER и отладочные действия `reaper-training (debug): …`. |
| `cmake --workflow --preset release` | Собрать модуль так, как его собирает CI для Releases, и прогнать тесты. Файл — в `build/release`. |

Полный список — `cmake --list-presets=<configure|build|test|workflow>`. Личные
пресеты кладутся в `CMakeUserPresets.json`, он в репозиторий не попадает.

## Сборки и релизы

GitHub Actions (`.github/workflows/build.yml`) на каждый push собирает модуль
пресетом `release` и прогоняет тесты:

| Система | Где собирается | Файл |
|---|---|---|
| Linux x86_64 | Ubuntu 22.04, GCC 13 | `reaper_training.so` |
| Windows x64 | MSVC | `reaper_training.dll` |
| macOS 12+, arm64 и x86_64 одним файлом | Apple clang | `reaper_training.dylib` |

Файлы лежат в артефактах прогона. Линтеры в CI не идут: их результат зависит
от версии инструментов, и они остаются в `push-check`.

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

Тег запускает ту же сборку и после неё создаёт релиз с тремя файлами и
`SHA256SUMS.txt`. Описание — `.github/release-notes.md` и список изменений,
собранный GitHub. Если тег не совпадает с версией в `CMakeLists.txt`, релиз не
создаётся. Тег с суффиксом (`v0.2.0-rc1`) даёт предварительный релиз.

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
