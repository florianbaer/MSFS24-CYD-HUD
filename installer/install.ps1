<#
.SYNOPSIS
  One-stop installer for the MSFS 2024 CYD HUD.

.DESCRIPTION
  Walks through everything needed to go from a fresh ESP32-2432S024C and a
  Windows PC with MSFS 2024 to a working HUD:

    1. finds the display on USB (and helps when the driver is missing)
    2. asks for USB or WiFi (and the WiFi credentials, which stay on this PC)
    3. flashes the firmware (the ready-made image Setup.exe ships, or one it
       builds with a private Arduino toolchain when run from the sources) and
       sends the WiFi settings to the display over USB
    4. installs the .NET SDK if needed, finds the MSFS SDK and builds the sender
    5. runs a short display test with synthetic flight data
    6. registers the sender in MSFS's exe.xml so it starts with the simulator,
       and adds Start-menu shortcuts and an "Apps & features" entry

  Nothing is written outside %LOCALAPPDATA%\MsfsCydHud, the Start menu, the
  per-user uninstall registry key and the MSFS exe.xml (which is backed up).
  Your own Arduino IDE installation and libraries are never touched.

  Re-run it at any time to re-flash, change USB/WiFi or update the sender:
  the previous answers are offered as defaults.

.EXAMPLE
  .\Install.cmd
  Interactive install (double-click Install.cmd in the repository root).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File installer\install.ps1 -Yes
  Accept every default (USB, auto-detected port, auto-start with MSFS).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File installer\install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
  # Remove everything the installer added.
  [switch]$Uninstall,
  # Accept the default answer to every question.
  [switch]$Yes,
  # Only (re)build and install the sender.
  [switch]$SkipFirmware,
  # Only (re)flash the display.
  [switch]$SkipSender,
  # Serial port of the display, e.g. COM6. Detected automatically if omitted.
  [string]$Port,
  # How the sender talks to the display.
  [ValidateSet('Usb', 'Wifi')]
  [string]$Connection,
  [string]$WifiSsid,
  # Folder of the MSFS 2024 SDK (the one that contains "SimConnect SDK").
  [string]$MsfsSdk,
  # Do not register the sender in MSFS's exe.xml.
  [switch]$NoAutoStart
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is 10x slower with the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

$AppName      = 'MSFS CYD HUD'
$AppId        = 'MsfsCydHud'
$AddonName    = 'ESP32 HUD Display'          # <Name> of our entry in exe.xml
$SenderExe    = 'msfs-hud-sender.exe'

$Esp32Index   = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
$Esp32Core    = 'esp32:esp32@3.3.12'
$ArduinoLibs  = @('lvgl@9.2.2', 'TFT_eSPI@2.5.43')
# Minimal SPIFFS gives the app 1.9 MB; the default 1.3 MB is ~99% full with WiFi.
$Fqbn         = 'esp32:esp32:esp32:PartitionScheme=min_spiffs'
$ArduinoCliUrl = 'https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip'
$EsptoolUrl   = 'https://github.com/espressif/esptool/releases/download/v4.9.0/esptool-v4.9.0-windows-amd64.zip'
$DotnetInstallUrl = 'https://dot.net/v1/dotnet-install.ps1'
$Ch340DriverUrl = 'https://www.wch-ic.com/downloads/CH341SER_EXE.html'

# USB-serial bridges used on ESP32-2432S0xx boards (keep in sync with PortFinder.cs)
$KnownBridges = @(
  @{ Vid = '1A86'; Pid = '7523'; Chip = 'CH340' },
  @{ Vid = '1A86'; Pid = '55D4'; Chip = 'CH9102' },
  @{ Vid = '10C4'; Pid = 'EA60'; Chip = 'CP210x' }
)

$RepoRoot     = Split-Path -Parent $PSScriptRoot
$InstallRoot  = Join-Path $env:LOCALAPPDATA $AppId
$AppDir       = Join-Path $InstallRoot 'app'
$ToolsDir     = Join-Path $InstallRoot 'tools'
$BuildDir     = Join-Path $InstallRoot 'build'
$LogFile      = Join-Path $InstallRoot 'install.log'
$SettingsFile = Join-Path $InstallRoot 'settings.json'
$StartMenuDir = Join-Path ([Environment]::GetFolderPath('Programs')) $AppName
$UninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppId"
# Setup.exe drops this marker: it then owns the uninstall entry and the uninstaller
$FromSetup    = Test-Path (Join-Path $RepoRoot 'installed-by-setup.txt')

# ---------------------------------------------------------------------------
# Console helpers
# ---------------------------------------------------------------------------

$script:StepNo = 0
$script:StepCount = 6

function Write-Banner {
  $line = '=' * 62
  Write-Host ''
  Write-Host "  $line" -ForegroundColor DarkCyan
  Write-Host '     MSFS 2024  *  Cheap Yellow Display HUD  *  Installer' -ForegroundColor Cyan
  Write-Host "  $line" -ForegroundColor DarkCyan
  Write-Host ''
}

function Write-Step([string]$Title) {
  $script:StepNo++
  Write-Host ''
  Write-Host ("  [{0}/{1}] {2}" -f $script:StepNo, $script:StepCount, $Title) -ForegroundColor Cyan
  Write-Host ('  ' + ('-' * ($Title.Length + 6))) -ForegroundColor DarkGray
  Add-InstallLog "== $Title"
}

