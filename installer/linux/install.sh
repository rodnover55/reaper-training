#!/bin/sh
# Установщик reaper-training для Linux (openspec add-installers, design D4).
#
# Запускается из архива .run, который собирает makeself: архив распаковывается
# во временную папку, и этот сценарий находит рядом с собой модуль. Пользователь
# передаёт ключи после `--`:
#
#   sh reaper-training-<версия>-linux-x86_64.run -- --uninstall
#
# Всё ставится в домашнюю папку, root не нужен и не принимается.
#
# TRAINING_INSTALL_SYSROOT подставляется перед системными путями (/opt,
# /usr/share) — только для тестов, чтобы они не зависели от машины.

set -u

plugin=reaper_training.so
flatpak_id=fm.reaper.Reaper
sysroot=${TRAINING_INSTALL_SYSROOT:-}
here=$(cd "$(dirname "$0")" && pwd)

case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in
  ru*) lang=ru ;;
  *) lang=en ;;
esac

nl='
'

# Печатает первый аргумент при русском языке системы, второй — при любом
# другом. Выводит в stdout с переводом строки.
say() {
  if [ "$lang" = ru ]; then
    printf '%s\n' "$1"
  else
    printf '%s\n' "$2"
  fi
}

# Печатает сообщение, как say, в stderr и завершает сценарий с кодом 1.
fail() {
  say "$1" "$2" >&2
  exit 1
}

usage() {
  say "Установка reaper-training — расширения REAPER.

  sh <установщик>.run                       поставить или обновить
  sh <установщик>.run -- --uninstall        удалить
  sh <установщик>.run -- --resource-path ПАПКА
                                            поставить в портативный REAPER:
                                            ПАПКА — та, где лежит reaper.ini

Установщик сам находит REAPER: reaper в PATH, /opt/REAPER, ~/opt/REAPER,
ярлык в меню, REAPER из Flatpak." \
"Installs reaper-training, an extension for REAPER.

  sh <installer>.run                        install or update
  sh <installer>.run -- --uninstall         uninstall
  sh <installer>.run -- --resource-path DIR
                                            install into a portable REAPER:
                                            DIR is the folder with reaper.ini

The installer finds REAPER by itself: reaper in PATH, /opt/REAPER,
~/opt/REAPER, the menu entry, REAPER from Flatpak."
}

action=install
explicit=

while [ $# -gt 0 ]; do
  case "$1" in
    --uninstall) action=uninstall ;;
    --resource-path)
      [ $# -ge 2 ] || fail "Ключу --resource-path нужна папка." \
        "--resource-path needs a folder."
      explicit=$2
      shift
      ;;
    --resource-path=*) explicit=${1#*=} ;;
    -h | --help)
      usage
      exit 0
      ;;
    *) fail "Неизвестный ключ: $1. Список ключей: --help." \
      "Unknown option: $1. See --help." ;;
  esac
  shift
done

# Расширение ставится в папку пользователя, который запустил установщик. От
# root оно попало бы в /root или, при `sudo` без -H, в чужую папку с владельцем
# root.
if [ "$(id -u)" = 0 ]; then
  fail "Запустите установщик от своего пользователя, без sudo: расширение ставится в домашнюю папку, права root не нужны." \
    "Run the installer as your own user, without sudo: the extension goes to your home folder and needs no root rights."
fi

case "$(uname -m)" in
  x86_64 | amd64) ;;
  *) fail "Расширение собрано для x86_64, а эта система — $(uname -m)." \
    "The extension is built for x86_64, and this system is $(uname -m)." ;;
esac

# Папки ресурсов, по одной на строку, без повторов.
targets=
# Непусто, если найден REAPER, которому расширение не подходит.
rejected=

# Добавляет папку ресурсов в targets, если её там ещё нет.
add_target() {
  case "$nl$targets" in
    *"$nl$1$nl"*) ;;
    *) targets="$targets$1$nl" ;;
  esac
}

# Печатает папку ресурсов REAPER, которая не портативная, — по тем же
# правилам, что install-reaper.sh из поставки REAPER: ~/.config/REAPER, а
# старая ~/.REAPER — только если в ней есть reaper.ini, а в ~/.config/REAPER
# нет.
standard_resource_dir() {
  if [ ! -f "$HOME/.config/REAPER/reaper.ini" ] &&
    [ -f "$HOME/.REAPER/reaper.ini" ]; then
    printf '%s\n' "$HOME/.REAPER"
  else
    printf '%s\n' "$HOME/.config/REAPER"
  fi
}

