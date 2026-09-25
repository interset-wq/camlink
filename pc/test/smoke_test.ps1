# Smoke test: run camlink.exe and feed it frames like the phone would.
$ErrorActionPreference = "Stop"
$pcDir = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $pcDir
$tmp = Join-Path $repo "temp"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# 1. Build a test JPEG
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap 320, 240
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::CornflowerBlue)
$brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::Orange)
$g.FillEllipse($brush, 60, 40, 200, 160)
$g.Dispose()
$jpg = Join-Path $tmp "test.jpg"
$bmp.Save($jpg, [System.Drawing.Imaging.ImageFormat]::Jpeg)
$bmp.Dispose()
Write-Host "test jpeg: $((Get-Item $jpg).Length) bytes"

# 2. Start camlink
$exe = Join-Path $repo "pc\build\camlink.exe"
$out = Join-Path $tmp "camlink_out.txt"
$err = Join-Path $tmp "camlink_err.txt"
$dump = Join-Path $tmp "frame.ppm"
$rec = Join-Path $tmp "smoke.avi"
Remove-Item $out, $err, $dump, $rec -ErrorAction SilentlyContinue

# 0. UI logic self-test (pure functions: record/snapshot paths, toasts)
$selfOut = & $exe --selftest-ui | Out-String
$selfExit = $LASTEXITCODE
Write-Host "--- selftest-ui ---"
Write-Host $selfOut.Trim()
if ($selfExit -ne 0 -or $selfOut -notmatch 'SELFTEST-UI: PASS') {
    Write-Host "SELFTEST-UI: FAIL (exit=$selfExit)"
    exit 1
}

$proc = Start-Process -FilePath $exe `
    -ArgumentList "--no-adb", "--duration", "8", "--dump-frame", $dump, "--record", $rec `
    -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
Start-Sleep -Seconds 1

# 3. Send 100 frames (protocol: 4-byte big-endian length + JPEG)
$data = [IO.File]::ReadAllBytes($jpg)
$len = [BitConverter]::GetBytes([uint32]$data.Length)
[Array]::Reverse($len)

$client = New-Object Net.Sockets.TcpClient("127.0.0.1", 5555)
$stream = $client.GetStream()
for ($i = 0; $i -lt 100; $i++) {
    $stream.Write($len, 0, 4)
    $stream.Write($data, 0, $data.Length)
    $stream.Flush()
    Start-Sleep -Milliseconds 33
}
$client.Close()
Write-Host "sent 100 frames"

Wait-Process -Id $proc.Id -Timeout 15

# 4. Verify
function Count-WhitePixels([string]$ppmPath) {
    $bytes = [IO.File]::ReadAllBytes($ppmPath)
    $ascii = [Text.Encoding]::ASCII.GetString($bytes[0..([Math]::Min(63, $bytes.Length - 1))])
    if ($ascii -notmatch 'P6\n(\d+) (\d+)\n255\n') { return -1 }
    $w = [int]$Matches[1]; $h = [int]$Matches[2]
    $hdr = $Matches[0].Length
    $count = 0
    $x0 = [Math]::Max(0, $w - 150)
    $y0 = [Math]::Max(0, $h - 50)
    for ($y = $y0; $y -lt $h - 5; $y++) {
        for ($x = $x0; $x -lt $w - 5; $x++) {
            $i = $hdr + ($y * $w + $x) * 3
            if ($bytes[$i] -gt 200 -and $bytes[$i + 1] -gt 200 -and $bytes[$i + 2] -gt 200) { $count++ }
        }
    }
    return $count
}

Write-Host "--- camlink stdout ---"
Get-Content $out
Write-Host "--- camlink stderr ---"
Get-Content $err
if (Test-Path $dump) {
    $bytes = [IO.File]::ReadAllBytes($dump)
    $header = [Text.Encoding]::ASCII.GetString($bytes[0..([Math]::Min(15, $bytes.Length - 1))])
    Write-Host "dump exists: $($bytes.Length) bytes, header: $header"

    # OSD assertion: burned-in overlay must contain white text pixels
    # in the bottom-right region (input JPEG is solid blue/orange, no white).
    $white = Count-WhitePixels $dump
    if ($white -lt 5) {
        Write-Host "OSD TEST: FAIL (no overlay pixels in dump, white=$white)"
        exit 1
    }
    Write-Host "OSD TEST: dump has overlay (white pixels=$white)"

    if (Test-Path $rec) {
        Write-Host "recording: $((Get-Item $rec).Length) bytes"
        $ff = Get-Command ffprobe -ErrorAction SilentlyContinue
        if ($ff) {
            Write-Host "--- ffprobe ---"
            & $ff.Source -v error -select_streams v:0 `
                -show_entries stream=codec_name,width,height,nb_frames:format=duration `
                -of default=nw=1 $rec
        }

        # OSD in the recording itself: extract first frame and check overlay.
        $ffm = Get-Command ffmpeg -ErrorAction SilentlyContinue
        if ($ffm) {
            $recFrame = Join-Path $tmp "smoke_rec_frame.png"
            Remove-Item $recFrame -ErrorAction SilentlyContinue
            & $ffm.Source -y -v error -i $rec -vframes 1 $recFrame
            if (Test-Path $recFrame) {
                $img = [System.Drawing.Image]::FromFile($recFrame)
                $bmp = New-Object System.Drawing.Bitmap $img
                $recWhite = 0
                $rx0 = [Math]::Max(0, $bmp.Width - 150)
                $ry0 = [Math]::Max(0, $bmp.Height - 50)
                for ($y = $ry0; $y -lt $bmp.Height - 5; $y++) {
                    for ($x = $rx0; $x -lt $bmp.Width - 5; $x++) {
                        $c = $bmp.GetPixel($x, $y)
                        if ($c.R -gt 200 -and $c.G -gt 200 -and $c.B -gt 200) { $recWhite++ }
                    }
                }
                $bmp.Dispose(); $img.Dispose()
                if ($recWhite -lt 5) {
                    Write-Host "OSD TEST: FAIL (no overlay pixels in recording, white=$recWhite)"
                    exit 1
                }
                Write-Host "OSD TEST: recording has overlay (white pixels=$recWhite)"
            }
        }

        Write-Host "SMOKE TEST: PASS"
    } else {
        Write-Host "SMOKE TEST: FAIL (no recording file)"
        exit 1
    }
} else {
    Write-Host "SMOKE TEST: FAIL (no dump file)"
    exit 1
}
