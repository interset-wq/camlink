# End-to-end test: real phone camera -> adb reverse -> camlink.exe -> PPM dump.
$ErrorActionPreference = "Continue"
$pcDir = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $pcDir
$tmp = Join-Path $repo "temp"
$out = Join-Path $tmp "e2e_out.txt"
$err = Join-Path $tmp "e2e_err.txt"
$dump = Join-Path $tmp "e2e_frame.ppm"
$rec = Join-Path $tmp "camlink_e2e.mp4"
Remove-Item $out, $err, $dump, $rec, (Join-Path $tmp "camlink_e2e.avi") -ErrorAction SilentlyContinue

adb shell am force-stop com.intersetwq.camlink
adb shell pm grant com.intersetwq.camlink android.permission.CAMERA 2>$null

# 1. Start PC client (30s window, recording the stream)
$exe = Join-Path $repo "pc\build\camlink.exe"
$proc = Start-Process -FilePath $exe `
    -ArgumentList "--duration", "30", "--dump-frame", $dump, "--record", $rec `
    -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
Start-Sleep -Seconds 2
if ($proc.HasExited) {
    Write-Host "camlink exited early:"; Get-Content $out, $err
    exit 1
}

# 2. Launch app on phone
adb shell am start -n com.intersetwq.camlink/.MainActivity | Out-Null
Start-Sleep -Seconds 5

# 3. Find and tap the Start button via UI automator
adb shell uiautomator dump /sdcard/ui.xml 2>&1 | Out-Null
$xml = adb shell cat /sdcard/ui.xml 2>$null
if (-not $xml) { $xml = ($xml -join "`n") }
$tag = [regex]::Match(($xml -join "`n"), '<node[^>]*text="START"[^>]*>')
if (-not $tag.Success) {
    Write-Host "Start button not found in UI dump:"
    Write-Host (($xml -join "`n").Substring(0, [Math]::Min(1500, ($xml -join "`n").Length)))
} else {
    $b = [regex]::Match($tag.Value, 'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"')
    $x1 = [int]$b.Groups[1].Value; $y1 = [int]$b.Groups[2].Value
    $x2 = [int]$b.Groups[3].Value; $y2 = [int]$b.Groups[4].Value
    $cx = ($x1 + $x2) / 2; $cy = ($y1 + $y2) / 2
    Write-Host "tapping Start at $cx,$cy"
    adb shell input tap $cx $cy
}

# 4. Let the stream run
Start-Sleep -Seconds 12

# 5. Teardown phone side
adb shell am force-stop com.intersetwq.camlink

Wait-Process -Id $proc.Id -Timeout 45 -ErrorAction SilentlyContinue

Write-Host "--- camlink stdout ---"
Get-Content $out -ErrorAction SilentlyContinue
Write-Host "--- camlink stderr ---"
Get-Content $err -ErrorAction SilentlyContinue

if ((Test-Path $dump) -and ((Get-Content $out -Raw) -match 'Frames shown: ([1-9]\d*)')) {
    $frames = $Matches[1]
    if (Test-Path $rec) {
        Write-Host "recording: $([Math]::Round((Get-Item $rec).Length / 1MB, 1)) MB"
        $ff = Get-Command ffprobe -ErrorAction SilentlyContinue
        if ($ff) {
            Write-Host "--- ffprobe ---"
            & $ff.Source -v error -select_streams v:0 `
                -show_entries stream=codec_name,width,height,nb_frames:format=duration `
                -of default=nw=1 $rec
        }
        Write-Host "E2E TEST: PASS (frames=$frames, recording=$rec)"
    } else {
        Write-Host "E2E TEST: FAIL (no recording produced)"
        exit 1
    }
} else {
    Write-Host "E2E TEST: FAIL"
    exit 1
}
