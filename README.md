# BLE keyboard → USB HID bridge

Firmware for the Waveshare **ESP32-S3-LCD-1.47 USB-A stick** (non-touch,
revision 0.2, 16 MB flash, 8 MB PSRAM).
Built with the installed ESP-IDF 6.1. The computer sees a standard USB boot
keyboard plus a USB CDC serial console. No computer-side keyboard driver or
input-injection application is needed.

## Build and install

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh
idf.py set-target esp32s3   # first build only; resets sdkconfig
idf.py menuconfig          # optional: Keyboard bridge → exact keyboard name
idf.py build
idf.py -p /dev/ttyACM0 flash
```

For subsequent updates, close other serial terminals and run:

```sh
bash tools/flash.sh
```

This builds, sends `bootloader` over the bridge's USB console, waits for its ROM
Serial/JTAG port, flashes it, and applies a watchdog reset if the chip remains in
download mode after the usual RTS reset. The first installation still requires
manual BOOT entry. Connect only the intended ESP while using this helper.
To enter download mode without flashing, activate ESP-IDF and run
`python tools/enter_bootloader.py`, or send `bootloader` plus Enter from a serial
terminal. HID disconnects while in the bootloader.

Use the board's **native USB port**, connected to the target computer. The
port that currently exposes Espressif USB JTAG/serial is suitable. A separate
USB-to-UART connector cannot present this firmware's USB keyboard interface.
Firmware switches that native port from the fixed USB Serial/JTAG peripheral
to USB OTG; it will disconnect and enumerate again with a different USB ID.

The onboard 172×320 display shows scanning, connection progress, pairing PIN,
connected keyboard, unsupported reports or pairing failures, and USB status.
The six-digit PIN preserves leading zeroes. It clears after authentication or
disconnect. **Hold BOOT for three seconds** to forget the keyboard and restart
pairing; a serial terminal is optional.

Display wiring follows the [Waveshare board documentation](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47):
MOSI 45, SCLK 40, CS 42, DC 41, RESET 39, backlight 48. The ST7789 panel uses
RGB565 and a 34-pixel controller offset (on the short axis after rotation).
Rendering uses a 16-row DMA strip (10 KB maximum), avoiding a full framebuffer or
dependency on PSRAM/LVGL. SPI operations run in a display task; Bluetooth
callbacks only update its state.

### Orientation

The default is **automatic with portrait fallback**: without a configured,
working accelerometer, the original 172×320 portrait layout is used. Tap BOOT
to cycle through portrait, landscape, inverted portrait, and inverted landscape.
Holding BOOT for three seconds still forgets the keyboard. Manual orientation
choices survive reboot independently of keyboard pairing.

The serial console accepts these commands:

```text
orientation portrait
orientation landscape
orientation portrait-inverted
orientation landscape-inverted
orientation auto
```

Landscape is a 320×172 layout with a compact header/footer and a large central
PIN. It reflows content rather than scaling the portrait screen. Rotation is
applied by the ST7789 driver; the renderer only needs the logical viewport.

The USB-A ESP32-S3-LCD-1.47 has **no onboard accelerometer**, as shown in its
[schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47/ESP32-S3-LCD-1.47_schematic_diagram.pdf).
The [1.47B](https://docs.waveshare.com/ESP32-S3-LCD-1.47B) has a QMI8658, but is
a different board with different backlight wiring. This firmware does not
silently substitute that board's wiring.

For an external QMI8658C, enable **Keyboard bridge → Enable optional QMI8658
accelerometer** in `idf.py menuconfig`, then configure SDA/SCL on the expansion
header and address 0x6A or 0x6B. The backend is off by default and touches no I2C
pins when off. Mount the sensor parallel to the display and configure X/Y
swap/inversion to align its axes with the screen. The implementation follows
the [QMI8658C datasheet](https://files.waveshare.com/upload/3/36/QMI8658C_datasheet_rev_0.8.pdf).
This external hardware path compiles but has not been tested on a physical IMU.

Automatic rotation requires a dominant gravity axis held for 500 ms. Diagonal,
flat, free-fall, and high-acceleration samples keep the last orientation instead
of flickering. A missing sensor or two seconds without fresh data falls back to
portrait in automatic mode; manual overrides stay selected.

Code boundaries: `screen.c` renders the status at viewport dimensions;
`orientation.c` defines panel geometry and the sensor-independent gravity policy;
`accelerometer.c` supplies optional hardware samples; `display.c` coordinates
them and owns panel operations and persisted preferences.

After installing, locate the optional console under `/dev/serial/by-id/` and open
it with a serial terminal (115200 baud; CDC does not depend on this rate).
For example, after activating ESP-IDF:

```sh
python -m serial.tools.miniterm /dev/ttyACM0 115200
```

Exit miniterm with Ctrl+]. Use the actual new device path if its number changes.

## RGB status light

The onboard WS2812B RGB LED (GPIO38, RGB channel order) shows the bridge state:

| Light | Meaning |
| --- | --- |
| Solid red | Starting, no matching keyboard found, connection/authentication failure, unsupported keyboard, or connected keyboard with USB unavailable |
| Fast blue blink (5 Hz) | A BLE scan is in progress |
| Slow blue blink (1 Hz) | Connecting, authenticating, entering a pairing PIN, or discovering the keyboard's reports |
| Solid green | A supported keyboard is connected and USB is ready to forward input |

Scans last five seconds. If no match is found, the light returns to red during
the two-second retry interval, then blinks blue again for the next scan.
Blinking runs in a dedicated task using the RMT/DMA LED driver; Bluetooth event
callbacks only update its state. `Indicator: ...` transitions appear in console
logs. Brightness is limited to 24/255 for the onboard indicator.

## Interactive serial console

From this project, run:

```sh
bash tools/console.sh
```

The launcher uses the existing ESP-IDF Python environment, which already has
pyserial. The standalone script can also run with `python tools/console.py`
after activating that environment. For another Python environment, install
`python -m pip install -r tools/requirements.txt`. The TUI uses Python's standard
`curses` module and is intended for a Linux/macOS terminal.

It auto-detects the bridge, shows live logs and pairing PINs, and tracks keyboard,
orientation, and accelerometer status. It follows the same device if its serial
port number changes, reconnects after reboot, and leaves the ROM bootloader port
free for flashing. Close other serial monitors before starting it.

| Key | Action |
| --- | --- |
| `S` | Request device status |
| `O` | Select any orientation or automatic mode with arrows and Enter |
| `F` | Forget keyboard and restart pairing; confirm with `Y` |
| `B` | Enter ROM bootloader; confirm with `Y` |
| `R` | Reconnect the console |
| `D` | Dump the latest 2,000 buffered log lines to a timestamped file |
| `L` | Start/stop continuous session recording |
| `:` | Type a raw console command (Enter sends, Esc cancels) |
| Page Up / Page Down | Scroll logs |
| End | Return to following live logs |
| `Q` | Quit |

Dumps and recordings go to the ignored `logs/` directory by default. They use
UTC timestamps and remove terminal color/control sequences. `D` saves the
current buffer; `L` records future events until stopped. Options:

```sh
bash tools/console.sh --list-ports
bash tools/console.sh --port /dev/serial/by-id/usb-ESP_Bridge_BLE_USB_Keyboard_A0F262E39B84-if00
bash tools/console.sh --dump-dir /tmp/bridge-logs --record /tmp/bridge-session.log
```

After selecting `B`, run `bash tools/flash.sh` in another terminal. The TUI waits
without opening the ROM port and reconnects when the application returns.

Tested against the connected ESP: status requests, orientation selection, log
snapshot/recording, cancelling a forget prompt, and bootloader → external flash
→ automatic reconnection. The MX Keys Mini also reconnected after that update.
Protocol, buffering, capture and serial lifecycle tests run with
`python tests/console_test.py` in the ESP-IDF environment.

## Pair a keyboard

1. Put a BLE keyboard into pairing mode. For a Logitech model with an
   Easy-Switch Bluetooth slot, select a spare slot and hold its pairing button
   until it indicates pairing; follow the keyboard's own instructions.
2. The ESP scans in five-second windows. With no saved peer or name filter,
   it selects the strongest HID device advertising a keyboard or generic HID
   appearance (Logitech MX Keys Mini uses generic HID). The report descriptor
   determines whether keyboard input is accepted after connection.
   Pair only the intended keyboard nearby, or set its exact advertised name
   through `menuconfig`. A name filter also handles keyboards without an
   advertised keyboard appearance.
3. Watch the display (or serial console). If a PIN appears, type its six digits on the
   Bluetooth keyboard and press Enter. The ESP advertises display-only pairing
   capability and requests secure bonding.
4. Once connected, type in an empty editor on the computer. Modifiers,
   press/release events, arrows, and other standard keyboard usages are relayed.
5. The peer address and Bluetooth bonds survive reboot. Disconnecting the
   keyboard releases USB keys and resumes scanning for the saved address.

Hold BOOT for three seconds, or type `forget` followed by Enter in the serial console, to delete the saved
peer and Bluetooth bonds, then reboot into pairing mode. Any other line prints
connection status. If the keyboard still remembers an old ESP bond, remove
that pairing on the keyboard or choose a fresh pairing slot too.

## Supported input and limits

- Bluetooth **LE HID over GATT** only. ESP32-S3 has no Bluetooth Classic radio
  support. Logitech Unifying/Bolt dongle protocols are not implemented.
- One keyboard at a time. Descriptor-validated reports must contain eight
  modifier bits, an optional reserved byte, and six eight-bit key usages, with optional
  report IDs. NKRO bitmaps, unusual layouts, mouse reports and consumer/media
  keys are ignored and logged. A specific keyboard may require another decoder.
- Lock state changes still work on the computer; its Caps/Num/Scroll Lock LEDs
  are not sent back to the physical keyboard yet.
- Reconnection currently matches the saved BLE address. Keyboards that rotate
  their private advertising address may require `forget` and pairing again;
  identity-based reconnection needs hardware validation.
- USB suspend/disconnect discards queued input. Reconnecting sends a neutral
  state instead of replaying buffered keystrokes. Remote wake is not implemented.
- Queue overflow inserts a release before the latest report to avoid stuck keys.

## Future phone input

The phone app is not implemented in this version. A future custom GATT service
can accept a versioned packet containing modifier bits and key usages, validate
it, and feed the same USB report queue. It should release keys on disconnect and
timeout, authenticate its peer, and arbitrate explicitly between phone and
physical keyboard input. The ESP is currently a GATT client for the keyboard;
a phone writing to the ESP would additionally need a GATT server.

## Recovery and backup

`backups/original-esp32s3.bin` contains the original full 16 MB flash, when the
initial backup has completed. It is excluded from version control.

To reflash after native USB has switched to HID/CDC: hold BOOT, press and release
RESET, then release BOOT. Alternatively, hold BOOT while unplugging/reconnecting
the USB stick, then release BOOT. The ROM USB Serial/JTAG port should return.
Close serial terminals before flashing. The explicit `bootloader` command is
supported; ordinary RTS/DTR toggling on the application CDC does not reboot it.

To restore the original image while in ROM download mode:

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 backups/original-esp32s3.bin
```