# Печатает старшую цифру версии REAPER из первой строки whatsnew.txt в папке
# $1 («v7.82 - October 4 2026» → 7). Если файла нет или строка другая, не
# печатает ничего.
reaper_major() {
  head -n 1 "$1/whatsnew.txt" 2>/dev/null | sed -n 's/^v\([0-9][0-9]*\)\..*/\1/p'
}

# Проверяет программу REAPER по пути $1 и добавляет её папку ресурсов в
# targets. Ссылки раскрываются. Программу не для x86_64 или версии ниже 7
# пропускает с сообщением и отмечает в rejected. Несуществующий путь
# пропускает молча.
consider_program() {
  [ -f "$1" ] || return 0
  exe=$(readlink -f "$1")
  dir=$(dirname "$exe")

  # e_machine в заголовке ELF — два байта со смещения 18; у x86-64 это 0x3e.
  machine=$(od -An -tx1 -j18 -N2 "$exe" 2>/dev/null | tr -d ' \n')
  if [ "$machine" != 3e00 ]; then
    say "REAPER в $dir — не для x86_64, пропущен." \
      "REAPER in $dir is not an x86_64 build, skipped."
    rejected=1
    return 0
  fi

  major=$(reaper_major "$dir")
  if [ -n "$major" ] && [ "$major" -lt 7 ]; then
    say "REAPER в $dir — версии $major, а нужен REAPER 7 или новее. Пропущен." \
      "REAPER in $dir is version $major, and REAPER 7 or newer is needed. Skipped."
    rejected=1
    return 0
  fi

  if [ -f "$dir/reaper.ini" ]; then
    add_target "$dir"
  else
    add_target "$(standard_resource_dir)"
  fi
}

# Печатает путь к программе из строки Exec ярлыка $1 — в кавычках или без.
desktop_exec() {
  sed -n -e 's/^Exec="\([^"]*\)".*/\1/p' -e 's/^Exec=\([^" ][^ ]*\).*/\1/p' "$1" |
    head -n 1
}

# Добавляет в targets папку ресурсов REAPER из Flatpak, если он стоит и
# подходит.
consider_flatpak() {
  command -v flatpak >/dev/null 2>&1 || return 0
  LC_ALL=C flatpak info "$flatpak_id" >/dev/null 2>&1 || return 0

  case "$(LC_ALL=C flatpak info -r "$flatpak_id" 2>/dev/null)" in
    */x86_64/*) ;;
    *)
      say "REAPER из Flatpak — не для x86_64, пропущен." \
        "REAPER from Flatpak is not an x86_64 build, skipped."
      rejected=1
      return 0
      ;;
  esac

  major=$(LC_ALL=C flatpak info "$flatpak_id" 2>/dev/null |
    sed -n 's/^ *Version: *\([0-9][0-9]*\)\..*/\1/p' | head -n 1)
  if [ -n "$major" ] && [ "$major" -lt 7 ]; then
    say "REAPER из Flatpak — версии $major, а нужен REAPER 7 или новее. Пропущен." \
      "REAPER from Flatpak is version $major, and REAPER 7 or newer is needed. Skipped."
    rejected=1
    return 0
  fi

  add_target "$HOME/.var/app/$flatpak_id/config/REAPER"
}

# Заполняет targets папками ресурсов всех найденных REAPER, которым подходит
# расширение.
find_reaper() {
  if path=$(command -v reaper 2>/dev/null); then
    consider_program "$path"
  fi
  consider_program "$sysroot/opt/REAPER/reaper"
  consider_program "$HOME/opt/REAPER/reaper"

  for apps in "${XDG_DATA_HOME:-$HOME/.local/share}/applications" \
    "$sysroot/usr/local/share/applications" "$sysroot/usr/share/applications"; do
    [ -f "$apps/cockos-reaper.desktop" ] || continue
    exe=$(desktop_exec "$apps/cockos-reaper.desktop")
    [ -n "$exe" ] && consider_program "$exe"
  done

  consider_flatpak

  # REAPER, распакованный куда-то без установки, найти негде, но если он уже
  # запускался, его папка ресурсов на месте.
  if [ -z "$targets" ] && [ -z "$rejected" ]; then
    standard=$(standard_resource_dir)
    [ -f "$standard/reaper.ini" ] && add_target "$standard"
  fi
}

