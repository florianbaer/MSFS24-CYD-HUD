# One-line install:
#
#   irm https://raw.githubusercontent.com/florianbaer/msfs24-cyd-hud/main/installer/bootstrap.ps1 | iex
#
# Downloads and runs MsfsCydHud-Setup.exe from the latest release. Before the
# first release exists it falls back to the sources, whose installer builds the
# firmware itself.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$repo = 'https://github.com/florianbaer/msfs24-cyd-hud'
$setup = Join-Path $env:TEMP 'MsfsCydHud-Setup.exe'

Write-Host 'Downloading MSFS CYD HUD...' -ForegroundColor Cyan
try {
  Invoke-WebRequest -Uri "$repo/releases/latest/download/MsfsCydHud-Setup.exe" -OutFile $setup -UseBasicParsing
  Start-Process -FilePath $setup -Wait
  return
} catch {
  Write-Host 'No release with Setup.exe yet - installing from the sources instead.' -ForegroundColor Yellow
}

$root = Join-Path $env:LOCALAPPDATA 'MsfsCydHud'
$src = Join-Path $root 'src'
$zip = Join-Path $env:TEMP 'msfs24-cyd-hud.zip'
New-Item -ItemType Directory -Force -Path $root | Out-Null
Invoke-WebRequest -Uri "$repo/archive/refs/heads/main.zip" -OutFile $zip -UseBasicParsing
if (Test-Path $src) { Remove-Item $src -Recurse -Force }
$tmp = Join-Path $root 'src-tmp'
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
Expand-Archive -Path $zip -DestinationPath $tmp -Force
Move-Item (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName $src
Remove-Item $tmp, $zip -Recurse -Force

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $src 'installer\install.ps1')
