# Downloads PC client build dependencies into pc/third_party/.
# Usage: pwsh -File pc/setup_deps.ps1 [-Sdl2Version 2.32.10]
param(
    [string]$Sdl2Version = "2.32.10"
)

$ErrorActionPreference = "Stop"

$pcDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoDir = Split-Path -Parent $pcDir
$thirdParty = Join-Path $pcDir "third_party"
$sdlDir = Join-Path $thirdParty "SDL2"
$tempDir = Join-Path $repoDir "temp"

New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

if (Test-Path (Join-Path $sdlDir "cmake/sdl2-config.cmake")) {
    Write-Host "SDL2 already present at $sdlDir"
    exit 0
}

$url = "https://github.com/libsdl-org/SDL/releases/download/release-$Sdl2Version/SDL2-devel-$Sdl2Version-mingw.zip"
$zip = Join-Path $tempDir "SDL2-devel-mingw.zip"
Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

$extractDir = Join-Path $tempDir "sdl2_extract"
if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
Expand-Archive -Path $zip -DestinationPath $extractDir

$inner = Get-ChildItem $extractDir | Select-Object -First 1
if (Test-Path $sdlDir) { Remove-Item -Recurse -Force $sdlDir }
Move-Item $inner.FullName $sdlDir

Remove-Item -Recurse -Force $extractDir
Remove-Item -Force $zip

Write-Host "SDL2 $Sdl2Version installed to $sdlDir"
