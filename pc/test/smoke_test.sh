#!/usr/bin/env bash
# Linux smoke test: selftest-ui + 100 synthetic JPEG frames -> recording + OSD.
# Counterpart of smoke_test.ps1 (which needs PowerShell on Windows).
#
#   ./pc/test/smoke_test.sh
#
# Requires: pc/build/camlink, python3 with Pillow.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exe="$(dirname "$here")/build/camlink"   # pc/test -> pc/build/camlink

if [[ ! -x "$exe" ]]; then
    echo "smoke test: $exe not found — build it first:" >&2
    echo "  cmake -S pc -B pc/build -DCMAKE_BUILD_TYPE=Release && cmake --build pc/build -j" >&2
    exit 1
fi

exec python3 "$here/smoke_test.py"
