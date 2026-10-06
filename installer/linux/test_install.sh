#!/bin/sh
# Проверка installer/linux/install.sh без настоящего REAPER и без makeself.
#
# Каждый случай идёт в своей подставной домашней папке с PATH из одних нужных
# сценарию программ, поэтому ни REAPER, ни Flatpak машины, на которой идёт
# тест, на результат не влияют. Поддельный REAPER — файл с заголовком ELF
# нужной машины и whatsnew.txt нужной версии; поддельные id и flatpak —
# сценарии.
#
# Использование: sh test_install.sh <путь к install.sh>

set -u

script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

failures=0
current=

check() {
  if ! eval "$1"; then
    printf 'FAIL [%s]: %s\n' "$current" "$1" >&2
    failures=$((failures + 1))
  fi
}

# Программы, которые сценарию разрешено найти в PATH.
tools=$work/tools
mkdir -p "$tools"
for tool in sh cat cp mv mkdir rm chmod readlink dirname od tr head sed sleep; do
  ln -s "$(command -v "$tool")" "$tools/$tool"
done
cat >"$tools/uname" <<'EOF'
#!/bin/sh
echo x86_64
EOF
cat >"$tools/id" <<'EOF'
#!/bin/sh
echo "${FAKE_UID:-1000}"
EOF
chmod +x "$tools/uname" "$tools/id"

# Сборка, которую «ставит» сценарий: install.sh и модуль рядом, как в архиве.
stage=$work/stage
mkdir -p "$stage"
cp "$script" "$stage/install.sh"
echo "module v1" >"$stage/reaper_training.so"

# Заводит новый случай: свежая домашняя папка и свои системные пути.
start() {
  current=$1
  home=$work/cases/$1
  root=$home.root
  bin=$home.bin
  mkdir -p "$home" "$root" "$bin"
  out=$home.out
}

# Кладёт поддельный REAPER в папку $1: машина $2 (x86_64 или aarch64), версия
# $3 в whatsnew.txt (пусто — без whatsnew.txt).
fake_reaper() {
  mkdir -p "$1"
  case "$2" in
    x86_64) machine='\076\000' ;;
    aarch64) machine='\267\000' ;;
  esac
  printf "\\177ELF\\002\\001\\001\\000\\000\\000\\000\\000\\000\\000\\000\\000\\002\\000$machine" \
    >"$1/reaper"
  chmod +x "$1/reaper"
  [ -z "$3" ] || printf 'v%s - October 4 2026\n  + something\n' "$3" >"$1/whatsnew.txt"
}

# Кладёт поддельный flatpak с REAPER версии $1 для машины $2.
fake_flatpak() {
  cat >"$bin/flatpak" <<EOF
#!/bin/sh
[ "\$1" = info ] || exit 1
if [ "\$2" = -r ]; then
  [ "\$3" = fm.reaper.Reaper ] && echo "app/fm.reaper.Reaper/$2/stable" && exit 0
  exit 1
fi
[ "\$2" = fm.reaper.Reaper ] || exit 1
echo "REAPER - Digital Audio Workstation"
echo
echo "          ID: fm.reaper.Reaper"
echo "        Arch: $2"
echo "     Version: $1"
EOF
  chmod +x "$bin/flatpak"
}

# Запускает установщик в текущем случае с ключами $@; вывод — в $out, код —
# в $status.
run() {
  (cd "$work" && env -i HOME="$home" PATH="$bin:$tools" LANG="${RUN_LANG:-C}" \
    FAKE_UID="${FAKE_UID:-1000}" USER_PWD="$home" \
    TRAINING_INSTALL_SYSROOT="$root" sh "$stage/install.sh" "$@") >"$out" 2>&1
  status=$?
}

installed() {
  [ "$(cat "$1/UserPlugins/reaper_training.so" 2>/dev/null)" = "module v1" ]
}

start no_reaper
run
check '[ $status -ne 0 ]'
check 'grep -q "REAPER was not found" "$out"'
check '[ ! -e "$home/.config" ]'

