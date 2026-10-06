#!/usr/bin/env bash
# Окружение для проверки модуля под Windows без Windows, на Ubuntu: Wine,
# компилятор MSVC через msvc-wine, шрифты Microsoft и портативный REAPER для
# Windows.
#
# Запуск: bash tools/setup-windows-env.sh
#
# Системные пакеты и русская локаль ставятся через sudo — пароль скрипт
# спросит сам. Всё остальное — в одну папку, по умолчанию
# ~/.local/share/reaper-training-windows; другую задаёт переменная
# TRAINING_WINDOWS_ENV. Скачивание MSVC просит принять лицензию Microsoft.
# Повторный запуск пропускает уже сделанное.
#
# Готовое окружение — файлы в той же папке:
#   env.sh    — `. env.sh`: WINEPREFIX, WINEDEBUG и cl, link, rc в PATH;
#   reaper.sh — REAPER под Wine с русской локалью: кодовая страница cp1251,
#               как у русской Windows.
#
# Папка ресурсов REAPER, куда ставится модуль (UserPlugins), —
# prefix/drive_c/users/<пользователь>/AppData/Roaming/REAPER. При каждом
# запуске REAPER показывает напоминание об ознакомительной лицензии; его
# закрывает кнопка Still Evaluating.

set -euo pipefail

root=${TRAINING_WINDOWS_ENV:-$HOME/.local/share/reaper-training-windows}
prefix=$root/prefix
msvc=$root/msvc
reaper_dir=$prefix/drive_c/REAPER
# Папка ресурсов REAPER — настройки, UserPlugins, Scripts. Под Wine это
# %APPDATA%\REAPER пользователя Wine, у которого то же имя, что в Linux.
resources=$prefix/drive_c/users/$USER/AppData/Roaming/REAPER

# REAPER — та же версия, что в CI (.github/workflows/build.yml).
reaper_file=reaper782_x64-install.exe
reaper_sha256=ae86dd8396673318a85275175dc8c1c9b0b8e090bf305f9cec920358e9d1e18f

# msvc-wine закреплён по коммиту: релизных тегов у него нет.
msvc_wine_commit=514f8ea34842cd6d831804d0e9658d3a32870ae1

export WINEPREFIX=$prefix
export WINEDEBUG=-all

# Печатает заголовок шага.
step() {
  printf '\n=== %s\n' "$1"
}

# Печатает ошибку в stderr и завершает скрипт с кодом 1.
fail() {
  printf 'Ошибка: %s\n' "$1" >&2
  exit 1
}

[ "$(id -u)" -ne 0 ] || fail "запускайте от своего пользователя: sudo скрипт вызовет сам"
mkdir -p "$root"

step "Системные пакеты"
# 7zip распаковывает установщик REAPER, cabextract — шрифты для winetricks,
# msitools — пакеты MSVC. winbind нужен компилятору MSVC под Wine для
# отладочной информации в отдельном файле (.pdb).
packages=(wine wine64 winetricks cabextract 7zip msitools winbind python3 git curl
  ca-certificates locales)
missing=()
for package in "${packages[@]}"; do
  if [ "$(dpkg-query -W -f='${Status}' "$package" 2>/dev/null)" != "install ok installed" ]; then
    missing+=("$package")
  fi
