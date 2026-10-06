# Tasks

Проверки вживую записываются в `findings.md` этого изменения. Проверки CI
идут в ветке `feature/installers` и её PR.

## 1. Цель сборки установщика

- [x] 1.1 Завести `installer/CMakeLists.txt` с целью `installer` вне `all`
      (design D5): поиск инструмента своей системы, понятная ошибка при
      сборке, если его нет; пресет сборки `installer` на конфигурации
      `release`. Проверка — на Linux без makeself `cmake --build --preset
      installer` падает с сообщением, что поставить; `cmake --build --preset
      release` по-прежнему собирается без инструментов установщика
- [x] 1.2 Описать в `README.md` команду сборки установщика и где лежит
      результат. Проверка — команда из README собирает установщик на Linux
      с makeself

## 2. Установщик для Linux

- [x] 2.1 Написать `installer/linux/install.sh` (design D4): отказ от root,
      поиск REAPER в `PATH`, `/opt`, `~/opt`, ярлыках, Flatpak и по
      `~/.config/REAPER/reaper.ini`, проверка ELF x86-64 и версии по
      `whatsnew.txt`, папка ресурсов по правилу `install-reaper.sh`,
      атомарная замена, `--uninstall`, `--resource-path`, сообщения на
      русском и английском. Проверка — прогон под `dash` с подставным `HOME`
      и подставными REAPER: без REAPER — ошибка и пусто; `~/opt/REAPER` —
      модуль в `~/.config/REAPER/UserPlugins`; портативный — в его папке;
      REAPER 6 и ARM — отказ; `--resource-path` без `reaper.ini` — отказ;
      повторная установка — один файл; `--uninstall` — файла нет,
      `reaper.ini` на месте; запуск от root — отказ
- [x] 2.2 Собирать `.run` целью `installer` через makeself с SHA-256.
      Проверка — `cmake --build --preset installer` даёт
      `build/release/installer/reaper-training-<версия>-linux-x86_64.run`,
      `sh … --check` проходит, установка из него повторяет проверки 2.1
- [x] 2.3 CI: makeself в задаче сборки Linux, установщик — артефактом;
      задача `installer-check` для Linux в `ubuntu:22.04`: отказ
      от root, отказ без REAPER, REAPER 7.82 через `install-reaper.sh
      --install ~/opt`, установка, повтор, портативный, `--uninstall`.
      Проверка — прогон CI в PR зелёный, в журнале видны все шаги
- [x] 2.4 Описать установку на Linux в `docs/install.md`: команда запуска,
      ключи, Flatpak, перезапуск REAPER, удаление. Проверка — команды из
      руководства работают на `.run` из артефактов CI

## 3. Установщик для macOS

- [x] 3.1 Написать `installer/macos/distribution.xml` и ресурсы `en.lproj`,
      `ru.lproj` (design D3): только домашняя папка, проверка REAPER на
      JavaScript, `must-close`, macOS от 12.0, обе архитектуры; собирать
      `.pkg` целью `installer` через `pkgbuild` и `productbuild`. Проверка —
      CI собирает `reaper-training-<версия>-macos.pkg`, `pkgutil
      --payload-files` показывает один модуль
- [x] 3.2 CI: задача `installer-check` для macOS: отказ без REAPER, REAPER
      7.82 из DMG в `/Applications`, пакет с пометкой карантина ставится
      `installer -target CurrentUserHomeDirectory`, модуль в
      `~/Library/Application Support/REAPER/UserPlugins` без карантина и
      совпадает с собранным, повторная установка. Проверка — прогон CI в PR
      зелёный
- [x] 3.3 Описать установку на macOS в `docs/install.md`: открытие
      неподписанного пакета на macOS 12–14 и 15+, выбор «Установить только
      для меня», закрытие REAPER, удаление; убрать шаг с `xattr` из основного
      пути. Проверка — шаги руководства совпадают с design D3 и текстами
      пакета (`welcome.html`, `conclusion.html`)

## 4. Установщик для Windows

- [x] 4.1 Написать `installer/windows/reaper-training.iss` (design D2):
      режим без прав администратора, поиск REAPER по реестру и Program
      Files, проверки PE и версии, портативный REAPER и выбор папки,
      проверка `REAPERwnd` перед установкой и удалением, деинсталлятор вне
      папки REAPER, английский и русский; собирать целью `installer` через
      `iscc`. Проверка — CI собирает
      `reaper-training-<версия>-windows-x64-setup.exe`
- [x] 4.2 CI: Inno Setup в задаче сборки Windows (Chocolatey, если в раннере
      нет); задача `installer-check` для Windows: отказ без REAPER, тихая
      установка REAPER 7.82 и печать его ключей реестра, установка
      `/VERYSILENT` — модуль в `%APPDATA%\REAPER\UserPlugins`, журнал Inno
      Setup — режим без прав администратора, повтор, портативный через
      `/DIR=`, удаление — модуля нет, `reaper.ini` на месте. Проверка —
      прогон CI в PR зелёный; если REAPER пишет реестр не так, как в design
      D2, поиск исправлен и это записано в `findings.md`
- [x] 4.3 Описать установку на Windows в `docs/install.md`: SmartScreen,
      портативный REAPER, удаление в «Установке и удалении программ».
      Проверка — шаги руководства совпадают с поведением установщика в
      журнале CI

## 5. Релиз

- [x] 5.1 Выкладывать в релиз установщики вместе с модулями, контрольные
      суммы — по всем файлам; обновить таблицу файлов в
      `.github/release-notes.md` и раздел «Сборки и релизы» в `README.md`.
      Проверка — задача `release` собирает `dist` из артефактов в PR-прогоне
      без выкладки (шаг выкладки только по тегу) и печатает список файлов и
      `SHA256SUMS.txt`
- [x] 5.2 Поднять версию до 0.4.0 и описать установщики в «Что нового» в
      `.github/release-notes.md`. Проверка — CI собирает установщики с
      версией 0.4.0 в имени

## 6. Проверки вживую

- [x] 6.1 Linux: поставить `.run` из артефактов CI в настоящий REAPER в
      `~/opt/REAPER`, запустить REAPER — строка версии в консоли; повторить с
      запущенным REAPER — он работает дальше. Результат — в `findings.md`
