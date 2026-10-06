# Установка

## Что нужно

| Что | Какое |
|---|---|
| Система | Linux x86_64 с glibc 2.35 и новее: Ubuntu 22.04, Debian 12, Fedora 36 и всё, что свежее. Windows 10 и 11 (x64). macOS 12 и новее, Mac с процессором Intel и с Apple Silicon — один и тот же установщик. |
| REAPER | REAPER 7 или новее; на Windows — 64-битный. |

## Установщик

Установщик для своей системы скачивается со страницы
[Releases](https://github.com/rodnover55/reaper-training/releases/latest):

| Система | Установщик |
|---|---|
| Windows (x64) | `reaper-training-<версия>-windows-x64-setup.exe` |
| macOS (Apple Silicon и Intel) | `reaper-training-<версия>-macos.pkg` |
| Linux (x86_64) | `reaper-training-<версия>-linux-x86_64.run` |

Установщик сам находит REAPER и его папку ресурсов — ту, где лежат настройки
REAPER, — и кладёт расширение в её папку `UserPlugins`. Права администратора
ему не нужны: всё ставится в папку пользователя. Если REAPER не найден, он
старше 7-й версии или на Windows 32-битный, установщик говорит об этом и
ничего не ставит.

Установщики не подписаны сертификатом разработчика, поэтому при первом
запуске Windows и macOS предупреждают о неизвестном разработчике — ниже
сказано, как продолжить.

### Windows

1. Запустите `reaper-training-<версия>-windows-x64-setup.exe`.
2. Если появится окно «Система Windows защитила ваш компьютер» (SmartScreen),
   нажмите «Подробнее» и «Выполнить в любом случае».
3. Пройдите мастер. Расширение ляжет в `%APPDATA%\REAPER\UserPlugins`. Если
   REAPER запущен, установщик попросит его закрыть.

REAPER ищется по записи его установщика в реестре и в `Program Files`.
Портативный REAPER — тот, у которого `reaper.ini` лежит рядом с `reaper.exe`,
— в реестр себя не пишет, и установщик его не находит. Если REAPER не найден,
установщик предложит указать папку портативного REAPER вручную: нажмите «Да»
и выберите папку, где лежат `reaper.exe` и `reaper.ini`. Если обычный REAPER
тоже есть, папку портативного задайте ключом `/DIR`:

```
reaper-training-<версия>-windows-x64-setup.exe /DIR="D:\REAPER"
```

### macOS

1. Откройте `reaper-training-<версия>-macos.pkg`.
2. macOS не даст открыть пакет от неизвестного разработчика:
   - на macOS 12–14 нажмите на пакет с клавишей Control, выберите «Открыть»
     и в окне ещё раз «Открыть»;
   - на macOS 15 и новее закройте сообщение, откройте «Системные настройки» →
     «Конфиденциальность и безопасность», внизу у сообщения о пакете нажмите
     «Всё равно открыть» и подтвердите.
3. Пройдите установку. Если Installer спросит, куда ставить, или покажет
   выбор места с пустым выбором, нажмите строку «Установить только для меня».
   Пароль администратора установка не спрашивает; если спрашивает — значит,
   выбор «только для меня» не отмечен, вернитесь и отметьте его.
4. Если REAPER запущен, Installer попросит его закрыть.

Расширение ляжет в `~/Library/Application Support/REAPER/UserPlugins`.
Снимать пометку карантина командой `xattr` не нужно: файл, поставленный
пакетом, её не получает.

REAPER ищется в «Программах» — общих и своих (`~/Applications`). REAPER из
другой папки подходит, если он уже запускался: тогда в
`~/Library/Application Support/REAPER` есть его `reaper.ini`. Портативный
REAPER установщик не поддерживает — его расширение ставится
[вручную](#ручная-установка).

Тот же пакет ставится и из Терминала, без окон Installer:

```sh
installer -pkg ~/Downloads/reaper-training-<версия>-macos.pkg -target CurrentUserHomeDirectory
```

### Linux

В терминале, от своего пользователя, без `sudo`:

```sh
sh ~/Downloads/reaper-training-<версия>-linux-x86_64.run
```

Установщик ищет REAPER так же, как его ставит сценарий `install-reaper.sh` из
поставки REAPER: команда `reaper` в `PATH`, `/opt/REAPER`, `~/opt/REAPER`,
ярлык REAPER в меню; а ещё REAPER из Flatpak (`fm.reaper.Reaper`). Расширение
ставится в каждый найденный REAPER:

| Какой REAPER | Куда ложится расширение |
|---|---|
| обычный | `~/.config/REAPER/UserPlugins` |
| портативный: `reaper.ini` рядом с программой | `UserPlugins` в папке программы |
| из Flatpak | `~/.var/app/fm.reaper.Reaper/config/REAPER/UserPlugins` |

Если REAPER распакован в свою папку без установки и уже запускался, он тоже
найдётся — по `~/.config/REAPER/reaper.ini`. Портативный REAPER, которого
установщик не нашёл, передаётся ключом после `--` — папка, где лежит
`reaper.ini`:

```sh
sh reaper-training-<версия>-linux-x86_64.run -- --resource-path ~/REAPER
```

Если REAPER запущен, установщик заменяет файл, не мешая ему работать, и
просит перезапустить REAPER: новое расширение загрузится при следующем
запуске.

Все ключи — `sh reaper-training-<версия>-linux-x86_64.run -- --help`.

## Как проверить, что расширение загрузилось

При запуске REAPER расширение пишет строку в консоль REAPER (окно «ReaScript
console output»):

```
reaper-training 0.4.0 loaded (сборка 2026-10-06 21:37:12)
```

В строке — версия расширения, в скобках — дата и время сборки: по ним видно,
что загружено.

Если строки нет, проверьте, что файл расширения лежит в папке `UserPlugins`
именно той папки ресурсов, которую открывает пункт меню REAPER **Options →
Show REAPER resource path in explorer/finder**.

## Обновление

Скачайте установщик новой версии и запустите его, как при установке: он
заменит прошлую версию. На Windows и macOS установщик попросит закрыть REAPER,
на Linux — перезапустить его после установки.

## Удаление

- **Windows:** «Параметры» → «Приложения» → «Установленные приложения» →
  reaper-training → «Удалить». Расширение удаляется из всех папок, куда его
  ставил установщик; настройки REAPER остаются.
- **macOS:** закройте REAPER и удалите `reaper_training.dylib` из
  `~/Library/Application Support/REAPER/UserPlugins`.
- **Linux:**

  ```sh
  sh reaper-training-<версия>-linux-x86_64.run -- --uninstall
  ```

  Для портативного REAPER добавьте `--resource-path <папка REAPER>`.

## Ручная установка

Без установщика расширение ставится одним файлом — например, в портативный
REAPER на macOS.

1. Скачайте файл расширения для своей системы со страницы
   [Releases](https://github.com/rodnover55/reaper-training/releases/latest):

   | Система | Файл | Папка `UserPlugins` |
   |---|---|---|
   | Linux (x86_64) | `reaper_training.so` | `~/.config/REAPER/UserPlugins/` |
   | Windows (x64) | `reaper_training.dll` | `%APPDATA%\REAPER\UserPlugins\` |
   | macOS (Apple Silicon и Intel) | `reaper_training.dylib` | `~/Library/Application Support/REAPER/UserPlugins/` |

   Если REAPER портативный, папка ресурсов — другая: её открывает пункт меню
   **Options → Show REAPER resource path in explorer/finder**.
2. Закройте REAPER.
3. Положите файл в папку `UserPlugins` папки ресурсов REAPER. Если папки нет —
   создайте её.
4. Только macOS: снимите с файла пометку «скачано из интернета», иначе
   система не даст REAPER его загрузить. В Терминале:

   ```sh
   xattr -d com.apple.quarantine ~/Library/Application\ Support/REAPER/UserPlugins/reaper_training.dylib
   ```

   Если команда отвечает `No such xattr`, пометки нет и снимать нечего.
5. Запустите REAPER.

Обновление — так же: закройте REAPER, замените файл и на macOS снова снимите
пометку карантина. Удаление — удалить файл.

Файл можно и собрать из исходников проекта (на Linux и Windows; на macOS 12
свежего компилятора нет, и файл для Mac собирает CI). Нужны компилятор C++23,
CMake 3.29 и новее и Ninja; в папке проекта выполните:

```sh
cmake --preset default
cmake --build --preset default
cmake --install build
```

Последняя команда сама кладёт файл расширения в папку `UserPlugins`.
