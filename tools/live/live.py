#!/usr/bin/env python3
"""Проверка тренажёра вживую: REAPER на виртуальном дисплее Xvfb, чёрным ящиком.

REAPER работает в отдельной папке ресурсов build/live (`reaper -cfgfile`):
модуль из сборки, свои настройки тренажёра и журнал build/live/reaper-training.log.
Папка ресурсов пользователя не трогается. Окно тренажёра стоит в плавающем доке
1400×800 в левом верхнем углу экрана — отдельным окном X: в доке главного окна
своего окна X у него нет, и ни щёлкнуть в него через XTEST, ни снять его нельзя.
Главное окно REAPER — справа от него.

Команды:
  start [--module ФАЙЛ] [--keep]  запустить Xvfb :99 и REAPER на нём
  run СКРИПТ.lua [АРГУМЕНТ…]      выполнить скрипт Lua в REAPER и напечатать,
                                  что он вернул
  click КОНТРОЛ [--item N --of M] щёлкнуть мышью в контрол окна тренажёра
  type КОНТРОЛ ТЕКСТ              набрать текст в поле окна тренажёра вместо
                                  прежнего
  key КЛАВИША…                    нажать клавиши в окне тренажёра: BackSpace,
                                  Tab, End, ctrl+a
  hover КОНТРОЛ                   навести мышь на контрол окна тренажёра без
                                  щелчка
  shot ФАЙЛ.png [--untitled]      снять окно тренажёра или окно без заголовка —
                                  подсказку у курсора
  windows                         перечислить окна верхнего уровня
  quit                            выйти из REAPER без вопросов о сохранении
                                  и остановить Xvfb

Нужны Xvfb, библиотеки X11 и Xtst и REAPER для Linux. Где REAPER, задаёт
переменная TRAINING_REAPER, по умолчанию ~/opt/REAPER/reaper. Звуковую систему
и лицензию REAPER инструмент берёт из папки ресурсов пользователя
~/.config/REAPER: без лицензии REAPER при запуске показывает напоминание.
"""

import argparse
import ctypes
import ctypes.util
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIVE = ROOT / "build" / "live"
INI = LIVE / "reaper.ini"
JOURNAL = LIVE / "reaper-training.log"
FORMS = ROOT / "src" / "views" / "forms.rc"
USER = Path.home() / ".config" / "REAPER"
REAPER = Path(os.environ.get("TRAINING_REAPER", Path.home() / "opt" / "REAPER" / "reaper"))
DISPLAY = ":99"

# Окно тренажёра в плавающем доке: заголовок и место. Главное окно REAPER
# стоит справа от него, а не под ним: запуск транспорта поднимает главное окно
# наверх, и окно тренажёра под ним не получало бы щелчков, а снимок был бы
# чужим.
TRAINER = "Timing trainer"
DOCK = {"left": 0, "top": 0, "right": 1400, "bottom": 800}
MAIN = {"x": 1400, "y": 0, "w": 1920, "h": 1200}
SCREEN = f"{MAIN['x'] + MAIN['w']}x{MAIN['h']}x24"

# Масштаб диалогов SWELL на Linux: пикселей на диалоговую единицу
# (SWELL_DEF_DLGSCALE2 в WDL/swell/swell-dlggen.h).
SCALE = 1.9

# Высота строки выпадающего списка в диалоговых единицах: в forms.rc у списка
# записана высота раскрытого списка.
COMBO_HEIGHT = 12

# Имена клавиш X для знаков, которые набирает `type`, кроме букв и цифр.
KEY_NAMES = {"-": "minus", "+": "plus", ".": "period", ",": "comma", " ": "space",
             "=": "equal", "_": "underscore"}


def fail(message):
    sys.exit("live: " + message)


def environment():
    env = dict(os.environ, DISPLAY=DISPLAY, GDK_BACKEND="x11")
    env.pop("WAYLAND_DISPLAY", None)
    return env


# --- Процессы -----------------------------------------------------------------


def reapers():
    """Номера процессов REAPER. Зависшие копии REAPER после fork — один поток —
    не в счёт."""
    found = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if (entry / "comm").read_text().strip() != "reaper":
                continue
            status = (entry / "status").read_text()
        except OSError:
            continue
        threads = re.search(r"^Threads:\s*(\d+)", status, re.M)
        if threads and int(threads.group(1)) > 1:
            found.append(int(entry.name))
    return found


