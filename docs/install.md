# Установка

## Что нужно

| Что | Какое |
|---|---|
| Система | Linux x86_64 с glibc 2.35 и новее: Ubuntu 22.04, Debian 12, Fedora 36 и всё, что свежее. macOS 12 и новее, Mac с процессором Intel и с Apple Silicon — один и тот же файл. |
| REAPER | REAPER 7. |

## Как поставить

1. Скачайте файл расширения для своей системы со страницы
   [Releases](https://github.com/rodnover55/reaper-training/releases/latest):

   | Система | Файл | Папка `UserPlugins` |
   |---|---|---|
   | Linux (x86_64) | `reaper_training.so` | `~/.config/REAPER/UserPlugins/` |
   | macOS (Apple Silicon и Intel) | `reaper_training.dylib` | `~/Library/Application Support/REAPER/UserPlugins/` |

   Если REAPER стоит в портативном режиме, папка ресурсов — другая: её
   открывает пункт меню **Options → Show REAPER resource path in
   explorer/finder**.
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

Файл можно и собрать из исходников проекта (на Linux; на macOS 12 свежего
компилятора нет, и файл для Mac собирает CI). Нужны компилятор C++23,
CMake 3.29 и новее и Ninja; в папке проекта выполните:

```sh
cmake --preset default
cmake --build --preset default
cmake --install build
```

Последняя команда сама кладёт файл расширения в папку `UserPlugins`.

## Как проверить, что расширение загрузилось

При запуске REAPER расширение пишет строку в консоль REAPER (окно «ReaScript
console output»):

```
reaper-training 0.1.0 loaded (сборка 2026-10-03 21:37:12)
```

В строке — версия расширения, в скобках — дата и время сборки: по ним видно,
что загружено.

Если строки нет, проверьте, что файл лежит в папке `UserPlugins` именно той
папки ресурсов, которую открывает **Options → Show REAPER resource path in
explorer/finder**, а на macOS — что с него снята пометка карантина (шаг 4).

## Обновление

Закройте REAPER, замените файл расширения новым со страницы
[Releases](https://github.com/rodnover55/reaper-training/releases/latest) и
запустите REAPER снова. На macOS снова снимите пометку карантина (шаг 4).

## Удаление

Закройте REAPER и удалите файл расширения из папки `UserPlugins`
(`reaper_training.so` или `reaper_training.dylib`).