start no_reaper_ru
RUN_LANG=ru_RU.UTF-8 run
check '[ $status -ne 0 ]'
check 'grep -q "REAPER не найден" "$out"'

start home_opt
fake_reaper "$home/opt/REAPER" x86_64 7.82
run
check '[ $status -eq 0 ]'
check 'installed "$home/.config/REAPER"'
check '[ ! -e "$home/opt/REAPER/UserPlugins" ]'
check '[ "$(ls -A "$home/.config/REAPER/UserPlugins")" = reaper_training.so ]'

start system_opt
fake_reaper "$root/opt/REAPER" x86_64 7.82
run
check '[ $status -eq 0 ]'
check 'installed "$home/.config/REAPER"'

start portable_found
fake_reaper "$home/opt/REAPER" x86_64 7.82
touch "$home/opt/REAPER/reaper.ini"
run
check '[ $status -eq 0 ]'
check 'installed "$home/opt/REAPER"'
check '[ ! -e "$home/.config" ]'

start legacy_resource_dir
fake_reaper "$home/opt/REAPER" x86_64 7.82
mkdir -p "$home/.REAPER" && touch "$home/.REAPER/reaper.ini"
run
check '[ $status -eq 0 ]'
check 'installed "$home/.REAPER"'

start path_symlink
fake_reaper "$home/apps/REAPER" x86_64 7.82
touch "$home/apps/REAPER/reaper.ini"
ln -s "$home/apps/REAPER/reaper" "$bin/reaper"
run
check '[ $status -eq 0 ]'
check 'installed "$home/apps/REAPER"'

start desktop_entry
fake_reaper "$home/apps/R" x86_64 7.82
mkdir -p "$home/.local/share/applications"
printf '[Desktop Entry]\nName=REAPER\nExec="%s" %%F\n' "$home/apps/R/reaper" \
  >"$home/.local/share/applications/cockos-reaper.desktop"
run
check '[ $status -eq 0 ]'
check 'installed "$home/.config/REAPER"'

start system_desktop_entry_unquoted
fake_reaper "$home/apps/R" x86_64 7.82
mkdir -p "$root/usr/share/applications"
printf '[Desktop Entry]\nExec=%s %%F\n' "$home/apps/R/reaper" \
  >"$root/usr/share/applications/cockos-reaper.desktop"
run
check '[ $status -eq 0 ]'
check 'installed "$home/.config/REAPER"'

start reaper6
fake_reaper "$home/opt/REAPER" x86_64 6.83
mkdir -p "$home/.config/REAPER" && touch "$home/.config/REAPER/reaper.ini"
run
check '[ $status -ne 0 ]'
check 'grep -q "REAPER 7 or newer" "$out"'
check '[ ! -e "$home/.config/REAPER/UserPlugins" ]'

start arm
fake_reaper "$home/opt/REAPER" aarch64 7.82
run
check '[ $status -ne 0 ]'
check 'grep -q "not an x86_64 build" "$out"'
check '[ ! -e "$home/.config" ]'

start unknown_version
fake_reaper "$home/opt/REAPER" x86_64 ""
run
check '[ $status -eq 0 ]'
check 'installed "$home/.config/REAPER"'

start flatpak
fake_flatpak 7.82 x86_64
run
check '[ $status -eq 0 ]'
check 'installed "$home/.var/app/fm.reaper.Reaper/config/REAPER"'
check '[ ! -e "$home/.config" ]'

start flatpak_and_native
fake_flatpak 7.82 x86_64
fake_reaper "$home/opt/REAPER" x86_64 7.82
run
check '[ $status -eq 0 ]'
check 'installed "$home/.var/app/fm.reaper.Reaper/config/REAPER"'
check 'installed "$home/.config/REAPER"'

start flatpak_old
fake_flatpak 6.83 x86_64
run
check '[ $status -ne 0 ]'
check '[ ! -e "$home/.var" ]'

start resource_dir_only
mkdir -p "$home/.config/REAPER" && touch "$home/.config/REAPER/reaper.ini"
run
check '[ $status -eq 0 ]'
check 'installed "$home/.config/REAPER"'

