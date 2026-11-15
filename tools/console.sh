#!/usr/bin/env bash
set -euo pipefail
bridge_python="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/tools/python/v6.1/venv}/bin/python"
if [[ ! -x "$bridge_python" ]]; then bridge_python=python3; fi
exec "$bridge_python" "$(dirname -- "${BASH_SOURCE[0]}")/console.py" "$@"
