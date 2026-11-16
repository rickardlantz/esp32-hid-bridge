# BLE / Deskflow → USB input bridge

Firmware for the Waveshare **ESP32-S3-LCD-1.47 USB-A stick** and
**ESP32-S3-LCD-1.47B USB-C board** (16 MB flash, 8 MB PSRAM).
Built with the installed ESP-IDF 6.1. The computer sees a standard USB boot
keyboard/mouse, a USB CDC serial console, and a writable configuration drive. No computer-side keyboard driver or
input-injection application is needed.

## Board selection

Both boards run the same application with a different build profile:

| Board | Backlight | Accelerometer | Update command |
| --- | --- | --- | --- |
| ESP32-S3-LCD-1.47 USB-A | GPIO48 | Optional external QMI8658; disabled by default | `bash tools/flash.sh` |
| ESP32-S3-LCD-1.47B USB-C | GPIO46 | Onboard QMI8658, SDA48/SCL47, address 0x6B | `bash tools/flash.sh --board lcd-147b` |

The B profile enables its onboard accelerometer by default; it can still be
disabled in menuconfig, and missing/failed hardware retains the portrait fallback.
`main/board.h` centralizes wiring, while display/orientation and input/network
logic remain shared. The B profile has its own SDK configuration and build under
`build/lcd-147b`, preserving the original USB-A build and defaults.

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh
idf.py -B build/lcd-147b menuconfig  # after the first B-profile build
```

The boards share LCD SPI pins and the RGB LED on GPIO38. Pin assignments follow
the [Waveshare B documentation](https://docs.waveshare.com/ESP32-S3-LCD-1.47B)
and [schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.47B/ESP32-S3-LCD-1.47B_schematic_diagram.pdf).
Select the correct profile rather than installing the USB-A binary on the B:
GPIO48 is an I2C data pin on the B, not its backlight.

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
The [1.47B](https://docs.waveshare.com/ESP32-S3-LCD-1.47B) has a QMI8658 and different backlight wiring. Select the B profile above
to use it; the firmware never silently substitutes one board’s wiring for another.

For an external QMI8658C, enable **Keyboard bridge → Enable optional QMI8658
accelerometer** in `idf.py menuconfig`, then configure SDA/SCL on the expansion
header and address 0x6A or 0x6B. The backend is off by default and touches no I2C
pins when off. Mount the sensor parallel to the display and configure X/Y
swap/inversion to align its axes with the screen. The implementation follows
the [QMI8658C datasheet](https://files.waveshare.com/upload/3/36/QMI8658C_datasheet_rev_0.8.pdf).
The external-sensor wiring path requires separate hardware validation.

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
| Fast orange blink (5 Hz) | Searching for / connecting to Wi-Fi |
| Solid orange | Wi-Fi connected; Deskflow disconnected |
| Slow turquoise blink (1 Hz) | Connecting to Deskflow |
| Solid turquoise | Deskflow connected |
| Slow blue blink (1 Hz) | Connecting, authenticating, entering a pairing PIN, or discovering the keyboard's reports |
| Solid green | A supported keyboard is connected and USB is ready to forward input |

Network stages take priority over Bluetooth status. With no active Wi-Fi profile,
the original Bluetooth red/blue/green indications apply. Solid turquoise means
Deskflow is connected; the display and diagnostic log separately report USB readiness.

BLE scans last five seconds. If no match is found, the light returns to red during
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
| `N` | Request Wi-Fi and Deskflow status |
| `C` | Reload configuration |
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
bash tools/console.sh --port /dev/serial/by-id/usb-ESP_Bridge_ESP_Input_Bridge_A0F262E39B84-if00
bash tools/console.sh --dump-dir /tmp/bridge-logs --record /tmp/bridge-session.log
```

After selecting `B`, run `bash tools/flash.sh` in another terminal. The TUI waits
without opening the ROM port and reconnects when the application returns.

Tested against the connected ESP: status requests, orientation selection, log
snapshot/recording, cancelling a forget prompt, and bootloader → external flash
→ automatic reconnection. The MX Keys Mini also reconnected after that update.
Protocol, buffering, capture and serial lifecycle tests run with
`python tests/console_test.py` in the ESP-IDF environment.

## X11 server: keyboard/scroll work but pointer is stuck