function Write-Ok([string]$Text)   { Write-Host "    [ OK ] $Text" -ForegroundColor Green;  Add-InstallLog "OK   $Text" }
function Write-Info([string]$Text) { Write-Host "           $Text" -ForegroundColor Gray;   Add-InstallLog "INFO $Text" }
function Write-Warn([string]$Text) { Write-Host "    [WARN] $Text" -ForegroundColor Yellow; Add-InstallLog "WARN $Text" }
function Write-Fail([string]$Text) { Write-Host "    [FAIL] $Text" -ForegroundColor Red;    Add-InstallLog "FAIL $Text" }
function Write-Doing([string]$Text) { Write-Host "    ...    $Text" -ForegroundColor White;  Add-InstallLog "DO   $Text" }

function Add-InstallLog([string]$Text) {
  try { Add-Content -Path $LogFile -Value ("{0:u} {1}" -f (Get-Date), $Text) -Encoding UTF8 } catch { }
}

function Confirm-Choice([string]$Question, [bool]$Default = $true) {
  if ($Yes) { return $Default }
  $hint = if ($Default) { '[Y/n]' } else { '[y/N]' }
  while ($true) {
    $answer = (Read-Host "    $Question $hint").Trim().ToLowerInvariant()
    if ($answer -eq '') { return $Default }
    if ($answer -in @('y', 'yes', 'j', 'ja')) { return $true }
    if ($answer -in @('n', 'no', 'nein')) { return $false }
  }
}

function Read-Choice([string]$Question, [string[]]$Options, [int]$Default = 0) {
  if ($Yes) { return $Default }
  Write-Host "    $Question"
  for ($i = 0; $i -lt $Options.Count; $i++) {
    $mark = if ($i -eq $Default) { '*' } else { ' ' }
    Write-Host ("     {0} {1}) {2}" -f $mark, ($i + 1), $Options[$i])
  }
  while ($true) {
    $answer = (Read-Host ("    Choice [{0}]" -f ($Default + 1))).Trim()
    if ($answer -eq '') { return $Default }
    $n = 0
    if ([int]::TryParse($answer, [ref]$n) -and $n -ge 1 -and $n -le $Options.Count) { return $n - 1 }
  }
}

function Read-Value([string]$Question, [string]$Default = '') {
  if ($Yes -and $Default) { return $Default }
  $suffix = if ($Default) { " [$Default]" } else { '' }
  while ($true) {
    $answer = (Read-Host "    $Question$suffix").Trim()
    if ($answer -eq '' -and $Default) { return $Default }
    if ($answer -ne '') { return $answer }
  }
}

function Stop-Installer([string]$Reason) {
  Write-Host ''
  Write-Fail $Reason
  Write-Host "           Full log: $LogFile" -ForegroundColor DarkGray
  Write-Host ''
  exit 1
}

# Runs a native tool with extra environment variables, sends its output to the
# log and shows only a status line; on failure the tail of the output is shown.
# Returns $true on success.
function Invoke-Tool([string]$Exe, [string[]]$Arguments, [string]$What, [hashtable]$Environment = @{}) {
  Write-Doing $What
  Add-InstallLog ("RUN  {0} {1}" -f $Exe, ($Arguments -join ' '))
  $saved = @{}
  foreach ($k in $Environment.Keys) {
    $saved[$k] = [Environment]::GetEnvironmentVariable($k, 'Process')
    [Environment]::SetEnvironmentVariable($k, $Environment[$k], 'Process')
  }
  # Windows PowerShell turns every stderr line of a native tool into an error
  # record; with 'Stop' the first progress message would abort the installer.
  $eap = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $output = @(& $Exe @Arguments 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $eap
    foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], 'Process') }
  }
  $output | ForEach-Object { Add-InstallLog "     $_" }
  if ($code -ne 0) {
    $output | Select-Object -Last 15 | ForEach-Object { Write-Host "           $_" -ForegroundColor DarkGray }
    return $false
  }
  return $true
}

# ---------------------------------------------------------------------------
# Settings (answers from the previous run become the defaults)
# ---------------------------------------------------------------------------

function Get-Settings {
  if (Test-Path $SettingsFile) {
    try { return Get-Content $SettingsFile -Raw | ConvertFrom-Json } catch { }
  }
  return [pscustomobject]@{}
}

function Get-Setting($Settings, [string]$Name, $Default = $null) {
  # PSObject.Properties[...] instead of .Properties.Name: on an object without
  # properties (first run) the latter throws under Set-StrictMode
  $prop = $Settings.PSObject.Properties[$Name]
  if ($prop -and $prop.Value) { return $prop.Value }
  return $Default
}

function Save-Settings([hashtable]$Values) {
  New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
  [pscustomobject]$Values | ConvertTo-Json | Set-Content -Path $SettingsFile -Encoding UTF8
}

# ---------------------------------------------------------------------------
# Display detection
# ---------------------------------------------------------------------------

function Get-SerialDevices {
  # Win32_PnPEntity covers every USB-serial driver; the COM name is in the caption
  $devices = @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '\((COM\d+)\)' })
  foreach ($d in $devices) {
    $null = $d.Name -match '\((COM\d+)\)'
    $com = $Matches[1]
    $chip = $null
    foreach ($b in $KnownBridges) {
      if ($d.DeviceID -match "VID_$($b.Vid)&PID_$($b.Pid)") { $chip = $b.Chip }
    }
    [pscustomobject]@{ Port = $com; Name = $d.Name; Chip = $chip; Known = [bool]$chip }
  }
}