## Validation

Build with `idf.py build`. The descriptor parser has native tests covering
standard layouts with and without report IDs, truncation, changed offsets,
and unsupported bitmaps/lengths:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain \
  main/keyboard_layout.c tests/keyboard_layout_test.c -o /tmp/keyboard-layout-test
/tmp/keyboard-layout-test
```

Verified on the connected board: ESP-IDF build, Linux USB keyboard/CDC
enumeration (`303a:4005`), console status response, BLE scan loop, and a full
software-triggered bootloader → flash → application cycle using `tools/flash.sh`.
The display's status screen was confirmed by the user. A Logitech MX Keys Mini
was discovered with generic HID appearance, authenticated with the displayed
PIN, and connected with a descriptor-validated seven-byte keyboard report.
The computer then received an actual press and release from the ESP USB input
device, confirming the BLE → firmware → USB → host input path. All four display
rotations and the sensor-free portrait fallback were exercised through the
console. Other keyboard models and broader key combinations need further hardware
testing. The original flash backup's SHA-256 was checked.

Screen rendering tests compare all state screens rendered as full frames and
16-row strips under memory/undefined-behavior sanitizers:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain \
  main/screen.c main/orientation.c tests/screen_test.c -o /tmp/bridge-screen-test
/tmp/bridge-screen-test
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain \
  main/orientation.c tests/orientation_test.c -o /tmp/orientation-test
/tmp/orientation-test
```

RGB indicator state and blink timing tests:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain \
  main/status_led_pattern.c tests/status_led_test.c -o /tmp/bridge-status-led-test
/tmp/bridge-status-led-test
```

`main/esp_hid_gap.c` and `.h` derive from Espressif's ESP-IDF 6.1
`examples/bluetooth/esp_hid_host` example (Unlicense OR CC0-1.0). They retain their
license notices; BLE discovery and pairing behavior are adapted for this bridge.
