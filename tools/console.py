#!/usr/bin/env python3
"""Live USB console for the BLE keyboard bridge (Python + pyserial + curses)."""
import argparse
import codecs
import curses
from collections import deque
from datetime import datetime, timezone
from pathlib import Path
import re
import sys
import textwrap
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    raise SystemExit("Install pyserial: python -m pip install -r tools/requirements.txt\n"
                     "Or run with ~/.espressif/tools/python/v6.1/venv/bin/python")

MODES = ("auto", "portrait", "landscape", "portrait-inverted", "landscape-inverted")
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def clean(text):
    return "".join(c for c in ANSI.sub("", text) if c.isprintable() or c == "\t").expandtabs(4)


def identity_key(value):
    return value.replace(":", "").upper() if value else None


class Console:
    """Serial lifecycle, bounded logs, device state and file capture; no curses."""

    def __init__(self, port=None, dump_dir="logs"):
        self.port_hint, self.dump_dir = port, Path(dump_dir).expanduser()
        self.connection = None
        self.port = "not connected"
        self.identity = None
        self.notice = "Waiting for the bridge"
        self.keyboard, self.orientation, self.imu, self.pin = "unknown", "unknown", "unknown", ""
        self.lines = deque(maxlen=2000)
        self.pending = ""
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self.retry_at = 0
        self.recording = None
        self.record_path = None

    def log(self, message, device=False):
        message = clean(message)
        stamp = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        line = f"{stamp} {message}"
        self.lines.append(line)
        if self.recording:
            try:
                self.recording.write(line + "\n")
                self.recording.flush()
            except OSError as error:
                self.recording.close()
                self.recording = None
                self.notice = f"Recording stopped: {error}"
        if device:
            if "Keyboard connected:" in message:
                self.keyboard = message.split("Keyboard connected:", 1)[1].strip()
                self.pin = ""
            elif re.search(r"Keyboard (connected|disconnected)\.", message):
                state = re.search(r"Keyboard (connected|disconnected)\.", message)[1]
                if state == "disconnected" or self.keyboard in ("unknown", "disconnected"):
                    self.keyboard = state
                if state == "disconnected":
                    self.pin = ""
            elif "Keyboard disconnected;" in message:
                self.keyboard, self.pin = "disconnected", ""
            match = re.search(r"Orientation ([a-z-]+)[; (]", message)
            if match:
                self.orientation = match[1]
            match = re.search(r"accelerometer (available|absent/disabled)", message, re.I)
            if match:
                self.imu = match[1].lower()
            match = re.search(r"Enter this PIN.*?:\s*(\d{6})", message)
            if match:
                self.pin = match[1]
            if "AUTH SUCCESS" in message or "AUTH ERROR" in message:
                self.pin = ""

    def inform(self, message):
        if self.notice != message:
            self.log("HOST: " + message)
        self.notice = message

    def discover(self):
        ports = list(list_ports.comports())
        if self.port_hint and not self.identity:
            target = Path(self.port_hint).resolve()
            match = next((p for p in ports if Path(p.device).resolve() == target), None)
            if match and (match.vid, match.pid) == (0x303A, 0x1001):
                self.identity = identity_key(match.serial_number)
                self.inform("ROM bootloader active; run bash tools/flash.sh in another terminal")
                return None
            if match:
                return match
            if not self.identity:
                self.inform("Waiting for " + self.port_hint)
                return None
        apps = [p for p in ports if (p.vid, p.pid) == (0x303A, 0x4005)
                and (not self.identity or identity_key(p.serial_number) == self.identity)
                and getattr(p, "product", None) in (None, "BLE USB Keyboard")]
        if len(apps) > 1:
            self.inform("Multiple bridges found; restart with --port /dev/serial/by-id/...")
        elif apps:
            return apps[0]
        elif any((p.vid, p.pid) == (0x303A, 0x1001) for p in ports):
            self.inform("ROM bootloader active; run bash tools/flash.sh in another terminal")
        else:
            self.inform("Waiting for the bridge to reconnect")
        return None

    def disconnect(self):
        if self.connection:
            self.connection.close()
        self.connection = None
        self.pin = ""
        self.keyboard = "unknown"
        self.pending = ""
        self.decoder.reset()

    def poll(self):
        if not self.connection:
            if time.monotonic() < self.retry_at:
                return
            self.retry_at = time.monotonic() + 1
            port = self.discover()
            if not port:
                return
            try:
                self.connection = serial.Serial(port.device, 115200, timeout=0,
                                                write_timeout=0.5, exclusive=True)
                self.port, self.identity = port.device, identity_key(port.serial_number)
                self.inform("Connected to " + port.device)
                self.send("status")
            except (OSError, serial.SerialException) as error:
                self.disconnect()
                self.inform(str(error))
            return
        try:
            data = self.connection.read(min(self.connection.in_waiting, 65536))
            self.pending += self.decoder.decode(data)
            pieces = self.pending.split("\n")
            self.pending = pieces.pop()
            for line in pieces:
                self.log(line.rstrip("\r"), device=True)
            if len(self.pending) > 4096:
                self.log(self.pending[:4096] + " [truncated]", device=True)
                self.pending = ""
        except (OSError, serial.SerialException) as error:
            self.disconnect()
            self.inform("Disconnected: " + str(error))

    def send(self, command):
        if not command or not command.isascii() or not command.isprintable() or len(command) > 31:
            self.inform("Commands must be 1–31 printable ASCII characters")
            return False
        if not self.connection:
            self.inform("Cannot send: device is not connected")
            return False
        try:
            self.connection.write((command + "\n").encode("ascii"))
            self.connection.flush()
            self.log(">>> " + command)
            if command == "bootloader":
                self.disconnect()
                self.retry_at = time.monotonic() + 1
                self.inform("Entering bootloader; console will reconnect after flashing")
            return True
        except (OSError, serial.SerialException) as error:
            self.disconnect()
            self.inform("Send failed: " + str(error))
            return False

    def log_path(self, prefix):
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
        return self.dump_dir / f"{prefix}-{stamp}.log"

    def dump(self):
        path = self.log_path("dump")
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")
            self.inform("Saved " + str(path.resolve()))
            return path
        except OSError as error:
            self.inform("Dump failed: " + str(error))
            return None

    def record(self, path=None):
        if self.recording:
            self.recording.close()
            self.recording = None
            self.inform("Recording stopped: " + str(self.record_path))
            return
        path = Path(path).expanduser() if path else self.log_path("session")
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            self.recording = path.open("a", encoding="utf-8")
            self.record_path = path.resolve()
            self.inform("Recording to " + str(self.record_path))
        except OSError as error:
            self.inform("Recording failed: " + str(error))

    def close(self):
        self.disconnect()
        if self.recording:
            self.recording.close()