function Test-MissingDriver {
  # A plugged-in bridge without a driver shows up with an error code and no COM port
  $problem = @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.ConfigManagerErrorCode -ne 0 -and $_.DeviceID -match 'VID_(1A86|10C4)' })
  return $problem.Count -gt 0
}

function Find-Display([string]$Preferred) {
  while ($true) {
    $all = @(Get-SerialDevices)
    if ($Preferred -and ($all | Where-Object Port -eq $Preferred)) {
      Write-Ok "Using $Preferred"
      return $Preferred
    }
    $known = @($all | Where-Object Known)
    if ($known.Count -eq 1) {
      Write-Ok ("Display found on {0} ({1})" -f $known[0].Port, $known[0].Chip)
      return $known[0].Port
    }
    if ($known.Count -gt 1) {
      $i = Read-Choice 'Several boards are connected. Which one is the display?' ($known | ForEach-Object { "$($_.Port)  $($_.Name)" })
      return $known[$i].Port
    }
    if ($all.Count -gt 0) {
      Write-Warn 'No CH340/CP210x board found, but these serial ports exist:'
      $options = @($all | ForEach-Object { "$($_.Port)  $($_.Name)" }) + 'None of these - search again'
      $i = Read-Choice 'Which one is the display?' $options ($options.Count - 1)
      if ($i -lt $all.Count) { return $all[$i].Port }
    } elseif (Test-MissingDriver) {
      Write-Warn 'The board is plugged in, but Windows has no driver for its USB chip.'
      Write-Info 'Windows Update usually installs it within a minute. If not, install the CH340 driver.'
      if (Confirm-Choice 'Open the CH340 driver download page?' $true) { Start-Process $Ch340DriverUrl }
    } else {
      Write-Warn 'No display found. Connect it with a USB *data* cable (some cables only charge).'
    }
    if ($Yes) { Stop-Installer 'Display not found.' }
    $null = Read-Host '           Press Enter to search again (Ctrl+C to quit)'
  }
}

function Stop-Sender {
  Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($SenderExe)) -ErrorAction SilentlyContinue |
    ForEach-Object {
      Write-Info "Stopping the running sender (PID $($_.Id)) so the port is free"
      $_ | Stop-Process -Force
      $_.WaitForExit(5000) | Out-Null
    }
}

# ---------------------------------------------------------------------------
# Firmware
# ---------------------------------------------------------------------------

function Get-ArduinoCli {
  $dir = Join-Path $ToolsDir 'arduino-cli'
  $exe = Join-Path $dir 'arduino-cli.exe'
  if (-not (Test-Path $exe)) {
    Write-Doing 'Downloading arduino-cli'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $zip = Join-Path $dir 'arduino-cli.zip'
    Invoke-WebRequest -Uri $ArduinoCliUrl -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $dir -Force
    Remove-Item $zip
  }
  Write-Ok 'arduino-cli ready'
  return $exe
}

# The firmware version the installer ships: from the bundled image's build or the sketch
function Get-ShippedFirmwareVersion {
  $sketch = Join-Path $RepoRoot 'ship_hud\ship_hud.ino'
  if (Test-Path $sketch) {
    $m = Select-String -Path $sketch -Pattern '#define\s+HUD_FW_VERSION\s+"([^"]+)"' | Select-Object -First 1
    if ($m) { return $m.Matches[0].Groups[1].Value }
  }
  return $null
}

# Setup.exe ships a ready-made image; a plain source checkout builds one.
function Get-BundledFirmware {
  $bin = Join-Path $RepoRoot 'firmware\msfs-cyd-hud.bin'
  if (Test-Path $bin) { return $bin }
  return $null
}

function Get-Esptool {
  foreach ($candidate in @((Join-Path $RepoRoot 'tools\esptool\esptool.exe'),
                           (Join-Path $ToolsDir 'esptool\esptool-windows-amd64\esptool.exe'))) {
    if (Test-Path $candidate) { return $candidate }
  }
  Write-Doing 'Downloading esptool (the ESP32 flasher)'
  $dir = Join-Path $ToolsDir 'esptool'
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  $zip = Join-Path $dir 'esptool.zip'
  Invoke-WebRequest -Uri $EsptoolUrl -OutFile $zip -UseBasicParsing
  Expand-Archive -Path $zip -DestinationPath $dir -Force
  Remove-Item $zip
  return (Join-Path $dir 'esptool-windows-amd64\esptool.exe')
}

