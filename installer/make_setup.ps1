<#
.SYNOPSIS
  Stages the files for MsfsCydHud-Setup.exe and compiles it with Inno Setup.

.EXAMPLE
  powershell -File installer\make_setup.ps1 -Firmware build\ship_hud.ino.merged.bin
#>
[CmdletBinding()]
param(
  # Merged flash image built from ship_hud (arduino-cli --build-path ... ship_hud.ino.merged.bin)
  [Parameter(Mandatory = $true)][string]$Firmware,
  # Where Setup.exe is written
  [string]$OutDir = 'build',
  # Inno Setup compiler; installed with Chocolatey if missing
  [string]$Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root 'build\setup-stage'
$esptoolUrl = 'https://github.com/espressif/esptool/releases/download/v4.9.0/esptool-v4.9.0-windows-amd64.zip'

$version = (Select-String -Path (Join-Path $root 'ship_hud\ship_hud.ino') -Pattern '#define\s+HUD_FW_VERSION\s+"([^"]+)"').Matches[0].Groups[1].Value
Write-Host "Staging MSFS CYD HUD $version"

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# What the guided setup needs: itself, the sketch (version, fallback build),
# the display configuration, the sender sources and the ready-made firmware
Copy-Item (Join-Path $root 'installer') (Join-Path $stage 'installer') -Recurse
Remove-Item (Join-Path $stage 'installer\make_setup.ps1'), (Join-Path $stage 'installer\MsfsCydHud.iss') -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root 'ship_hud') (Join-Path $stage 'ship_hud') -Recurse
Remove-Item (Join-Path $stage 'ship_hud\wifi_config.h') -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root 'config') (Join-Path $stage 'config') -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'msfs-sender') | Out-Null
Copy-Item (Join-Path $root 'msfs-sender\MsfsHudSender') (Join-Path $stage 'msfs-sender\MsfsHudSender') -Recurse
foreach ($d in 'bin', 'obj') {
  $p = Join-Path $stage "msfs-sender\MsfsHudSender\$d"
  if (Test-Path $p) { Remove-Item $p -Recurse -Force }
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'firmware') | Out-Null
Copy-Item $Firmware (Join-Path $stage 'firmware\msfs-cyd-hud.bin')
foreach ($f in 'README.md', 'LICENSE') { Copy-Item (Join-Path $root $f) $stage }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'docs') | Out-Null
Copy-Item (Join-Path $root 'docs\*.md') (Join-Path $stage 'docs')
"Installed by MsfsCydHud-Setup.exe $version" | Set-Content (Join-Path $stage 'installed-by-setup.txt')

# esptool flashes the firmware; GPL-2.0, shipped with its licence
$zip = Join-Path $env:TEMP 'esptool.zip'
Invoke-WebRequest -Uri $esptoolUrl -OutFile $zip -UseBasicParsing
$unz = Join-Path $env:TEMP 'esptool-unzip'
if (Test-Path $unz) { Remove-Item $unz -Recurse -Force }
Expand-Archive $zip $unz
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'tools\esptool') | Out-Null
foreach ($f in 'esptool.exe', 'LICENSE', 'README.md') {
  Copy-Item (Join-Path $unz "esptool-windows-amd64\$f") (Join-Path $stage 'tools\esptool')
}

if (-not (Test-Path $Iscc)) {
  Write-Host 'Installing Inno Setup'
  choco install innosetup -y --no-progress | Out-Null
}
$out = Join-Path $root $OutDir
& $Iscc "/DAppVersion=$version" "/DStage=$stage" "/DOutDir=$out" (Join-Path $PSScriptRoot 'MsfsCydHud.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)" }
Write-Host "Built $(Join-Path $out 'MsfsCydHud-Setup.exe')"
