# One-line install without git:
#
#   irm https://raw.githubusercontent.com/florianbaer/msfs24-cyd-hud/main/installer/bootstrap.ps1 | iex
#
# Downloads the current sources to %LOCALAPPDATA%\MsfsCydHud\src and starts the installer.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$zipUrl = 'https://github.com/florianbaer/msfs24-cyd-hud/archive/refs/heads/main.zip'
$root = Join-Path $env:LOCALAPPDATA 'MsfsCydHud'
$src = Join-Path $root 'src'
$zip = Join-Path $env:TEMP 'msfs24-cyd-hud.zip'

Write-Host 'Downloading MSFS CYD HUD...' -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $root | Out-Null
Invoke-WebRequest -Uri $zipUrl -OutFile $zip -UseBasicParsing
if (Test-Path $src) { Remove-Item $src -Recurse -Force }
$tmp = Join-Path $root 'src-tmp'
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
Expand-Archive -Path $zip -DestinationPath $tmp -Force
Move-Item (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName $src
Remove-Item $tmp, $zip -Recurse -Force

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $src 'installer\install.ps1')
