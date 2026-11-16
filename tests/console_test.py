import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from console import Console
import serial


def port(pid=0x4005, identity="ABC", device="/dev/ttyACM0"):
    return SimpleNamespace(vid=0x303A, pid=pid, serial_number=identity,
                           device=device, product="BLE USB Keyboard")


class ConsoleTest(unittest.TestCase):
    def test_device_state_and_pin_lifecycle(self):
        c = Console()
        c.log("\x1b[32mI (1) bridge: Orientation landscape; accelerometer absent/disabled\x1b[0m", device=True)
        self.assertEqual((c.orientation, c.imu), ("landscape", "absent/disabled"))
        c.log("Enter this PIN on the keyboard, then Enter: 000123", device=True)
        self.assertEqual(c.pin, "000123")
        c.log("BLE GAP AUTH SUCCESS", device=True)
        self.assertEqual(c.pin, "")
        c.log("Keyboard connected: MX Keys Mini", device=True)
        self.assertEqual(c.keyboard, "MX Keys Mini")
        c.log("Keyboard connected. Orientation portrait; accelerometer absent/disabled.", device=True)
        self.assertEqual(c.keyboard, "MX Keys Mini")
        c.log("Keyboard disconnected; keys released", device=True)
        self.assertEqual(c.keyboard, "disconnected")
        self.assertNotIn("\x1b", "".join(c.lines))

    def test_bounded_snapshot_and_session_capture(self):
        with TemporaryDirectory() as directory:
            c = Console(dump_dir=directory)
            for i in range(2100):
                c.log(f"line {i}")
            self.assertEqual(len(c.lines), 2000)
            dump = c.dump()
            self.assertEqual(len(dump.read_text().splitlines()), 2000)
            recording = Path(directory) / "record.log"
            c.record(recording)
            c.log("session marker")
            c.record()
            self.assertIn("session marker", recording.read_text())
            c.close()

    def test_partial_serial_lines_and_utf8(self):
        c = Console()
        c.connection = Mock(in_waiting=100)
        c.connection.read.side_effect = [b"I: caf\xc3", b"\xa9\r\nKeyboard connected: MX", b" Keys Mini\n"]
        c.poll()
        self.assertEqual(len(c.lines), 0)
        c.poll()
        self.assertIn("café", c.lines[-1])
        self.assertEqual(c.keyboard, "unknown")
        c.poll()
        self.assertEqual(c.keyboard, "MX Keys Mini")

    def test_commands_reject_injection_and_detach_for_bootloader(self):
        c = Console()
        connection = c.connection = Mock()
        self.assertFalse(c.send("status\nforget"))
        self.assertFalse(c.send("x" * 32))
        connection.write.assert_not_called()
        self.assertTrue(c.send("orientation landscape-inverted"))
        connection.write.assert_called_with(b"orientation landscape-inverted\n")
        self.assertTrue(c.send("bootloader"))
        connection.close.assert_called_once()
        self.assertIsNone(c.connection)

    def test_rom_port_is_left_available_to_flasher(self):
        c = Console()
        with patch("console.list_ports.comports", return_value=[port(pid=0x1001)]), \
             patch("console.serial.Serial") as open_serial:
            c.poll()
            open_serial.assert_not_called()
            self.assertIn("ROM bootloader", c.notice)

    def test_composite_storage_device_and_network_status(self):
        c = Console()
        device = port(pid=0x4007)
        device.product = "ESP Input Bridge"
        with patch("console.list_ports.comports", return_value=[device]), \
             patch("console.serial.Serial", return_value=Mock()):
            c.poll()
            self.assertIsNotNone(c.connection)
        c.log("Wi-Fi address: 192.0.2.10", device=True)
        c.log("Deskflow ready", device=True)
        self.assertEqual((c.wifi, c.deskflow), ("192.0.2.10", "ready"))
        c.log("Deskflow screen entered", device=True)
        self.assertEqual(c.deskflow, "active")

    def test_explicit_missing_port_does_not_open_another_device(self):
        c = Console(port="/dev/missing-bridge")
        with patch("console.list_ports.comports", return_value=[port()]), \
             patch("console.serial.Serial") as open_serial:
            c.poll()
            open_serial.assert_not_called()

    def test_reconnect_follows_same_device_after_port_number_changes(self):
        c = Console(port="/dev/ttyACM0")
        c.identity = "ABC"
        new = port(device="/dev/ttyACM3")
        with patch("console.list_ports.comports", return_value=[port(identity="OTHER"), new]), \
             patch("console.serial.Serial", return_value=Mock()) as open_serial:
            c.poll()
            self.assertEqual(open_serial.call_args.args[0], "/dev/ttyACM3")
            c.connection.write.assert_called_with(b"status\n")

    def test_explicit_rom_port_matches_application_identity(self):
        c = Console(port="/dev/ttyACM0")
        with patch("console.list_ports.comports", return_value=[port(pid=0x1001, identity="AA:BB:CC")]):
            self.assertIsNone(c.discover())
        with patch("console.list_ports.comports", return_value=[port(identity="AABBCC", device="/dev/ttyACM1")]):
            self.assertEqual(c.discover().device, "/dev/ttyACM1")

    def test_disconnect_during_read_resets_partial_state(self):
        c = Console()
        connection = c.connection = Mock(in_waiting=1)
        connection.read.side_effect = serial.SerialException("device removed")
        c.pending, c.pin = "unfinished", "000123"
        c.poll()
        self.assertIsNone(c.connection)
        self.assertEqual((c.pending, c.pin), ("", ""))
        connection.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