def tui(window, console):
    window.nodelay(True)
    curses.set_escdelay(25)
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    scroll, menu, selection, confirm, command = 0, False, 0, None, None

    def put(y, text, attr=0, x=0):
        height, width = window.getmaxyx()
        if 0 <= y < height and x < width - 1:
            try:
                window.addnstr(y, x, text, width - x - 1, attr)
            except curses.error:
                pass

    while True:
        console.poll()
        height, width = window.getmaxyx()
        window.erase()
        put(0, "ESP KEYBOARD BRIDGE  |  " + ("ONLINE" if console.connection else "WAITING"), curses.A_BOLD)
        if height < 14 or width < 60:
            put(2, "Resize terminal to at least 60 columns by 14 rows. Q quits.")
        else:
            put(1, console.port + ("  [RECORDING]" if console.recording else ""))
            put(2, f"Keyboard: {console.keyboard} | View: {console.orientation} | IMU: {console.imu}")
            put(3, f"PAIRING PIN: {console.pin}  Type on keyboard, then Enter" if console.pin else "Live device logs (timestamps in UTC)", curses.A_BOLD if console.pin else 0)
            put(4, "─" * (width - 1))
            wrapped = [part for line in console.lines for part in textwrap.wrap(line[11:], width - 2) or [""]]
            rows = height - 10
            scroll = min(scroll, max(0, len(wrapped) - rows))
            end = max(0, len(wrapped) - scroll)
            for y, line in enumerate(wrapped[max(0, end - rows):end], 5):
                put(y, line)
            put(height - 5, "─" * (width - 1))
            put(height - 4, "S status  O orientation  R reconnect  D dump logs  L record")
            put(height - 3, "F forget  B bootloader  : command  PgUp/PgDn scroll  End live  Q quit")
            put(height - 2, "> " + command if command is not None else
                f"Send {confirm}? Y confirms, Esc cancels" if confirm else console.notice)
            put(height - 1, f"{len(console.lines)}/2000 buffered log lines" + (" | scrollback" if scroll else " | following"))
            if menu:
                top = 5
                for y in range(top, top + len(MODES) + 1):
                    put(y, " " * (width - 1))
                put(top, "Select orientation: arrows + Enter, Esc cancels", curses.A_BOLD)
                for i, mode in enumerate(MODES):
                    put(top + i + 1, ("> " if i == selection else "  ") + mode,
                        curses.A_REVERSE if i == selection else 0)
        window.refresh()
        key = window.getch()
        if command is not None:
            if key == 27:
                command = None
            elif key in (10, 13, curses.KEY_ENTER):
                console.send(command)
                command = None
            elif key in (curses.KEY_BACKSPACE, 127, 8):
                command = command[:-1]
            elif 32 <= key <= 126 and len(command) < 31:
                command += chr(key)
        elif confirm:
            if key in (ord('y'), ord('Y')):
                console.send(confirm)
                confirm = None
            elif key != -1:
                confirm = None
        elif menu:
            if key in (curses.KEY_UP, ord('k')):
                selection = (selection - 1) % len(MODES)
            elif key in (curses.KEY_DOWN, ord('j')):
                selection = (selection + 1) % len(MODES)
            elif key in (10, 13, curses.KEY_ENTER):
                console.send("orientation " + MODES[selection])
                menu = False
            elif key == 27:
                menu = False
        elif key in (ord('q'), ord('Q')):
            return
        elif key in (ord('s'), ord('S')):
            console.send("status")
        elif key in (ord('o'), ord('O')):
            menu = True
        elif key in (ord('r'), ord('R')):
            console.disconnect()
            console.retry_at = 0
        elif key in (ord('d'), ord('D')):
            console.dump()
        elif key in (ord('l'), ord('L')):
            console.record()
        elif key in (ord('f'), ord('F')):
            confirm = "forget"
        elif key in (ord('b'), ord('B')):
            confirm = "bootloader"
        elif key == ord(':'):
            command = ""
        elif key == curses.KEY_PPAGE:
            scroll += max(1, height - 10)
        elif key == curses.KEY_NPAGE:
            scroll = max(0, scroll - max(1, height - 10))
        elif key == curses.KEY_END:
            scroll = 0
        time.sleep(0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port or /dev/serial/by-id path (default: auto-detect)")
    parser.add_argument("--dump-dir", default=str(Path(__file__).resolve().parents[1] / "logs"))
    parser.add_argument("--record", metavar="FILE", help="continuously append session logs to FILE")
    parser.add_argument("--list-ports", action="store_true", help="list ports and exit")
    args = parser.parse_args()
    if args.list_ports:
        for port in list_ports.comports():
            print(f"{port.device}\t{port.description}\t{port.hwid}")
        return 0
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        parser.error("the TUI needs an interactive terminal")
    console = Console(args.port, args.dump_dir)
    if args.record:
        console.record(args.record)
    try:
        curses.wrapper(tui, console)
    except KeyboardInterrupt:
        return 130
    except curses.error as error:
        print(f"Terminal initialization failed: {error}", file=sys.stderr)
        return 1
    finally:
        console.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