The Arch test laptop's Cinnamon 6.8.0 / Muffin 6.8.1 applied a 2× Coordinate
Transformation Matrix to its touchpad and XTEST pointer. This corrupts relative
movement after Deskflow warps the source pointer, causing immediate screen exits
and movement clamped to the remote screen's corner. It matches
[Muffin issue #888](https://github.com/linuxmint/muffin/issues/888), whose upstream
fix reverts the coordinate scaling change. This is a server desktop issue.

Check the server with `xinput list-props "Virtual core XTEST pointer"` and the
corresponding touchpad/mouse name from `xinput list`. For this specific 2× bug,
the temporary runtime workaround is:

```sh
xinput set-prop "Virtual core XTEST pointer" "Coordinate Transformation Matrix" 1 0 0 0 1 0 0 0 1
xinput set-prop "YOUR TOUCHPAD NAME" "Coordinate Transformation Matrix" 1 0 0 0 1 0 0 0 1
```

Apply only to affected devices; preserve deliberate rotated/mapped-device matrices.
The desktop may reapply its matrix after login or device/display changes. The
runtime test on this laptop reproduced `(981,801)` after warping to `(500,400)`
and moving `(-10,0)` with its 2× matrix; identity correctly produced `(490,400)`.
The ESP's direct square test was confirmed on both macOS and Android.

`tools/deskflow_trace_proxy.py` is an optional plain-TCP protocol diagnostic. It
forwards traffic and logs pointer positions/focus messages, without keyboard
contents. Its default listener is port 24801 and upstream is localhost:24800;
temporarily point the ESP's `port` to that listener, then restore 24800 afterward.

## Temporary persistent pointer diagnostics

Use `trace on` in the serial console to enable a bounded diagnostic ring in NVS.
It records the last 32 pointer/focus/USB diagnostic lines, without recording
keystrokes or passwords. It remains enabled across reboots so the ESP can be
moved to another USB host. Saves occur every three seconds when records change;
wait four seconds after testing before unplugging. Each boot's capture stops
automatically after ten minutes; `trace off` stops it earlier.

After reconnecting to the development machine, `trace dump` prints the retained
log. `trace clear` erases it. NVS is separate from the USB configuration drive;
firmware never writes diagnostic files into the mounted host filesystem.

For an independent USB motion check, `mouse-test` waits five seconds, then moves
an 80-pixel square without clicking. `mouse-test-next` arms that test once for
the next boot, allowing a different host to be tested. Pause Deskflow during the
test to avoid mixing server input with generated motion. The display shows when
the test runs. A test that never starts, together with `mounted=0`, indicates
that the USB host did not configure the device. Enumeration counters distinguish
host reset/setup activity from a connection supplying power alone. USB device
revision 2.00 identifies the current HID layout.

The combined HID interface advertises the generic report protocol,
with TinyUSB's standard keyboard and mouse descriptors.

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

## Wi-Fi and Deskflow

The USB configuration drive exposes approximately 13 MB of internal flash, separate
from firmware and private NVS settings. Open `CONFIG.INI`; each section names a
Wi-Fi SSID. Up to eight profiles are supported, tried in file order:

```ini
[Your WiFi SSID]
password = your-wifi-password
server = 192.168.1.10
port = 24800
screen = esp-bridge
width = 1920
height = 1080
layout = se
tls = true
server_fingerprint = YOUR_SERVER_CERTIFICATE_SHA256
enabled = true
```

Add `esp-bridge` to the Deskflow server's screen arrangement. The ESP initiates
an outgoing connection to that server, then forwards its keyboard and mouse
input to the computer attached by USB. Width and height describe that computer's
screen. `layout` accepts `us` (default) or `se`; match the target computer's keyboard
layout. Deskflow takes input focus when the server enters its screen; leaving it
releases remote input and restores the Bluetooth keyboard state.

Passwords are stored as plain text as requested. Empty passwords select an open
network. Values may be quoted; full-line comments start with `;` or `#`.
Unknown keys and invalid profiles reject the entire edit and retain the previous
configuration. `enabled=false` disables a profile without removing it.

TLS defaults to enabled and requires the SHA-256 fingerprint of the server's
certificate: 64 hexadecimal digits, optionally separated by colons. Obtain it
from Deskflow's certificate information, or from its certificate file:

```sh
openssl x509 -in server.pem -noout -fingerprint -sha256
```

For servers requiring client certificates, place a PEM certificate and its
unencrypted matching private key in the drive root, then add:

```ini
client_cert = CLIENT.CRT
client_key = CLIENT.KEY
```

Trust that client's certificate fingerprint on the Deskflow server. Both filenames
must use the 8.3 format; each file is limited to 4096 bytes. For a prototype
without encryption, set `tls=false` and disable encryption on the Deskflow server.

### Relative movement and locked switching

For targets with different screen sizes, enable Deskflow server settings
`relativeMouseMoves=true` and `defaultLockToComputerState=true`. Locked sessions
send relative movement directly, avoiding automatic exits at Deskflow's logical
screen boundaries. These are server settings; the ESP cannot force them through
the client protocol.

The development laptop is configured with these shortcuts:

- **Ctrl+Alt+Shift+1**: unlock, switch to `esp-bridge`, then lock.
- **Ctrl+Alt+Shift+2**: unlock, switch to `arch-dual256`, then lock.
- **Scroll Lock**: toggle locking on the current computer.

Deskflow starts locked to the laptop; use the shortcuts to switch computers.
The settings are saved in `~/.config/Deskflow/Deskflow.conf` and are visible in
the GUI. Keyboard and mouse support in firmware handles both relative packets
and ordinary Deskflow absolute coordinates.

### Live configuration

Saving while the drive remains mounted triggers a reload after USB writes settle
(typically two to three seconds after the operating system flushes them). Use
`sync` if the host caches writes. Firmware reads consistent, bounded FAT snapshots;
it does not mount or modify the filesystem alongside the host.

Only affected services restart: changing Deskflow settings or certificate files
reconnects Deskflow; changing the active Wi-Fi credentials reconnects Wi-Fi.
Unchanged settings do not cause reconnections. Invalid saves and transient empty
files keep the last accepted configuration, also saved in NVS for recovery after
reboot. To disable the final profile, use `enabled=false` rather than deleting
all sections. Console commands `network` and `reload` show status and request a
fresh read. `restart` reboots the bridge, releasing a captured pointer. The display
also shows network status. Motion diagnostics in the serial log report received
absolute/relative Deskflow packets, ignored packets with focus/acknowledgement
state, and successfully queued USB movements. These are rate limited to one log
per two seconds during motion.

The Wi-Fi manager owns AP selection, credentials and DHCP. Deskflow session,
protocol framing, socket/TLS transport, and certificate authentication have separate
modules. Wi-Fi and Deskflow configuration structs remain separate even though
the INI parser combines them under each SSID.

Deskflow support includes protocol 1.1–1.8 framing, keyboard press/release,
absolute/relative Deskflow mouse motion converted to a relative USB mouse,
five buttons, scrolling, heartbeat and disconnect
cleanup. Clipboard, file transfer, media keys and IME synchronization are not
forwarded. The USB mouse uses conventional signed 8-bit X/Y fields; larger movements are
split into multiple reports without duplicating scroll input. Android cursor
compatibility still requires hardware confirmation. Host pointer
acceleration can make its position differ from Deskflow’s logical screen position;
entering the screen establishes a new movement baseline without moving the cursor.
Deskflow sessions use turquoise; Wi-Fi without Deskflow uses orange.

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

The current composite firmware was built and flashed to the connected ESP.
Linux enumerated keyboard, mouse, CDC and mass storage (`303a:4007`), mounted the
configuration drive, and received console status replies. A valid configuration
save applied while mounted; an invalid edit retained the previous settings.
Wi-Fi and Deskflow keyboard, mouse, scrolling and key combinations were verified
on the original USB-A stick with macOS and Android targets. The server’s Muffin
coordinate-transform issue was reproduced and worked around as described above.
TLS interoperability with a real server still requires validation.

The 1.47B profile was built and flashed. Its USB interfaces, display and onboard
accelerometer were verified, and the user confirmed upright automatic rotation.
Its full 16 MB factory flash backup was hashed and checked. Wi-Fi association
succeeded, but stable Deskflow connectivity on this second board remains unverified.

Native INI and Deskflow tests cover configuration validation, fragmented frames,
handshakes, keyboard/mouse events, heartbeat and release on disconnect:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain \
  main/network_config.c tests/network_config_test.c -o /tmp/network-config-test
/tmp/network-config-test
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain \
  main/deskflow_protocol.c main/deskflow_keys.c tests/deskflow_protocol_test.c \
  -o /tmp/deskflow-protocol-test
/tmp/deskflow-protocol-test
```

FAT snapshot tests were exercised with generated FAT12, FAT16, FAT32 and
MBR-partitioned images. Existing keyboard, orientation, screen and LED native
tests also pass, alongside the ten Python console tests.


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