# Builds the firmware from the sketch with a private arduino-cli and returns
# the merged flash image. Only used when no ready-made image is bundled.
function Build-Firmware {
  $cli = Get-ArduinoCli
  # A private Arduino environment: the user's IDE, cores and libraries stay
  # untouched. Short folder names: the ESP32 toolchain has deep paths and
  # Windows still trips over 260 characters in places.
  $arduinoEnv = @{
    ARDUINO_DIRECTORIES_DATA      = (Join-Path $ToolsDir 'a15')
    ARDUINO_DIRECTORIES_USER      = (Join-Path $ToolsDir 'au')
    ARDUINO_DIRECTORIES_DOWNLOADS = (Join-Path $ToolsDir 'dl')
    ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS = $Esp32Index
  }

  if (-not (Invoke-Tool $cli @('core', 'update-index') 'Updating the board index' $arduinoEnv)) {
    Stop-Installer 'Could not download the ESP32 board index. Check your internet connection.'
  }
  if (-not (Invoke-Tool $cli @('core', 'install', $Esp32Core) 'Installing the ESP32 core (first run downloads ~300 MB)' $arduinoEnv)) {
    Stop-Installer 'Installing the ESP32 core failed.'
  }
  if (-not (Invoke-Tool $cli (@('lib', 'install') + $ArduinoLibs) 'Installing LVGL and TFT_eSPI' $arduinoEnv)) {
    Stop-Installer 'Installing the libraries failed.'
  }
  Write-Ok 'Toolchain ready'

  $libs = Join-Path $arduinoEnv.ARDUINO_DIRECTORIES_USER 'libraries'
  Copy-Item (Join-Path $RepoRoot 'config\lv_conf.h') (Join-Path $libs 'lv_conf.h') -Force
  Copy-Item (Join-Path $RepoRoot 'config\User_Setup.h') (Join-Path $libs 'TFT_eSPI\User_Setup.h') -Force

  # WiFi is configured over USB after flashing, so the sketch is built as is
  $buildPath = Join-Path $BuildDir 'out'
  $sketch = Join-Path $RepoRoot 'ship_hud'
  if (-not (Invoke-Tool $cli @('compile', '--fqbn', $Fqbn, '--build-path', $buildPath, $sketch) 'Compiling the firmware (takes a few minutes the first time)' $arduinoEnv)) {
    Stop-Installer 'The firmware did not compile. Please open an issue and attach the log.'
  }
  $image = Join-Path $buildPath 'ship_hud.ino.merged.bin'
  if (-not (Test-Path $image)) { Stop-Installer 'The firmware build did not produce a flash image.' }
  Write-Ok 'Firmware built'
  return $image
}

function Install-Firmware([string]$ComPort, [string]$Image) {
  $esptool = Get-Esptool
  Stop-Sender
  while ($true) {
    $flashArgs = @('--chip', 'esp32', '--port', $ComPort, '--baud', '921600',
                   '--before', 'default_reset', '--after', 'hard_reset',
                   'write_flash', '-z', '0x0', $Image)
    if (Invoke-Tool $esptool $flashArgs "Flashing the display on $ComPort (about 30 seconds)") {
      Write-Ok 'Display flashed'
      return
    }
    Write-Warn 'Flashing failed.'
    Write-Info 'Close anything that uses the port (Arduino serial monitor, the sender).'
    Write-Info 'If it keeps failing: hold the BOOT button on the back of the board while flashing starts.'
    if (-not (Confirm-Choice 'Try again?' $true) -or $Yes) { Stop-Installer 'Could not flash the display.' }
  }
}

# ---- talking to the display over USB (see lib/hud_proto/config_command.h) ----

function ConvertTo-HexString([string]$Text) {
  if ($Text -eq '') { return '-' }
  return (([Text.Encoding]::UTF8.GetBytes($Text) | ForEach-Object { $_.ToString('x2') }) -join '')
}

function New-WifiCommand([string]$Ssid, [string]$Password) {
  return 'HUDCFG WIFI {0} {1}' -f (ConvertTo-HexString $Ssid), (ConvertTo-HexString $Password)
}

# "HUDCFG INFO fw=1.1.0 wifi=connected ip=192.168.1.5 port=4242" -> hashtable
function ConvertFrom-InfoLine([string]$Line) {
  $info = @{}
  if ($Line -notmatch '^HUDCFG INFO ') { return $null }
  foreach ($pair in ($Line.Substring(12) -split ' ')) {
    $kv = $pair -split '=', 2
    if ($kv.Count -eq 2) { $info[$kv[0]] = $kv[1] }
  }
  return $info
}

function Open-Display([string]$ComPort) {
  $sp = New-Object System.IO.Ports.SerialPort $ComPort, 115200
  $sp.ReadTimeout = 250
  $sp.NewLine = "`n"
  # Keep EN and IO0 (wired to RTS/DTR on these boards) released: no reset on open
  $sp.DtrEnable = $false
  $sp.RtsEnable = $false
  $sp.Open()
  return $sp
}

# Sends a command until a HUDCFG reply arrives (the display may still be booting).
function Invoke-DisplayCommand($Port, [string]$Command, [int]$TimeoutSeconds = 20) {
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $nextSend = Get-Date
  while ((Get-Date) -lt $deadline) {
    if ((Get-Date) -ge $nextSend) {
      $Port.Write("`n$Command`n")
      $nextSend = (Get-Date).AddSeconds(2)
    }
    try { $line = $Port.ReadLine().Trim() } catch [TimeoutException] { continue }
    if ($line) { Add-InstallLog "     display: $line" }
    if ($line -match '^HUDCFG (OK|ERR|INFO)') { return $line }
  }
  return $null
}

function Get-DisplayInfo([string]$ComPort) {
  $sp = $null
  try {
    $sp = Open-Display $ComPort
    return (ConvertFrom-InfoLine (Invoke-DisplayCommand $sp 'HUDCFG INFO' 8))
  } catch {
    Add-InstallLog "display info failed: $_"
    return $null
  } finally {
    if ($sp -and $sp.IsOpen) { $sp.Close() }
  }
}