def wait(condition, seconds, step=0.2):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(step)
    return condition()


# --- Xlib ---------------------------------------------------------------------


class Attributes(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int), ("width", ctypes.c_int),
                ("height", ctypes.c_int), ("border_width", ctypes.c_int), ("depth", ctypes.c_int),
                ("visual", ctypes.c_void_p), ("root", ctypes.c_ulong), ("class_", ctypes.c_int),
                ("bit_gravity", ctypes.c_int), ("win_gravity", ctypes.c_int),
                ("backing_store", ctypes.c_int), ("backing_planes", ctypes.c_ulong),
                ("backing_pixel", ctypes.c_ulong), ("save_under", ctypes.c_int),
                ("colormap", ctypes.c_ulong), ("map_installed", ctypes.c_int),
                ("map_state", ctypes.c_int), ("all_event_masks", ctypes.c_long),
                ("your_event_mask", ctypes.c_long), ("do_not_propagate_mask", ctypes.c_long),
                ("override_redirect", ctypes.c_int), ("screen", ctypes.c_void_p)]


class Image(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int), ("xoffset", ctypes.c_int),
                ("format", ctypes.c_int), ("data", ctypes.POINTER(ctypes.c_ubyte)),
                ("byte_order", ctypes.c_int), ("bitmap_unit", ctypes.c_int),
                ("bitmap_bit_order", ctypes.c_int), ("bitmap_pad", ctypes.c_int),
                ("depth", ctypes.c_int), ("bytes_per_line", ctypes.c_int),
                ("bits_per_pixel", ctypes.c_int)]


IS_VIEWABLE = 2
Z_PIXMAP = 2
REVERT_TO_PARENT = 2
CURRENT_TIME = 0

# Клавиши X модификаторов для `Display.key`.
MODIFIERS = {"ctrl": "Control_L", "shift": "Shift_L", "alt": "Alt_L"}


