#!/usr/bin/env bash
# Linux end-to-end test: real phone camera -> adb reverse -> camlink -> PPM dump.
# Counterpart of e2e_test.ps1 (which needs PowerShell on Windows).
#
#   ./pc/test/e2e_test.sh
#
# Requires: phone authorized in `adb devices`, app installed
# (cd android && ./gradlew installDebug), pc/build/camlink.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pc="$(dirname "$here")"
repo="$(dirname "$pc")"
exe="$pc/build/camlink"
tmp="$repo/temp"
dump="$tmp/e2e_frame.ppm"
rec="$tmp/camlink_e2e.mp4"
out="$tmp/e2e_out.txt"
err="$tmp/e2e_err.txt"
pkg=com.intersetwq.camlink

mkdir -p "$tmp"
rm -f "$dump" "$rec" "$rec".avi "$out" "$err"

adb_bin="${ADB:-}"
if [[ -z "$adb_bin" ]]; then
    if command -v adb >/dev/null 2>&1; then
        adb_bin=adb
    elif [[ -x "$HOME/Android/Sdk/platform-tools/adb" ]]; then
        adb_bin="$HOME/Android/Sdk/platform-tools/adb"
    else
        echo "E2E TEST: FAIL (adb not found; set ADB=/path/to/adb)" >&2
        exit 1
    fi
fi

if [[ ! -x "$exe" ]]; then
    echo "E2E TEST: FAIL ($exe not found — build it first)" >&2
    exit 1
fi
if "$adb_bin" devices 2>/dev/null | grep -q 'unauthorized'; then
    echo "E2E TEST: FAIL (device unauthorized — accept the USB debugging prompt)" >&2
    exit 1
fi

adb="$adb_bin"

# 0. reset the app so a stale session cannot interfere
"$adb" shell am force-stop "$pkg" >/dev/null 2>&1
"$adb" shell pm grant "$pkg" android.permission.CAMERA >/dev/null 2>&1

# 1. start the PC client (30s window, recording the stream)
"$exe" --duration 30 --dump-frame "$dump" --record "$rec" >"$out" 2>"$err" &
pid=$!
sleep 2
if ! kill -0 "$pid" 2>/dev/null; then
    echo "camlink exited early:"
    cat "$out" "$err"
    echo "E2E TEST: FAIL"
    exit 1
fi

# 2. launch the app on the phone
"$adb" shell am start -n "$pkg/.MainActivity" >/dev/null
sleep 5

# 3. find and tap the Start button via UI automator
"$adb" shell uiautomator dump /sdcard/ui.xml >/dev/null 2>&1
xml="$("$adb" shell cat /sdcard/ui.xml 2>/dev/null)"
if [[ "$xml" != *'text="START"'* ]]; then
    echo "Start button not found in UI dump:"
    echo "${xml:0:1500}"
else
    # bounds="[x1,y1][x2,y2]" on the node whose text is "START"
    node="$(grep -o '<node[^>]*text="START"[^>]*>' <<<"$xml" | head -1)"
    bounds="$(grep -o '\[[0-9]\+,[0-9]\+\]\[[0-9]\+,[0-9]\+\]' <<<"$node" | head -1)"
    if [[ "$bounds" =~ \[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\] ]]; then
        cx=$(( (BASH_REMATCH[1] + BASH_REMATCH[3]) / 2 ))
        cy=$(( (BASH_REMATCH[2] + BASH_REMATCH[4]) / 2 ))
        echo "tapping Start at $cx,$cy"
        "$adb" shell input tap "$cx" "$cy"
    else
        echo "no bounds for Start button in UI dump"
    fi
fi

# 4. let the stream run
sleep 12

# 5. tear down the phone side
"$adb" shell am force-stop "$pkg" >/dev/null 2>&1
wait "$pid" 2>/dev/null

echo "--- camlink stdout ---"
cat "$out"
if [[ -s "$err" ]]; then
    echo "--- camlink stderr ---"
    cat "$err"
fi

if [[ -f "$dump" ]] && grep -qE 'Frames shown: [1-9][0-9]*' "$out"; then
    frames="$(grep -oE 'Frames shown: [0-9]+' "$out" | head -1 | grep -oE '[0-9]+')"
    produced=""
    if [[ -f "$rec" ]]; then
        produced="$rec"
    elif [[ -f "${rec%.mp4}.avi" ]]; then
        # camlink keeps the native MJPEG .avi when ffmpeg is unavailable
        produced="${rec%.mp4}.avi"
        echo "note: ffmpeg not installed, camlink kept the native MJPEG recording"
    fi
    if [[ -n "$produced" ]]; then
        echo "recording: $produced ($(du -m "$produced" | cut -f1) MB)"
        if command -v ffprobe >/dev/null 2>&1; then
            echo "--- ffprobe ---"
            ffprobe -v error -select_streams v:0 \
                -show_entries stream=codec_name,width,height,nb_frames:format=duration \
                -of default=nw=1 "$produced"
        fi
        echo "E2E TEST: PASS (frames=$frames, recording=$produced)"
    else
        echo "E2E TEST: FAIL (no recording produced)"
        exit 1
    fi
else
    echo "E2E TEST: FAIL"
    exit 1
fi