# Stores the WiFi network on the display (or switches WiFi off) and, for WiFi,
# waits until it has joined and returns its IP address.
function Set-DisplayConnection([string]$ComPort, [string]$Mode, [string]$Ssid, [Security.SecureString]$Password) {
  $sp = $null
  try {
    $sp = Open-Display $ComPort
    if ($Mode -ne 'Wifi') {
      $reply = Invoke-DisplayCommand $sp 'HUDCFG WIFI-OFF'
      if ($reply -ne 'HUDCFG OK') { Write-Warn 'The display did not confirm the USB setting.'; return $null }
      Write-Ok 'Display set to USB'
      return $null
    }

    $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password))
    $command = New-WifiCommand $Ssid $plain
    $plain = $null
    Write-Doing "Sending the WiFi settings to the display"
    # Not via Invoke-DisplayCommand: that would log the command with the password
    $sp.Write("`n$command`n")
    $command = $null
    $deadline = (Get-Date).AddSeconds(15)
    $reply = $null
    while (-not $reply -and (Get-Date) -lt $deadline) {
      try { $line = $sp.ReadLine().Trim() } catch [TimeoutException] { continue }
      if ($line -match '^HUDCFG (OK|ERR)') { $reply = $line }
    }
    if ($reply -ne 'HUDCFG OK') { Write-Warn "The display did not accept the WiFi settings ($reply)."; return $null }
    Write-Ok 'WiFi settings stored on the display'

    Write-Doing 'Waiting for the display to join the network'
    $deadline = (Get-Date).AddSeconds(45)
    while ((Get-Date) -lt $deadline) {
      $info = ConvertFrom-InfoLine (Invoke-DisplayCommand $sp 'HUDCFG INFO' 5)
      if ($info -and $info['wifi'] -eq 'connected' -and $info['ip'] -match '^\d+\.\d+\.\d+\.\d+$') { return $info['ip'] }
      Start-Sleep -Seconds 2
    }
    return $null
  } catch {
    Add-InstallLog "display configuration failed: $_"
    Write-Warn "Could not talk to the display on ${ComPort}: $($_.Exception.Message)"
    return $null
  } finally {
    if ($sp -and $sp.IsOpen) { $sp.Close() }
  }
}

# ---------------------------------------------------------------------------
# Sender
# ---------------------------------------------------------------------------

function Get-Dotnet {
  foreach ($candidate in @((Join-Path $ToolsDir 'dotnet\dotnet.exe'), 'dotnet')) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if (-not $cmd) { continue }
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $sdks = @(& $cmd.Source --list-sdks 2>$null)
    $ErrorActionPreference = $eap
    if ($sdks -match '^10\.') {
      Write-Ok ".NET 10 SDK found"
      return $cmd.Source
    }
  }
  Write-Doing 'Installing the .NET 10 SDK for this user (no admin rights needed)'
  $script = Join-Path $env:TEMP 'dotnet-install.ps1'
  Invoke-WebRequest -Uri $DotnetInstallUrl -OutFile $script -UseBasicParsing
  $target = Join-Path $ToolsDir 'dotnet'
  if (-not (Invoke-Tool 'powershell' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script,
      '-Channel', '10.0', '-InstallDir', $target, '-NoPath') 'Running dotnet-install')) {
    Stop-Installer 'Installing the .NET SDK failed.'
  }
  $exe = Join-Path $target 'dotnet.exe'
  if (-not (Test-Path $exe)) { Stop-Installer 'Installing the .NET SDK failed.' }
  Write-Ok '.NET 10 SDK installed'
  return $exe
}

function Test-MsfsSdk([string]$Path) {
  if (-not $Path) { return $false }
  return Test-Path (Join-Path $Path 'SimConnect SDK\lib\managed\Microsoft.FlightSimulator.SimConnect.dll')
}

function Find-MsfsSdk([string]$Preferred) {
  $candidates = @($Preferred, $env:MSFS2024_SDK, $env:MSFS_SDK)
  foreach ($drive in (Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue)) {
    foreach ($name in @('MSFS 2024 SDK', 'MSFS SDK', 'MSFS2024 SDK')) {
      $candidates += Join-Path $drive.Root $name
      $candidates += Join-Path $drive.Root "Program Files\$name"
    }
  }
  foreach ($c in $candidates) {
    if (Test-MsfsSdk $c) { Write-Ok "MSFS SDK found: $c"; return $c }
  }

  Write-Warn 'The MSFS 2024 SDK was not found. The sender needs its SimConnect library.'
  Write-Info 'Install it once from inside the simulator:'
  Write-Info '  MSFS 2024 > Options > General > Developers > Developer Mode ON,'
  Write-Info '  then in the developer menu bar: Help > SDK Installer (Core).'
  Write-Info 'The default install folder is C:\MSFS 2024 SDK.'
  if ($Yes) { return $null }
  while ($true) {
    $i = Read-Choice 'How do you want to continue?' @('Browse for the SDK folder', 'Type the path', 'I just installed it - search again', 'Skip the sender for now') 0
    switch ($i) {
      0 {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
        $dlg.Description = 'Select the MSFS 2024 SDK folder (it contains "SimConnect SDK")'
        if ($dlg.ShowDialog() -eq 'OK' -and (Test-MsfsSdk $dlg.SelectedPath)) { return $dlg.SelectedPath }
        Write-Warn 'That folder does not contain "SimConnect SDK".'
      }
      1 {
        $p = Read-Value 'SDK folder'
        if (Test-MsfsSdk $p) { return $p }
        Write-Warn 'That folder does not contain "SimConnect SDK".'
      }
      2 { return Find-MsfsSdk $null }
      3 { return $null }
    }
  }
}