class Display:
    """Дисплей Xvfb: окна верхнего уровня, снимки и поддельная мышь XTEST."""

    def __init__(self):
        x11 = ctypes.util.find_library("X11")
        xtst = ctypes.util.find_library("Xtst")
        if not x11 or not xtst:
            fail("нет библиотек X11 и Xtst")
        self.x = ctypes.cdll.LoadLibrary(x11)
        self.t = ctypes.cdll.LoadLibrary(xtst)
        x, t = self.x, self.t
        x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x.XOpenDisplay.restype = ctypes.c_void_p
        x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x.XDefaultRootWindow.restype = ctypes.c_ulong
        x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                 ctypes.POINTER(ctypes.c_ulong),
                                 ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                                 ctypes.POINTER(ctypes.c_uint)]
        x.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
        x.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                           ctypes.POINTER(Attributes)]
        x.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int,
                                ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
        x.XGetImage.restype = ctypes.POINTER(Image)
        x.XTranslateCoordinates.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
                                            ctypes.c_int, ctypes.c_int,
                                            ctypes.POINTER(ctypes.c_int),
                                            ctypes.POINTER(ctypes.c_int),
                                            ctypes.POINTER(ctypes.c_ulong)]
        x.XFlush.argtypes = [ctypes.c_void_p]
        t.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                           ctypes.c_int, ctypes.c_ulong]
        t.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
                                           ctypes.c_ulong]
        t.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
                                        ctypes.c_ulong]
        x.XStringToKeysym.argtypes = [ctypes.c_char_p]
        x.XStringToKeysym.restype = ctypes.c_ulong
        x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        x.XKeysymToKeycode.restype = ctypes.c_ubyte
        x.XKeycodeToKeysym.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_int]
        x.XKeycodeToKeysym.restype = ctypes.c_ulong
        x.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int,
                                     ctypes.c_ulong]
        x.XRaiseWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.display = x.XOpenDisplay(DISPLAY.encode())
        if not self.display:
            fail(f"нет дисплея {DISPLAY}: Xvfb не запущен")
        self.root = x.XDefaultRootWindow(self.display)

    def attributes(self, window):
        attributes = Attributes()
        self.x.XGetWindowAttributes(self.display, window, ctypes.byref(attributes))
        return attributes

    def windows(self):
        """Видимые окна верхнего уровня: (окно, заголовок, x, y, ширина, высота)."""
        root, parent = ctypes.c_ulong(), ctypes.c_ulong()
        children, count = ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
        self.x.XQueryTree(self.display, self.root, ctypes.byref(root), ctypes.byref(parent),
                          ctypes.byref(children), ctypes.byref(count))
        found = []
        for i in range(count.value):
            window = children[i]
            attributes = self.attributes(window)
            if attributes.map_state != IS_VIEWABLE:
                continue
            name = ctypes.c_char_p()
            self.x.XFetchName(self.display, window, ctypes.byref(name))
            title = name.value.decode("utf-8", "replace") if name.value else ""
            found.append((window, title, attributes.x, attributes.y, attributes.width,
                          attributes.height))
        return found

    def find(self, title):
        for window, name, *_ in self.windows():
            if title in name:
                return window
        return None

    def click(self, window, x, y):
        """Щелчок левой кнопкой в точке (x, y) окна."""
        at_x, at_y, child = ctypes.c_int(), ctypes.c_int(), ctypes.c_ulong()
        self.x.XTranslateCoordinates(self.display, window, self.root, x, y, ctypes.byref(at_x),
                                     ctypes.byref(at_y), ctypes.byref(child))
        self.t.XTestFakeMotionEvent(self.display, -1, at_x.value, at_y.value, 0)
        self.x.XFlush(self.display)
        time.sleep(0.1)
        self.t.XTestFakeButtonEvent(self.display, 1, 1, 0)
        self.x.XFlush(self.display)
        time.sleep(0.05)
        self.t.XTestFakeButtonEvent(self.display, 1, 0, 0)
        self.x.XFlush(self.display)
        time.sleep(0.2)

    def move(self, window, x, y):
        """Мышь в точку (x, y) окна, без щелчка."""
        at_x, at_y, child = ctypes.c_int(), ctypes.c_int(), ctypes.c_ulong()
        self.x.XTranslateCoordinates(self.display, window, self.root, x, y, ctypes.byref(at_x),
                                     ctypes.byref(at_y), ctypes.byref(child))
        self.t.XTestFakeMotionEvent(self.display, -1, at_x.value, at_y.value, 0)
        self.x.XFlush(self.display)
        time.sleep(0.3)

    def focus(self, window):
        """Фокус ввода X — окну: без оконного менеджера он остаётся у главного
        окна REAPER, и клавиши туда и уходят."""
        self.x.XSetInputFocus(self.display, window, REVERT_TO_PARENT, CURRENT_TIME)
        self.x.XFlush(self.display)
        time.sleep(0.2)

    def raise_(self, window):
        """Поднимает окно над остальными и ждёт, пока его перерисуют: без
        оконного менеджера подсказка «поверх всех» остаётся под окном, которое
        подняли после неё, и снимок вышел бы чёрным."""
        self.x.XRaiseWindow(self.display, window)
        self.x.XFlush(self.display)
        time.sleep(0.5)

    def key(self, name):
        """Нажимает и отпускает клавишу `name` — имя клавиши X («BackSpace», «a»,
        «minus») с модификаторами через «+»: «ctrl+a», «shift+Tab». Знак, который
        стоит на клавише со Shift, нажимается со Shift."""
        *modifiers, main = name.split("+") if name != "+" else ["plus"]
        keysym = self.x.XStringToKeysym(main.encode())
        code = self.x.XKeysymToKeycode(self.display, keysym) if keysym else 0
        if not code:
            fail(f"нет клавиши {main}")
        if self.x.XKeycodeToKeysym(self.display, code, 0) != keysym:
            modifiers.append("shift")
        held = []
        for modifier in modifiers:
            held.append(self.x.XKeysymToKeycode(
                self.display, self.x.XStringToKeysym(MODIFIERS[modifier.lower()].encode())))
        for code_held in held:
            self.t.XTestFakeKeyEvent(self.display, code_held, 1, 0)
        self.t.XTestFakeKeyEvent(self.display, code, 1, 0)
        self.x.XFlush(self.display)
        time.sleep(0.03)
        self.t.XTestFakeKeyEvent(self.display, code, 0, 0)
        for code_held in reversed(held):
            self.t.XTestFakeKeyEvent(self.display, code_held, 0, 0)
        self.x.XFlush(self.display)
        time.sleep(0.12)

    def shot(self, window, path):
        """Кадр окна в PNG."""
        attributes = self.attributes(window)
        image = self.x.XGetImage(self.display, window, 0, 0, attributes.width,
                                 attributes.height, 0xFFFFFFFF, Z_PIXMAP)
        if not image:
            fail("XGetImage не отдал кадр")
        frame = image.contents
        rows = []
        for y in range(frame.height):
            line = ctypes.string_at(ctypes.addressof(frame.data.contents) + y * frame.bytes_per_line,
                                    frame.width * 4)
            row = bytearray(frame.width * 3)
            row[0::3], row[1::3], row[2::3] = line[2::4], line[1::4], line[0::4]
            rows.append(b"\x00" + bytes(row))

        def chunk(kind, data):
            body = kind + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

        header = struct.pack(">IIBBBBB", frame.width, frame.height, 8, 2, 0, 0, 0)
        Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                               + chunk(b"IDAT", zlib.compress(b"".join(rows), 6))
                               + chunk(b"IEND", b""))
        return frame.width, frame.height


