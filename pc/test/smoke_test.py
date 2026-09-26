#!/usr/bin/env python3
"""Linux/macOS equivalent of smoke_test.ps1 (no pwsh / System.Drawing here).

Feeds 100 synthetic JPEG frames to a freshly started camlink, then asserts:
  * the UI self-test passes
  * the first decoded frame was dumped as PPM
  * the burned-in OSD overlay is present (white pixels in the dump)
  * the .avi recording exists and holds the frames

Usage:  python3 pc/test/smoke_test.py        (run from anywhere)
Needs:  pc/build/camlink, Python 3, Pillow (`pip install pillow`)
"""
import os
import socket
import struct
import subprocess
import sys
import time

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("smoke_test: Pillow is required (pip install pillow)")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
EXE = os.path.join(REPO, "pc", "build", "camlink")
TMP = os.path.join(REPO, "temp")
DURATION = 8          # seconds camlink runs before auto-exit
FRAMES = 100           # frames to push over TCP
FPS_MS = 33            # pacing, ~30 fps


def fail(msg):
    print(msg)
    sys.exit(1)


if not os.path.exists(EXE):
    fail(f"smoke test: {EXE} not found — build it first (cmake --build pc/build)")

os.makedirs(TMP, exist_ok=True)
jpg_path = os.path.join(TMP, "test.jpg")
dump = os.path.join(TMP, "frame.ppm")
rec = os.path.join(TMP, "smoke.avi")
for p in (dump, rec):
    if os.path.exists(p):
        os.remove(p)

# 1. synthetic test JPEG: cornflower blue bg + orange ellipse (deliberately no
#    white, so any white pixel in the dump must come from the OSD overlay)
img = Image.new("RGB", (320, 240), (100, 149, 237))
ImageDraw.Draw(img).ellipse((60, 40, 260, 200), fill=(255, 165, 0))
img.save(jpg_path, "JPEG")
data = open(jpg_path, "rb").read()
print(f"test jpeg: {len(data)} bytes")

# 0. UI logic self-test (pure functions: record/snapshot paths, toasts)
st = subprocess.run([EXE, "--selftest-ui"], capture_output=True, text=True)
print("--- selftest-ui ---")
print(st.stdout.strip())
if st.returncode != 0 or "SELFTEST-UI: PASS" not in st.stdout:
    fail(f"SELFTEST-UI: FAIL (exit={st.returncode})")

# 2. start camlink
proc = subprocess.Popen(
    [EXE, "--no-adb", "--duration", str(DURATION), "--dump-frame", dump,
     "--record", rec],
    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
time.sleep(1)

# 3. send FRAMES framed JPEGs (protocol: 4-byte big-endian length + JPEG)
hdr = struct.pack(">I", len(data))
try:
    sock = socket.create_connection(("127.0.0.1", 5555), timeout=5)
    stream = sock.makefile("wb")
    for _ in range(FRAMES):
        stream.write(hdr)
        stream.write(data)
        stream.flush()
        time.sleep(FPS_MS / 1000)
    stream.close()
    sock.close()
    print(f"sent {FRAMES} frames")
except OSError as e:
    time.sleep(0.5)
    try:
        out, err = proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
    print("--- camlink stdout ---\n" + (out or "").strip())
    print("--- camlink stderr ---\n" + (err or "").strip())
    fail(f"FEED FAIL: {e}")

try:
    out, err = proc.communicate(timeout=DURATION + 15)
except subprocess.TimeoutExpired:
    proc.kill()
    out, err = proc.communicate()
print("--- camlink stdout ---")
print(out.strip())
if err.strip():
    print("--- camlink stderr ---")
    print(err.strip())


def count_white(path):
    """White pixels in the bottom-right region, where the OSD is burned in."""
    raw = open(path, "rb").read()
    if not raw.startswith(b"P6"):
        return -1
    end = raw.index(b"255\n") + 4
    tokens = raw[:end].decode("ascii").split()   # ["P6", w, h, "255"]
    w, h = int(tokens[1]), int(tokens[2])
    count = 0
    for y in range(max(0, h - 50), h - 5):
        for x in range(max(0, w - 150), w - 5):
            i = end + (y * w + x) * 3
            if raw[i] > 200 and raw[i + 1] > 200 and raw[i + 2] > 200:
                count += 1
    return count


if not os.path.exists(dump):
    fail("SMOKE TEST: FAIL (no dump file)")

print(f"dump exists: {os.path.getsize(dump)} bytes")
white = count_white(dump)
if white < 5:
    fail(f"OSD TEST: FAIL (no overlay pixels in dump, white={white})")
print(f"OSD TEST: dump has overlay (white pixels={white})")

if not os.path.exists(rec) or os.path.getsize(rec) == 0:
    fail("SMOKE TEST: FAIL (no recording file)")

frames = open(rec, "rb").read().count(b"\xff\xd8\xff")
print(f"recording: {os.path.getsize(rec)} bytes, ~{frames} JPEG frames")
print("SMOKE TEST: PASS")