function Install-Sender([string]$Sdk) {
  $dotnet = Get-Dotnet
  Stop-Sender
  $project = Join-Path $RepoRoot 'msfs-sender\MsfsHudSender\MsfsHudSender.csproj'
  $publish = Join-Path $BuildDir 'sender'
  if (Test-Path $publish) { Remove-Item $publish -Recurse -Force }
  $publishArgs = @('publish', $project, '-c', 'Release', '-r', 'win-x64', '--self-contained',
                   '-p:PublishSingleFile=true', '-o', $publish, '--nologo')
  $buildEnv = @{ MSFS_SDK = $Sdk; DOTNET_CLI_TELEMETRY_OPTOUT = '1'; DOTNET_NOLOGO = '1' }
  if (-not (Invoke-Tool $dotnet $publishArgs 'Building the sender' $buildEnv)) { Stop-Installer 'Building the sender failed.' }
  if (-not (Test-Path (Join-Path $publish 'SimConnect.dll'))) {
    Stop-Installer 'The sender was built without SimConnect.dll - the SDK folder looks incomplete.'
  }

  New-Item -ItemType Directory -Force -Path $AppDir | Out-Null
  Copy-Item (Join-Path $publish '*') $AppDir -Recurse -Force
  Write-Ok "Sender installed to $AppDir"
  return (Join-Path $AppDir $SenderExe)
}

function Test-Display([string]$Exe, [string[]]$TargetArgs) {
  Write-Doing 'Sending a demo flight to the display for 15 seconds...'
  $p = Start-Process -FilePath $Exe -ArgumentList ($TargetArgs + '--demo') -PassThru -WindowStyle Minimized
  Start-Sleep -Seconds 15
  if (-not $p.HasExited) { $p | Stop-Process -Force }
  elseif ($p.ExitCode -ne 0) { Write-Warn "The sender stopped with exit code $($p.ExitCode)." }
  return (Confirm-Choice 'Did the horizon move on the display (and "NO DATA" disappear)?' $true)
}

# ---------------------------------------------------------------------------
# MSFS integration
# ---------------------------------------------------------------------------