# --- Команды ------------------------------------------------------------------


def config():
    """reaper.ini отдельной папки ресурсов: звук — как у пользователя, без
    проверки новой версии, окно тренажёра — в плавающем доке 15, главное окно —
    справа от него."""
    audio = []
    user = USER / "reaper.ini"
    if user.exists():
        audio = [line for line in user.read_text(errors="replace").splitlines()
                 if re.match(r"(alsa_|linux_audio_)", line)]
    else:
        print(f"live: нет {user} — звуковая система REAPER по умолчанию", file=sys.stderr)
    window = [f"wnd_{key}={value}" for key, value in MAIN.items()]
    lines = ["[reaper]", *audio, "verchk=0", *window,
             "dockermode15=32768", *[f"docker15_{side}={value}" for side, value in DOCK.items()],
             "", "[verchk]", f"lastt={int(time.time())}", "",
             "[REAPERdockpref]", "reaper_training=0.50000000 15", ""]
    return "\n".join(lines)


def start(arguments):
    if reapers():
        fail("REAPER уже запущен; второй не запускается — звуковая карта одна")
    module = Path(arguments.module).resolve()
    if not module.exists():
        fail(f"нет модуля {module}: соберите его")
    if not REAPER.exists():
        fail(f"нет REAPER {REAPER}: задайте TRAINING_REAPER")

    if not Path("/tmp/.X11-unix/X" + DISPLAY[1:]).exists():
        xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", SCREEN, "-nolisten", "tcp"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                start_new_session=True)
        LIVE.mkdir(parents=True, exist_ok=True)
        (LIVE / "xvfb.pid").write_text(str(xvfb.pid))
        if not wait(lambda: Path("/tmp/.X11-unix/X" + DISPLAY[1:]).exists(), 5):
            fail("Xvfb не запустился")

    if not arguments.keep and LIVE.exists():
        for entry in LIVE.iterdir():
            if entry.name != "xvfb.pid":
                shutil.rmtree(entry) if entry.is_dir() and not entry.is_symlink() else entry.unlink()
    (LIVE / "UserPlugins").mkdir(parents=True, exist_ok=True)
    if not INI.exists():
        INI.write_text(config())
    # REAPER грузит из UserPlugins только файлы reaper_*.so.
    plugin = LIVE / "UserPlugins" / "reaper_training.so"
    plugin.unlink(missing_ok=True)
    plugin.symlink_to(module)
    license_file = LIVE / "reaper-license.rk"
    if not license_file.exists() and (USER / "reaper-license.rk").exists():
        license_file.symlink_to(USER / "reaper-license.rk")

    journal = JOURNAL.read_text(errors="replace") if JOURNAL.exists() else ""
    reaper = subprocess.Popen([str(REAPER), "-cfgfile", str(INI), "-nosplash", "-new"],
                              env=environment(), stdout=(LIVE / "reaper.out").open("w"),
                              stderr=subprocess.STDOUT, start_new_session=True)
    (LIVE / "reaper.pid").write_text(str(reaper.pid))

    loaded = lambda: JOURNAL.exists() and JOURNAL.read_text(errors="replace").count(" load (") > \
        journal.count(" load (")
    if not wait(loaded, 30):
        fail("модуль не загрузился: в журнале нет строки load")
    display = Display()
    if not wait(lambda: display.find("REAPER v") is not None, 30):
        fail("нет главного окна REAPER")
    time.sleep(1.0)
    print(f"REAPER {reaper.pid} на {DISPLAY}, папка ресурсов {LIVE}")
    for window, title, *_ in display.windows():
        if "REAPER v" not in title and title not in ("", "reaper"):
            print(f"live: открыто окно «{title}»", file=sys.stderr)


