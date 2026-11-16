#!/usr/bin/env python3
"""Switch the connected bridge to its ROM flashing port (requires pyserial)."""
import sys
import time
import serial
from serial.tools import list_ports


def find(pid):
    pids = pid if isinstance(pid, tuple) else (pid,)
    matches = [p for p in list_ports.comports() if p.vid == 0x303A and p.pid in pids]
    if len(matches) > 1:
        raise SystemExit("Multiple ESP devices found; connect only the intended bridge.")
    return matches[0].device if matches else None


def main():
    rom = find(0x1001)
    if rom:
        print(rom)
        return
    port = sys.argv[1] if len(sys.argv) > 1 else find((0x4005, 0x4007))
    if not port:
        raise SystemExit("Bridge not found. For initial recovery, hold BOOT while reconnecting USB.")
    try:
        with serial.Serial(port, 115200, timeout=0.2, write_timeout=2) as connection:
            connection.write(b"bootloader\n")
            connection.flush()
            time.sleep(0.2)
    except serial.SerialException as error:
        # Disconnecting while the command takes effect is expected; an initial
        # open failure is reported if no ROM port appears within the deadline.
        print(str(error), file=sys.stderr)
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        rom = find(0x1001)
        if rom:
            print(rom)
            return
        time.sleep(0.2)
    raise SystemExit("ROM port did not appear. Close other serial terminals and retry, or hold BOOT while reconnecting USB.")


if __name__ == "__main__":
    main()