# exe.xml lives next to UserCfg.opt; Steam and Microsoft Store use different folders.
function Get-ExeXmlPaths {
  $roots = @(
    (Join-Path $env:APPDATA 'Microsoft Flight Simulator 2024'),
    (Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.Limitless_8wekyb3d8bbwe\LocalCache')
  )
  foreach ($r in $roots) {
    if ((Test-Path (Join-Path $r 'UserCfg.opt')) -or (Test-Path (Join-Path $r 'exe.xml'))) {
      Join-Path $r 'exe.xml'
    }
  }
}

function Get-ExeXml([string]$Path) {
  $xml = New-Object System.Xml.XmlDocument
  $xml.PreserveWhitespace = $false
  if (Test-Path $Path) {
    $xml.Load($Path)
  } else {
    $xml.LoadXml(@'
<?xml version="1.0" encoding="windows-1252"?>
<SimBase.Document Type="Launch" version="1,0">
  <Descr>Launch</Descr>
  <Filename>exe.xml</Filename>
  <Disabled>False</Disabled>
  <Launch.ManualLoad>False</Launch.ManualLoad>
</SimBase.Document>
'@)
  }
  return $xml
}

function Save-ExeXml([System.Xml.XmlDocument]$Xml, [string]$Path) {
  if (Test-Path $Path) {
    Copy-Item $Path ("{0}.bak-{1:yyyyMMdd-HHmmss}" -f $Path, (Get-Date)) -Force
  }
  $settings = New-Object System.Xml.XmlWriterSettings
  $settings.Indent = $true
  # MSFS writes exe.xml as windows-1252; keep it that way
  try { $settings.Encoding = [Text.Encoding]::GetEncoding(1252) } catch { $settings.Encoding = New-Object Text.UTF8Encoding $false }
  $writer = [System.Xml.XmlWriter]::Create($Path, $settings)
  try { $Xml.Save($writer) } finally { $writer.Close() }
}

function Get-AddonName([System.Xml.XmlNode]$Addon) {
  $n = $Addon.SelectSingleNode('Name')
  if ($n) { return $n.InnerText }
  return ''
}

function Register-AutoStart([string]$Exe, [string]$CommandLine) {
  $paths = @(Get-ExeXmlPaths)
  if ($paths.Count -eq 0) {
    Write-Warn 'No MSFS 2024 settings folder found (start the simulator once, then re-run the installer).'
    return
  }
  foreach ($path in $paths) {
    $xml = Get-ExeXml $path
    $doc = $xml.DocumentElement
    $addon = $doc.SelectNodes('Launch.Addon') | Where-Object { (Get-AddonName $_) -eq $AddonName } | Select-Object -First 1
    if (-not $addon) {
      $addon = $xml.CreateElement('Launch.Addon')
      foreach ($n in @('Name', 'Disabled', 'ManualLoad', 'Path', 'CommandLine')) {
        $addon.AppendChild($xml.CreateElement($n)) | Out-Null
      }
      $doc.AppendChild($addon) | Out-Null
    }
    $fields = [ordered]@{ Name = $AddonName; Disabled = 'False'; ManualLoad = 'False'; Path = $Exe; CommandLine = $CommandLine }
    foreach ($k in $fields.Keys) {
      $node = $addon.SelectSingleNode($k)
      if (-not $node) { $node = $addon.AppendChild($xml.CreateElement($k)) }
      $node.InnerText = $fields[$k]
    }
    Save-ExeXml $xml $path
    Write-Ok "Starts with MSFS ($path)"
  }
}

function Unregister-AutoStart {
  foreach ($path in @(Get-ExeXmlPaths)) {
    if (-not (Test-Path $path)) { continue }
    $xml = Get-ExeXml $path
    $hits = @($xml.DocumentElement.SelectNodes('Launch.Addon') | Where-Object { (Get-AddonName $_) -eq $AddonName })
    if ($hits.Count -eq 0) { continue }
    foreach ($h in $hits) { $xml.DocumentElement.RemoveChild($h) | Out-Null }
    Save-ExeXml $xml $path
    Write-Ok "Removed from $path"
  }
}

function New-Shortcut([string]$Path, [string]$Target, [string]$Arguments, [string]$Description, [string]$WorkDir = '') {
  $shell = New-Object -ComObject WScript.Shell
  $lnk = $shell.CreateShortcut($Path)
  $lnk.TargetPath = $Target
  $lnk.Arguments = $Arguments
  $lnk.WorkingDirectory = if ($WorkDir) { $WorkDir } else { Split-Path $Target }
  $lnk.Description = $Description
  $lnk.Save()
}

function Install-Shortcuts([string]$Exe, [string]$TargetArgs) {
  New-Item -ItemType Directory -Force -Path $StartMenuDir | Out-Null
  New-Shortcut (Join-Path $StartMenuDir "$AppName.lnk") $Exe $TargetArgs 'Stream MSFS 2024 telemetry to the display'
  New-Shortcut (Join-Path $StartMenuDir "$AppName - Display test.lnk") $Exe "$TargetArgs --demo".Trim() 'Send a demo flight to the display'
  if (-not $FromSetup) {  # Setup.exe adds its own uninstall shortcut
    $uninstall = Join-Path $InstallRoot 'Uninstall.cmd'
    New-Shortcut (Join-Path $StartMenuDir "Uninstall $AppName.lnk") $uninstall '' "Remove $AppName" $env:USERPROFILE
  }
  Write-Ok 'Start-menu shortcuts created'
}

function Register-Uninstaller {
  if ($FromSetup) { return }  # Setup.exe registered itself in Apps & features
  # Keep a copy of this script so uninstalling works without the repository
  Copy-Item $PSCommandPath (Join-Path $InstallRoot 'install.ps1') -Force
  @(
    '@echo off',
    'cd /d "%USERPROFILE%"',
    'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall'
  ) | Set-Content -Path (Join-Path $InstallRoot 'Uninstall.cmd') -Encoding ASCII

  New-Item -Path $UninstallKey -Force | Out-Null
  $values = @{
    DisplayName     = $AppName
    Publisher       = 'MSFS24-CYD-HUD'
    InstallLocation = $InstallRoot
    UninstallString = "`"$(Join-Path $InstallRoot 'Uninstall.cmd')`""
    DisplayIcon     = (Join-Path $AppDir $SenderExe)
    NoModify        = 1
    NoRepair        = 1
  }
  foreach ($k in $values.Keys) {
    $type = if ($values[$k] -is [int]) { 'DWord' } else { 'String' }
    New-ItemProperty -Path $UninstallKey -Name $k -Value $values[$k] -PropertyType $type -Force | Out-Null
  }
}

# ---------------------------------------------------------------------------
# Uninstall
# ---------------------------------------------------------------------------

function Invoke-Uninstall {
  Write-Banner
  Write-Host "  This removes $AppName from this PC (the display keeps its firmware)." -ForegroundColor White
  if (-not (Confirm-Choice 'Continue?' $true)) { return }
  Stop-Sender
  Unregister-AutoStart
  if (Test-Path $StartMenuDir) { Remove-Item $StartMenuDir -Recurse -Force; Write-Ok 'Shortcuts removed' }
  if (Test-Path $UninstallKey) { Remove-Item $UninstallKey -Recurse -Force }
  Write-Host ''
  Write-Host "  $AppName has been removed." -ForegroundColor Green
  if (-not $Yes) { $null = Read-Host '  Press Enter to close' }
  if (Test-Path $InstallRoot) {
    # This script may be the copy inside InstallRoot: delete the folder after we exit
    Set-Location $env:USERPROFILE
    $cmd = "ping 127.0.0.1 -n 4 > nul & rmdir /s /q `"$InstallRoot`""
    Start-Process -FilePath cmd.exe -ArgumentList '/c', $cmd -WindowStyle Hidden -WorkingDirectory $env:USERPROFILE
  }
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if ($Uninstall) { Invoke-Uninstall; exit 0 }

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
Add-InstallLog "---- install started (PowerShell $($PSVersionTable.PSVersion)) ----"
Write-Banner

if (-not (Test-Path (Join-Path $RepoRoot 'ship_hud\ship_hud.ino'))) {
  Stop-Installer "Run the installer from the project folder (ship_hud\ship_hud.ino not found next to $PSScriptRoot)."
}

$prev = Get-Settings
Write-Host '  This sets up the display and the MSFS companion app.'
if (Get-BundledFirmware) {
  Write-Host '  It takes a few minutes; the first run downloads the .NET SDK if it is missing.'
} else {
  Write-Host '  It takes 5-15 minutes, most of it downloading the ESP32 toolchain on the first run.'
}
Write-Host "  Everything goes to $InstallRoot" -ForegroundColor DarkGray

# ---- 1. Display ----------------------------------------------------------
Write-Step 'Find the display'
$comPort = $null
if (-not $SkipFirmware -or -not $SkipSender) {
  if (-not $Port) { $Port = Get-Setting $prev 'Port' }
  $comPort = Find-Display $Port
}

# ---- 2. Connection ---------------------------------------------------------
Write-Step 'Choose the connection'
if (-not $Connection) {
  $defaultIdx = if ((Get-Setting $prev 'Connection' 'Usb') -eq 'Wifi') { 1 } else { 0 }
  $idx = Read-Choice 'How should the PC talk to the display?' @(
    'USB cable (simplest, recommended)',
    'WiFi (the display only needs USB power)') $defaultIdx
  $Connection = @('Usb', 'Wifi')[$idx]
}
$password = $null
if ($Connection -eq 'Wifi') {
  if (-not $WifiSsid) {
    $current = $null
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
      $iface = netsh wlan show interfaces 2>$null | Select-String '^\s+SSID\s+:\s+(.+)$' | Select-Object -First 1
      if ($iface) { $current = $iface.Matches[0].Groups[1].Value.Trim() }
    } catch { }
    $ErrorActionPreference = $eap
    $WifiSsid = Read-Value 'WiFi network name (2.4 GHz)' (Get-Setting $prev 'WifiSsid' $current)
  }
  if (-not $SkipFirmware) {
    if ($Yes) { Stop-Installer 'WiFi needs a password; run without -Yes or use USB.' }
    $password = Read-Host "    WiFi password for '$WifiSsid'" -AsSecureString
  }
  Write-Ok "WiFi: $WifiSsid (the ESP32 only supports 2.4 GHz networks)"
} else {
  Write-Ok 'USB'
}

# ---- 3. Firmware -------------------------------------------------------------
Write-Step 'Flash and configure the display'
$displayIp = Get-Setting $prev 'DisplayIp'
if ($SkipFirmware) {
  Write-Info 'Skipped (-SkipFirmware)'
} else {
  Stop-Sender
  $shipped = Get-ShippedFirmwareVersion
  $running = Get-DisplayInfo $comPort
  $flash = $true
  if ($running -and $shipped -and $running['fw'] -eq $shipped) {
    Write-Ok "The display already runs firmware $shipped"
    $flash = Confirm-Choice 'Flash it again anyway?' $false
  }
  if ($flash) {
    $image = Get-BundledFirmware
    if ($image) { Write-Ok "Using the ready-made firmware $shipped" }
    else { $image = Build-Firmware }
    Install-Firmware $comPort $image
  }

  $ip = Set-DisplayConnection $comPort $Connection $WifiSsid $password
  $password = $null
  if ($Connection -eq 'Wifi') {
    if ($ip) {
      $displayIp = $ip
      Write-Ok "Display is on the network at $displayIp"
      Write-Info 'Tip: give it a fixed address (DHCP reservation) in your router.'
    } else {
      Write-Warn 'The display did not join the network (wrong password, or a 5 GHz-only network?).'
      $displayIp = Read-Value 'Enter the display IP address if you know it' $displayIp
    }
  }
}

if ($Connection -eq 'Wifi') {
  if (-not $displayIp) { $displayIp = Read-Value 'Display IP address' }
  $targetArgs = @($displayIp, '--udp')
} else {
  # "auto" finds the display by its USB chip, so a new COM number never breaks auto-start
  $isKnown = @(Get-SerialDevices | Where-Object { $_.Port -eq $comPort -and $_.Known }).Count -gt 0
  $targetArgs = if ($isKnown) { @('auto') } else { @($comPort) }
}

# ---- 4. Sender ---------------------------------------------------------------
Write-Step 'Build and install the MSFS sender'
$exe = Join-Path $AppDir $SenderExe
$sdk = Get-Setting $prev 'MsfsSdk'
if ($SkipSender) {
  Write-Info 'Skipped (-SkipSender)'
} else {
  if (-not $MsfsSdk) { $MsfsSdk = $sdk }
  $sdk = Find-MsfsSdk $MsfsSdk
  if ($sdk) {
    $exe = Install-Sender $sdk
  } else {
    Write-Warn 'Sender skipped. Re-run the installer once the MSFS SDK is installed.'
  }
}

Save-Settings @{ Port = $comPort; Connection = $Connection; WifiSsid = $WifiSsid; DisplayIp = $displayIp; MsfsSdk = $sdk }

# ---- 5. Test -------------------------------------------------------------------
Write-Step 'Test the display'
if (Test-Path $exe) {
  if (Test-Display $exe $targetArgs) {
    Write-Ok 'Display works'
  } else {
    Write-Warn 'No picture? Check the troubleshooting section in docs/MSFS_PLUGIN.md.'
    Write-Info ('Run the test again any time: "{0}" {1} --demo' -f $exe, ($targetArgs -join ' '))
  }
} else {
  Write-Info 'Skipped: the sender is not installed.'
}

# ---- 6. Integration -----------------------------------------------------------
Write-Step 'Start with MSFS'
if (Test-Path $exe) {
  if (-not $NoAutoStart -and (Confirm-Choice 'Start the HUD automatically with MSFS 2024?' $true)) {
    Register-AutoStart $exe ($targetArgs -join ' ')
  } else {
    Write-Info 'Not registered. Start it from the Start menu before or after launching MSFS.'
  }
  Register-Uninstaller
  Install-Shortcuts $exe ($targetArgs -join ' ')
} else {
  Write-Info 'Skipped: the sender is not installed.'
}

Write-Host ''
Write-Host '  ==============================================================' -ForegroundColor DarkGreen
Write-Host "   Done! Start a flight in MSFS 2024 - the HUD comes alive." -ForegroundColor Green
Write-Host '   Tap the display to cycle through the 7 screens.' -ForegroundColor Green
Write-Host '  ==============================================================' -ForegroundColor DarkGreen
Write-Host "   Re-run the setup (Start menu) to switch USB/WiFi or update." -ForegroundColor DarkGray
Write-Host "   Uninstall: Settings > Apps > $AppName" -ForegroundColor DarkGray
Write-Host ''
Add-InstallLog '---- install finished ----'