start explicit
mkdir -p "$home/portable" && touch "$home/portable/reaper.ini"
run --resource-path "$home/portable"
check '[ $status -eq 0 ]'
check 'installed "$home/portable"'

start explicit_relative
mkdir -p "$home/portable" && touch "$home/portable/reaper.ini"
fake_reaper "$home/opt/REAPER" x86_64 7.82
run --resource-path=portable
check '[ $status -eq 0 ]'
check 'installed "$home/portable"'
check '[ ! -e "$home/.config" ]'

start explicit_without_ini
mkdir -p "$home/portable"
run --resource-path "$home/portable"
check '[ $status -ne 0 ]'
check 'grep -q "not a REAPER folder" "$out"'
check '[ ! -e "$home/portable/UserPlugins" ]'

start explicit_old_reaper
fake_reaper "$home/portable" x86_64 6.83
touch "$home/portable/reaper.ini"
run --resource-path "$home/portable"
check '[ $status -ne 0 ]'
check '[ ! -e "$home/portable/UserPlugins" ]'

start update
fake_reaper "$home/opt/REAPER" x86_64 7.82
mkdir -p "$home/.config/REAPER/UserPlugins"
echo "module v0" >"$home/.config/REAPER/UserPlugins/reaper_training.so"
echo "other" >"$home/.config/REAPER/UserPlugins/reaper_other.so"
run
check '[ $status -eq 0 ]'
check 'installed "$home/.config/REAPER"'
check '[ "$(ls -A "$home/.config/REAPER/UserPlugins" | tr "\n" " ")" = "reaper_other.so reaper_training.so " ]'

start uninstall
fake_reaper "$home/opt/REAPER" x86_64 7.82
run
mkdir -p "$home/.config/REAPER" && touch "$home/.config/REAPER/reaper.ini"
echo "other" >"$home/.config/REAPER/UserPlugins/reaper_other.so"
run --uninstall
check '[ $status -eq 0 ]'
check 'grep -q "Removed: .*reaper_training.so" "$out"'
check '[ ! -e "$home/.config/REAPER/UserPlugins/reaper_training.so" ]'
check '[ -f "$home/.config/REAPER/reaper.ini" ]'
check '[ -f "$home/.config/REAPER/UserPlugins/reaper_other.so" ]'

start uninstall_without_reaper
mkdir -p "$home/.var/app/fm.reaper.Reaper/config/REAPER/UserPlugins"
echo "module v1" >"$home/.var/app/fm.reaper.Reaper/config/REAPER/UserPlugins/reaper_training.so"
run --uninstall
check '[ $status -eq 0 ]'
check '[ ! -e "$home/.var/app/fm.reaper.Reaper/config/REAPER/UserPlugins/reaper_training.so" ]'

start uninstall_nothing
run --uninstall
check '[ $status -eq 0 ]'
check 'grep -q "nothing to remove" "$out"'

start root
fake_reaper "$home/opt/REAPER" x86_64 7.82
FAKE_UID=0 run
check '[ $status -ne 0 ]'
check 'grep -q "without sudo" "$out"'
check '[ ! -e "$home/.config" ]'

start unknown_option
run --frobnicate
check '[ $status -ne 0 ]'
check 'grep -q "Unknown option" "$out"'

# Процесс с именем reaper — сценарий: ядро называет процесс по имени файла
# сценария. Копия sleep не годится: в coreutils одним файлом программа берёт
# своё имя из пути и под чужим не работает.
start reaper_running
fake_reaper "$home/opt/REAPER" x86_64 7.82
mkdir -p "$home/running"
printf '#!/bin/sh\nwhile :; do sleep 1; done\n' >"$home/running/reaper"
chmod +x "$home/running/reaper"
"$home/running/reaper" &
sleeper=$!
run
kill "$sleeper" 2>/dev/null
check '[ $status -eq 0 ]'
check 'grep -q "REAPER is running" "$out"'
check 'installed "$home/.config/REAPER"'

if [ "$failures" -ne 0 ]; then
  printf '%d проверок не прошли\n' "$failures" >&2
  exit 1
fi
echo "Все случаи прошли"