# Проверяет папку, переданную --resource-path, и делает её единственной в
# targets. Относительный путь считается от папки, откуда запущен установщик.
use_explicit() {
  case "$explicit" in
    /*) dir=$explicit ;;
    *) dir="${USER_PWD:-$PWD}/$explicit" ;;
  esac
  [ -f "$dir/reaper.ini" ] ||
    fail "$explicit — не папка REAPER: в ней нет reaper.ini." \
      "$explicit is not a REAPER folder: there is no reaper.ini in it."

  if [ "$action" = install ] && [ -f "$dir/reaper" ]; then
    consider_program "$dir/reaper"
    [ -z "$rejected" ] || fail "Ничего не поставлено." "Nothing was installed."
  fi
  dir=$(cd "$dir" && pwd)
  targets="$dir$nl"
}

# Проверяет, запущен ли REAPER у кого-нибудь в системе.
reaper_running() {
  for comm in /proc/[0-9]*/comm; do
    [ "$(cat "$comm" 2>/dev/null)" = reaper ] && return 0
  done
  return 1
}

# Кладёт модуль в UserPlugins папки ресурсов $1, создавая папки. Файл прошлой
# версии заменяется переименованием: запущенный REAPER держит старый файл
# открытым и работает дальше, а запись поверх загруженного модуля его бы
# уронила.
install_into() {
  dest="$1/UserPlugins"
  tmp="$dest/.$plugin.$$"
  if mkdir -p "$dest" && cp "$here/$plugin" "$tmp" && chmod 644 "$tmp" &&
    mv -f "$tmp" "$dest/$plugin"; then
    say "Поставлено: $dest/$plugin" "Installed: $dest/$plugin"
  else
    rm -f "$tmp"
    fail "Не удалось записать $dest/$plugin." "Could not write $dest/$plugin."
  fi
}

if [ -n "$explicit" ]; then
  use_explicit
else
  find_reaper
fi

if [ "$action" = uninstall ]; then
  # Удаление ищет и там, куда ставит установка, и в обычных папках: REAPER
  # могли удалить раньше расширения.
  if [ -z "$explicit" ]; then
    add_target "$(standard_resource_dir)"
    add_target "$HOME/.var/app/$flatpak_id/config/REAPER"
  fi
  removed=
  while IFS= read -r dir; do
    [ -n "$dir" ] && [ -f "$dir/UserPlugins/$plugin" ] || continue
    rm -f "$dir/UserPlugins/$plugin" ||
      fail "Не удалось удалить $dir/UserPlugins/$plugin." \
        "Could not remove $dir/UserPlugins/$plugin."
    say "Удалено: $dir/UserPlugins/$plugin" "Removed: $dir/UserPlugins/$plugin"
    removed=1
  done <<EOF
$targets
EOF
  [ -n "$removed" ] || say "Расширение не найдено — удалять нечего." \
    "The extension was not found, nothing to remove."
  exit 0
fi

if [ -z "$targets" ]; then
  if [ -n "$rejected" ]; then
    fail "Подходящего REAPER нет: нужен REAPER 7 или новее для x86_64. Ничего не поставлено." \
      "No suitable REAPER: REAPER 7 or newer for x86_64 is needed. Nothing was installed."
  fi
  fail "REAPER не найден. Поставьте REAPER 7 и запустите его хотя бы раз, затем запустите установщик снова.
Если REAPER портативный, укажите его папку — ту, где лежит reaper.ini:
  sh <установщик>.run -- --resource-path <папка REAPER>" \
    "REAPER was not found. Install REAPER 7 and run it once, then run the installer again.
If your REAPER is portable, pass its folder, the one with reaper.ini:
  sh <installer>.run -- --resource-path <REAPER folder>"
fi

while IFS= read -r dir; do
  [ -n "$dir" ] && install_into "$dir"
done <<EOF
$targets
EOF

if reaper_running; then
  say "REAPER запущен: перезапустите его, чтобы загрузилось расширение." \
    "REAPER is running: restart it to load the extension."
else
  say "Запустите REAPER: в консоли REAPER появится строка «reaper-training … loaded»." \
    "Start REAPER: its console shows the line \"reaper-training ... loaded\"."
fi
