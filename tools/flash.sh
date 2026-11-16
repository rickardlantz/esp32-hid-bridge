#!/usr/bin/env bash
set -euo pipefail
bridge_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$bridge_root"
if ! command -v idf.py >/dev/null 2>&1; then
    # EIM's generated script refuses to be sourced from a named shell script.
    # Its -e mode provides key=value lines; import them without evaluating code.
    while IFS= read -r bridge_env; do
        case "$bridge_env" in
            PATH=*) export PATH="${bridge_env#PATH=}:$PATH" ;;
            SYSTEM_PATH=*) ;;
            *=*) export "$bridge_env" ;;
        esac
    done < <(bash "$HOME/.espressif/tools/activate_idf_v6.1.sh" -e)
fi
bridge_idf_args=()
if [[ $# -gt 0 ]]; then
    if [[ $# -ne 2 || "$1" != "--board" ]]; then
        echo "Usage: bash tools/flash.sh [--board lcd-147|lcd-147b]" >&2
        exit 2
    fi
    case "$2" in
        lcd-147) ;;
        lcd-147b)
            mkdir -p "$bridge_root/build/lcd-147b"
            bridge_idf_args=(-B "$bridge_root/build/lcd-147b"
                -D "SDKCONFIG=$bridge_root/build/lcd-147b/sdkconfig"
                -D "SDKCONFIG_DEFAULTS=$bridge_root/sdkconfig.defaults;$bridge_root/sdkconfig.board-147b.defaults")
            ;;
        *) echo "Unknown board: $2" >&2; exit 2 ;;
    esac
fi
idf.py "${bridge_idf_args[@]}" build
bridge_port=$(python "$bridge_root/tools/enter_bootloader.py")
idf.py "${bridge_idf_args[@]}" -p "$bridge_port" flash
# Some S3 revisions remain in ROM after the USB Serial/JTAG RTS reset.
# A watchdog reset exits that state without unplugging the USB stick.
python - <<'PY'
import subprocess
import sys
import time
from serial.tools import list_ports
time.sleep(1)
ports = [p for p in list_ports.comports() if p.vid == 0x303A and p.pid == 0x1001]
if len(ports) == 1:
    subprocess.run([sys.executable, '-m', 'esptool', '--port', ports[0].device,
                    '--after', 'watchdog-reset', 'chip-id'], check=True)
elif len(ports) > 1:
    raise SystemExit('Multiple ESP ROM ports found; select the intended device manually.')
PY
