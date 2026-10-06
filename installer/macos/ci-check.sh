#!/bin/sh
# Проверка установщика для macOS на чистой машине с настоящим REAPER (openspec
# add-installers, design D6). Идёт в CI на раннере macOS от его пользователя.
#
# Пакет ставится командой `installer -target CurrentUserHomeDirectory` — тем
# же путём, что «Установить только для меня» в Installer, но без окон.
#
# Использование: sh ci-check.sh <установщик .pkg> <reaper_training.dylib> \
#                  <reaper*_universal.dmg>
#
# Меняет систему: кладёт REAPER в /Applications и в домашнюю папку. Запускать
# только на одноразовой машине.

set -eu

pkg=$1
module=$2
dmg=$3
resources="$HOME/Library/Application Support/REAPER"
plugin="$resources/UserPlugins/reaper_training.dylib"

step() {
  echo
  echo "=== $1"
}

fail() {
  echo "::error::$1" >&2
  exit 1
}

install_pkg() {
  installer -pkg "$pkg" -target CurrentUserHomeDirectory
}

# Кладёт в /Applications/REAPER.app пустой пакет приложения с идентификатором
# REAPER и версией $1 — для проверки версии без старого REAPER.
fake_reaper() {
  mkdir -p /Applications/REAPER.app/Contents
  cat >/Applications/REAPER.app/Contents/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>com.cockos.reaper</string>
  <key>CFBundleShortVersionString</key><string>$1</string>
  <key>CFBundlePackageType</key><string>APPL</string>
</dict>
</plist>
EOF
}

[ ! -e /Applications/REAPER.app ] || fail "на раннере уже есть REAPER"
[ ! -e "$resources" ] || fail "на раннере уже есть папка ресурсов REAPER"

step "Состав пакета"
expanded=$(mktemp -d)/pkg
pkgutil --expand-full "$pkg" "$expanded"
payload=$(cd "$expanded" && find . -path '*/Payload/*' -type f)
echo "$payload"
[ "$(echo "$payload" | sed 's|.*/||')" = reaper_training.dylib ] ||
  fail "в пакете не один модуль"
grep -q 'enable_currentUserHome="true"' "$expanded/Distribution" ||
  fail "пакет ставится не только в домашнюю папку"

step "Без REAPER установщик отказывается"
if install_pkg; then
  fail "пакет без REAPER поставился"
fi
[ ! -e "$plugin" ] || fail "без REAPER модуль поставлен"

step "С REAPER 6 установщик отказывается"
fake_reaper 6.83
if install_pkg; then
  fail "пакет с REAPER 6 поставился"
fi
[ ! -e "$plugin" ] || fail "с REAPER 6 модуль поставлен"
rm -rf /Applications/REAPER.app

step "REAPER ставится из образа в /Applications"
mnt=$(mktemp -d)
# В образе REAPER — лицензионное соглашение: hdiutil показывает его и ждёт
# согласия, без ответа подключение отменяется.
yes | PAGER=cat hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$dmg" >/dev/null
app=$(find "$mnt" -maxdepth 2 -name REAPER.app -type d | head -n 1)
[ -n "$app" ] || fail "в образе нет REAPER.app"
cp -R "$app" /Applications/
hdiutil detach "$mnt" >/dev/null
defaults read /Applications/REAPER.app/Contents/Info CFBundleShortVersionString

step "Установка пакета, скачанного из интернета"
# Такую пометку ставит браузер на скачанный файл.
xattr -w com.apple.quarantine "0083;$(printf %x "$(date +%s)");Safari;" "$pkg"
install_pkg
cmp "$plugin" "$module" || fail "поставленный модуль не совпадает с собранным"
if xattr -p com.apple.quarantine "$plugin" 2>/dev/null; then
  fail "у поставленного модуля пометка карантина"
fi
[ "$(stat -f %Su "$plugin")" = "$(id -un)" ] || fail "модуль принадлежит не пользователю"

step "Повторная установка"
install_pkg
[ "$(ls -A "$resources/UserPlugins")" = reaper_training.dylib ] ||
  fail "в UserPlugins не один файл: $(ls -A "$resources/UserPlugins")"

step "REAPER не в «Программах», но уже запускался"
mv /Applications/REAPER.app "$HOME/REAPER.app"
touch "$resources/reaper.ini"
rm "$plugin"
install_pkg
cmp "$plugin" "$module" || fail "по reaper.ini модуль не поставлен"

step "REAPER не в «Программах» и не запускался — отказ"
rm "$resources/reaper.ini" "$plugin"
if install_pkg; then
  fail "пакет без найденного REAPER поставился"
fi

echo
echo "Установщик для macOS прошёл все проверки"
