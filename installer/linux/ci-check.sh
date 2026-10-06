#!/bin/sh
# Проверка установщика для Linux на чистой машине с настоящим REAPER (openspec
# add-installers, design D6). Идёт в CI в контейнере ubuntu:22.04 от root:
# сам заводит пользователя, от которого ставит REAPER и расширение.
#
# Использование: sh ci-check.sh <установщик .run> <reaper_training.so> \
#                  <reaper*_linux_x86_64.tar.xz>
#
# Меняет систему: заводит пользователя tester. Запускать только на
# одноразовой машине.

set -eu

# Копия установщика — там, откуда её прочитает пользователь tester.
run=/tmp/reaper-training-installer.run
cp "$1" "$run"
chmod 644 "$run"
module=$(readlink -f "$2")
reaper_tarball=$(readlink -f "$3")
user=tester

step() {
  echo
  echo "=== $1"
}

fail() {
  echo "::error::$1" >&2
  exit 1
}

# Выполняет команду $1 оболочкой от пользователя tester в его домашней папке.
as_user() {
  su - "$user" -s /bin/sh -c "$1"
}

step "От root установщик отказывается"
if sh "$run"; then
  fail "установщик от root отработал успешно"
fi
[ ! -e /root/.config/REAPER/UserPlugins ] || fail "от root что-то поставлено"

id "$user" >/dev/null 2>&1 || useradd -m "$user"
home=$(getent passwd "$user" | cut -d: -f6)

step "Без REAPER установщик отказывается"
if as_user "sh '$run'"; then
  fail "установщик без REAPER отработал успешно"
fi
[ ! -e "$home/.config/REAPER" ] || fail "без REAPER появилась папка ресурсов"

step "REAPER ставится в ~/opt своим сценарием"
src=$(mktemp -d)
tar -xJf "$reaper_tarball" -C "$src"
chmod -R a+rX "$src"
as_user "sh '$src/reaper_linux_x86_64/install-reaper.sh' --install ~/opt --quiet"
[ -x "$home/opt/REAPER/reaper" ] || fail "REAPER не поставился в ~/opt/REAPER"

step "Установка"
as_user "sh '$run'"
cmp "$home/.config/REAPER/UserPlugins/reaper_training.so" "$module" ||
  fail "поставленный модуль не совпадает с собранным"

step "Повторная установка"
as_user "sh '$run'"
[ "$(ls -A "$home/.config/REAPER/UserPlugins")" = reaper_training.so ] ||
  fail "в UserPlugins не один файл: $(ls -A "$home/.config/REAPER/UserPlugins")"

step "Портативный REAPER в ~/opt"
as_user "touch ~/opt/REAPER/reaper.ini && sh '$run'"
cmp "$home/opt/REAPER/UserPlugins/reaper_training.so" "$module" ||
  fail "в портативный REAPER модуль не поставлен"
as_user "rm -rf ~/opt/REAPER/reaper.ini ~/opt/REAPER/UserPlugins"

step "Папка ключом --resource-path"
as_user "mkdir -p ~/portable && touch ~/portable/reaper.ini && sh '$run' -- --resource-path ~/portable"
cmp "$home/portable/UserPlugins/reaper_training.so" "$module" ||
  fail "в папку из --resource-path модуль не поставлен"

step "Удаление"
as_user "touch ~/.config/REAPER/reaper.ini && sh '$run' -- --uninstall"
[ ! -e "$home/.config/REAPER/UserPlugins/reaper_training.so" ] ||
  fail "после удаления модуль на месте"
[ -f "$home/.config/REAPER/reaper.ini" ] || fail "удаление тронуло reaper.ini"

echo
echo "Установщик для Linux прошёл все проверки"