done
if ((${#missing[@]})); then
  sudo apt-get update
  sudo apt-get install -y "${missing[@]}"
else
  echo "уже стоят"
fi

step "Русская локаль"
if grep -qiE '^ru_RU\.utf-?8$' <<<"$(locale -a)"; then
  echo "уже есть"
else
  sudo locale-gen ru_RU.UTF-8
fi

# wineserver Ubuntu кладёт не в PATH, а к остальным файлам Wine; winetricks
# находит его по переменной WINESERVER.
WINESERVER=$(command -v wineserver || true)
if [ -z "$WINESERVER" ]; then
  for candidate in /usr/lib/*/wine/wineserver /usr/lib/wine/wineserver; do
    if [ -x "$candidate" ]; then
      WINESERVER=$candidate
      break
    fi
  done
fi
[ -n "$WINESERVER" ] || fail "не найден wineserver"
export WINESERVER

step "Префикс Wine: $prefix"
# Префикс — отдельный «компьютер» Wine: свой диск C: и реестр. ~/.wine не
# трогается.
if [ -f "$prefix/system.reg" ]; then
  echo "уже есть"
else
  # Без mscoree и mshtml Wine не предлагает окнами поставить Mono и Gecko:
  # ни то ни другое здесь не нужно.
  WINEARCH=win64 WINEDLLOVERRIDES="mscoree,mshtml=" wine wineboot --init
  "$WINESERVER" -w
fi

step "Шрифты Microsoft"
# Окно тренажёра рисует Arial; без него Wine подставит другой шрифт.
if [ -n "$(find "$prefix/drive_c/windows/Fonts" -maxdepth 1 -iname arial.ttf 2>/dev/null)" ]; then
  echo "уже стоят"
else
  winetricks -q corefonts
  "$WINESERVER" -w
fi

step "REAPER: $reaper_dir"
if [ -f "$reaper_dir/reaper.exe" ]; then
  echo "уже стоит"
else
  installer=$root/$reaper_file
  [ -f "$installer" ] || curl -fL -o "$installer" "https://www.reaper.fm/files/7.x/$reaper_file"
  if ! sha256sum -c --quiet <<<"$reaper_sha256  $installer"; then
    rm -f "$installer"
    fail "SHA-256 у $reaper_file не совпадает"
  fi

  # Установщик — 32-битная программа, а Wine из Ubuntu без пакета wine32
  # запускает только 64-битные. Поэтому установщик распаковывается: файлы
  # для папки ресурсов лежат в нём под $INSTDIR$_8_, служебные — под
  # $PLUGINSDIR и $COMMONFILES64.
  sevenzip=$(command -v 7z || command -v 7zz) || fail "не найден 7z"
  unpacked=$root/reaper.unpacked
  rm -rf "$unpacked"
  "$sevenzip" x -y -bso0 -bsp0 -o"$unpacked" "$installer"
  cp -a "$unpacked/\$INSTDIR\$_8_/." "$unpacked/"
  rm -rf "$unpacked/\$INSTDIR\$_8_" "$unpacked/\$PLUGINSDIR" "$unpacked/\$COMMONFILES64" \
    "$unpacked/Uninstall.exe.nsis"
  [ -f "$unpacked/reaper.exe" ] || fail "в установщике REAPER нет reaper.exe"
  mkdir -p "$(dirname "$reaper_dir")"
  mv "$unpacked" "$reaper_dir"
fi

step "Настройки REAPER: $resources"
# Звуковая система — Dummy Audio (mode=4): транспорт идёт без звуковой карты,
# и REAPER при запуске не спрашивает о звуковом устройстве. Выбранную раньше
# систему скрипт не трогает.
if grep -q '^\[audioconfig\]' "$resources/REAPER.ini" 2>/dev/null; then
  echo "звуковая система уже выбрана"
else
  mkdir -p "$resources"
  printf '[audioconfig]\nmode=4\n' >>"$resources/REAPER.ini"
fi
mkdir -p "$resources/UserPlugins"

step "MSVC: $msvc"
if [ -x "$msvc/bin/x64/cl" ]; then
  echo "уже стоит"
else
  src=$root/msvc-wine
  [ -d "$src/.git" ] || git clone https://github.com/mstorsjo/msvc-wine "$src"
  git -C "$src" checkout -q "$msvc_wine_commit"
  # Спрашивает, принимаете ли вы лицензию Microsoft. Скачивает и
  # распаковывает несколько гигабайт.
  (cd "$src" && python3 ./vsdownload.py --dest "$msvc" --architecture x64)
  (cd "$src" && ./install.sh "$msvc")
  [ -x "$msvc/bin/x64/cl" ] || fail "msvc-wine не поставил $msvc/bin/x64/cl"
fi

step "Файлы окружения"
{
  echo "# Окружение сборки и запуска под Wine: . $root/env.sh"
  printf 'export WINEPREFIX=%q\n' "$prefix"
  printf 'export WINESERVER=%q\n' "$WINESERVER"
  echo "export WINEDEBUG=-all"
  printf 'export PATH=%q:"$PATH"\n' "$msvc/bin/x64"
} >"$root/env.sh"

{
  echo "#!/bin/sh"
  echo "# REAPER для Windows под Wine с русской локалью: кодовая страница cp1251,"
  echo "# как у русской Windows. Аргументы передаются REAPER."
  printf 'export WINEPREFIX=%q\n' "$prefix"
  echo "export WINEDEBUG=-all"
  printf 'LANG=ru_RU.UTF-8 LC_ALL=ru_RU.UTF-8 exec wine %q "$@"\n' "$reaper_dir/reaper.exe"
} >"$root/reaper.sh"
chmod +x "$root/reaper.sh"
echo "$root/env.sh"
echo "$root/reaper.sh"

step "Проверка"
# Программа печатает кодовую страницу ANSI: ей функции Win32 без UNICODE
# читают текст. Под русской локалью должна быть 1251, как у русской Windows.
check=$root/check
rm -rf "$check"
mkdir -p "$check"
cat >"$check/acp.c" <<'EOF'
#include <stdio.h>
#include <windows.h>

int main(void) {
  printf("%u\n", GetACP());
  return 0;
}
EOF
(cd "$check" && PATH="$msvc/bin/x64:$PATH" cl /nologo acp.c /Feacp.exe >/dev/null) ||
  fail "MSVC не собрал проверочную программу"
acp=$(LANG=ru_RU.UTF-8 LC_ALL=ru_RU.UTF-8 wine "$check/acp.exe" | tr -d '\r')
[ "$acp" = 1251 ] || fail "под русской локалью Wine даёт кодовую страницу «$acp», а не 1251"
rm -rf "$check"
echo "MSVC собирает, Wine запускает, кодовая страница под русской локалью — 1251"

step "Готово"
echo "Окружение: $root"