def lua(text):
    """Строка Lua, в которой `text` записан как есть."""
    level = 0
    while f"]{'=' * level}]" in text:
        level += 1
    return f"[{'=' * level}[{text}]{'=' * level}]"


def run(arguments):
    if not reapers():
        fail("REAPER не запущен: live.py start")
    script = Path(arguments.script).resolve()
    result = LIVE / "result.txt"
    result.unlink(missing_ok=True)
    wrapper = LIVE / "run.lua"
    wrapper.write_text(
        f"arg = {{{', '.join(lua(value) for value in arguments.args)}}}\n"
        f"local ok, value = xpcall(function() return dofile({lua(str(script))}) end,"
        f" debug.traceback)\n"
        f"local file = io.open({lua(str(result))}, 'w')\n"
        f"file:write(ok and tostring(value) or ('ERROR: ' .. tostring(value)))\n"
        f"file:close()\n")
    sender = subprocess.Popen([str(REAPER), "-cfgfile", str(INI), "-nonewinst", str(wrapper)],
                              env=environment(), stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
    if not wait(result.exists, arguments.timeout, 0.05):
        sender.kill()
        fail(f"скрипт не ответил за {arguments.timeout} с")
    try:
        sender.wait(5)
    except subprocess.TimeoutExpired:
        sender.kill()
    text = result.read_text(errors="replace")
    print(text)
    if text.startswith("ERROR: "):
        sys.exit(1)


def control(name):
    """Прямоугольник контрола `name` в forms.rc, диалоговые единицы, и вид
    контрола."""
    pattern = re.compile(r"^\s*(\w+)\s+(?:\"[^\"]*\"\s*,\s*)?" + re.escape(name)
                         + r"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)")
    for line in FORMS.read_text().splitlines():
        found = pattern.match(line)
        if found:
            kind, *box = found.groups()
            return kind, [int(value) for value in box]
    fail(f"в {FORMS.name} нет контрола {name}")


def trainer_window(display):
    trainer = display.find(TRAINER)
    if not trainer:
        fail("окна тренажёра нет: откройте его действием REAPER_TRAINING_WINDOW")
    return trainer


def centre(name):
    """Середина контрола `name` в окне тренажёра, пикселей."""
    kind, (x, y, width, height) = control(name)
    if kind == "COMBOBOX":
        height = COMBO_HEIGHT
    return round((x + width / 2) * SCALE), round((y + height / 2) * SCALE)


def click(arguments):
    display = Display()
    trainer = trainer_window(display)
    at = centre(arguments.control)
    display.click(trainer, *at)
    print(f"{arguments.control}: щелчок в {at[0]}, {at[1]}")
    if arguments.item is None:
        return

    if not arguments.of:
        fail("--item без --of: сколько пунктов в списке")
    if not wait(lambda: display.find("menu") is not None, 2):
        fail("список не раскрылся")
    menu = display.find("menu")
    size = display.attributes(menu)
    item = (size.width // 2, round((arguments.item + 0.5) * size.height / arguments.of))
    display.click(menu, *item)
    print(f"пункт {arguments.item} из {arguments.of}: щелчок в {item[0]}, {item[1]}")


def type_(arguments):
    display = Display()
    trainer = trainer_window(display)
    display.focus(trainer)
    display.click(trainer, *centre(arguments.control))
    # Ctrl+A выделяет весь текст поля, и первый знак заменяет его: поле не
    # проходит через значения, которые давало бы стирание по знаку.
    display.key("ctrl+a")
    for char in arguments.text:
        display.key(KEY_NAMES.get(char, char))
    print(f"{arguments.control}: набрано «{arguments.text}»")


def key(arguments):
    display = Display()
    display.focus(trainer_window(display))
    for name in arguments.keys:
        display.key(name)
    print(f"нажато: {' '.join(arguments.keys)}")


def hover(arguments):
    display = Display()
    at = centre(arguments.control)
    display.move(trainer_window(display), *at)
    print(f"{arguments.control}: мышь в {at[0]}, {at[1]}")


def shot(arguments):
    display = Display()
    if arguments.untitled:
        untitled = [window for window, title, *_ in display.windows() if not title]
        if not untitled:
            fail("окна без заголовка нет: подсказка не показана")
        window = untitled[0]
        display.raise_(window)
    else:
        window = trainer_window(display)
    width, height = display.shot(window, arguments.file)
    print(f"{arguments.file}: {width}×{height}")


def windows(_arguments):
    for window, title, x, y, width, height in Display().windows():
        print(f"0x{window:x} «{title}» {width}×{height} в {x}, {y}")


def quit_(_arguments):
    running = reapers()
    if running:
        subprocess.run([str(REAPER), "-cfgfile", str(INI), "-nonewinst",
                        "-closeall:nosave:exit"], env=environment(), stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=30, check=False)
        if not wait(lambda: not reapers(), 30):
            windows(None)
            fail("REAPER не вышел: окна выше")
    xvfb = LIVE / "xvfb.pid"
    if xvfb.exists():
        try:
            os.kill(int(xvfb.read_text()), 15)
        except (OSError, ValueError):
            pass
        xvfb.unlink()
    print("REAPER закрыт" if running else "REAPER не был запущен")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)

    command = commands.add_parser("start", help="запустить Xvfb и REAPER")
    command.add_argument("--module", default=str(ROOT / "build" / "reaper_training.so"),
                         help="модуль расширения, по умолчанию build/reaper_training.so")
    command.add_argument("--keep", action="store_true",
                         help="не очищать папку ресурсов: настройки и журнал прошлого запуска")
    command.set_defaults(handler=start)

    command = commands.add_parser("run", help="выполнить скрипт Lua в REAPER")
    command.add_argument("script")
    command.add_argument("args", nargs="*", help="строки в глобальной таблице arg скрипта")
    command.add_argument("--timeout", type=float, default=20.0)
    command.set_defaults(handler=run)

    command = commands.add_parser("click", help="щёлкнуть в контрол окна тренажёра")
    command.add_argument("control", help="имя контрола из forms.rc, например IDC_CALIBRATE")
    command.add_argument("--item", type=int, help="пункт раскрывшегося списка, с 0")
    command.add_argument("--of", type=int, help="сколько пунктов в списке")
    command.set_defaults(handler=click)

    command = commands.add_parser("type", help="набрать текст в поле окна тренажёра")
    command.add_argument("control", help="поле из forms.rc, например IDC_SILENCE")
    command.add_argument("text", help="текст: буквы, цифры и знаки - + . , = _ и пробел")
    command.set_defaults(handler=type_)

    command = commands.add_parser("key", help="нажать клавиши в окне тренажёра")
    command.add_argument("keys", nargs="+", help="клавиши X: BackSpace, Tab, End, ctrl+a")
    command.set_defaults(handler=key)

    command = commands.add_parser("hover", help="навести мышь на контрол окна тренажёра")
    command.add_argument("control", help="имя контрола из forms.rc, например IDC_TOLERANCE")
    command.set_defaults(handler=hover)

    command = commands.add_parser("shot", help="снять окно тренажёра в PNG")
    command.add_argument("file")
    command.add_argument("--untitled", action="store_true",
                         help="снять окно верхнего уровня без заголовка — подсказку у курсора")
    command.set_defaults(handler=shot)

    command = commands.add_parser("windows", help="перечислить окна верхнего уровня")
    command.set_defaults(handler=windows)

    command = commands.add_parser("quit", help="выйти из REAPER и остановить Xvfb")
    command.set_defaults(handler=quit_)

    arguments = parser.parse_args()
    arguments.handler(arguments)


if __name__ == "__main__":
    main()
