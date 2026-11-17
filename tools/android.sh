#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
python="${LEM_PYTHON:-python3}"
if ! command -v "$python" >/dev/null 2>&1 || ! "$python" -c 'import sys; sys.exit(sys.version_info < (3, 10))'; then
    for arg in "$@"; do
        if [[ "$arg" == --no-bootstrap ]]; then
            echo 'Python 3.10+ is missing. Install it or set LEM_PYTHON.' >&2
            exit 1
        fi
    done
    if [[ ! -f /etc/os-release ]] || ! grep -q '^ID=ubuntu$' /etc/os-release; then
        echo 'Automatic dependency setup requires Ubuntu 22.04+.' >&2
        exit 1
    fi
    elevate=()
    if (( EUID != 0 )); then elevate=(sudo); fi
    "${elevate[@]}" apt-get update
    "${elevate[@]}" apt-get install -y python3 python3-venv ca-certificates
    python=python3
fi
exec "$python" "$script_dir/android.py" "$@"
