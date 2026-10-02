# Tests for the logic in installer/install.ps1 that does not need real hardware:
# settings, string escaping, display detection (with mocked device lists),
# MSFS SDK detection and exe.xml editing.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests/installer/installer_test.ps1
#   pwsh -NoProfile -File tests/installer/installer_test.ps1
#
# The installer is a script, not a module: its functions are loaded from the
# parsed file, so nothing in it runs.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$script = Join-Path $PSScriptRoot '..\..\installer\install.ps1'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $script).Path, [ref]$tokens, [ref]$errors)
if ($errors) { throw "install.ps1 does not parse: $($errors -join '; ')" }
$ast.FindAll({ $args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true) |
  ForEach-Object { . ([scriptblock]::Create($_.Extent.Text)) }

# Values the installer's top level would set
$work = Join-Path ([IO.Path]::GetTempPath()) ("cydhud-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$InstallRoot = $work
$SettingsFile = Join-Path $work 'settings.json'
$LogFile = Join-Path $work 'install.log'
$AddonName = 'ESP32 HUD Display'
$KnownBridges = @(
  @{ Vid = '1A86'; Pid = '7523'; Chip = 'CH340' },
  @{ Vid = '1A86'; Pid = '55D4'; Chip = 'CH9102' },
  @{ Vid = '10C4'; Pid = 'EA60'; Chip = 'CP210x' }
)
function Write-Host { }  # keep the test output to the results

$script:failures = 0
function Check([bool]$Condition, [string]$Name) {
  if ($Condition) { [Console]::WriteLine("ok   $Name") }
  else { [Console]::WriteLine("FAIL $Name"); $script:failures++ }
}

try {
  # ---- settings --------------------------------------------------------------
  $s = Get-Settings
  Check ((Get-Setting $s 'Port' 'COM1') -eq 'COM1') 'missing settings fall back to the default'
  Save-Settings @{ Port = 'COM7'; Connection = 'Wifi'; WifiSsid = 'Home "5G"'; DisplayIp = $null }
  $s = Get-Settings
  Check ((Get-Setting $s 'Port') -eq 'COM7') 'port round-trips'
  Check ((Get-Setting $s 'WifiSsid') -eq 'Home "5G"') 'SSID with quotes round-trips'
  Check ((Get-Setting $s 'DisplayIp' 'none') -eq 'none') 'empty value falls back to the default'

  # ---- WiFi settings travel to the display as hex (config_command.h) --------
  Check ((ConvertTo-HexString 'AB c') -eq '41422063') 'text is sent as the hex of its bytes'
  Check ((ConvertTo-HexString '') -eq '-') 'an empty password is sent as "-"'
  Check ((ConvertTo-HexString ([string][char]0x00E4)) -eq 'c3a4') 'non-ASCII characters are sent as UTF-8'
  Check ((New-WifiCommand 'Home "5G"' 'p w') -eq 'HUDCFG WIFI 486f6d652022354722 702077') 'the WiFi command has the format the firmware parses'
  Check ((New-WifiCommand 'Cafe' '') -eq 'HUDCFG WIFI 43616665 -') 'an open network has no password'

  $info = ConvertFrom-InfoLine 'HUDCFG INFO fw=1.1.0 wifi=connected ip=192.168.1.50 port=4242'
  Check ($info['fw'] -eq '1.1.0' -and $info['wifi'] -eq 'connected' -and $info['ip'] -eq '192.168.1.50') 'the INFO reply is parsed'
  Check ($null -eq (ConvertFrom-InfoLine 'HUDCFG OK')) 'other replies are not taken for INFO'

  # ---- firmware image and version --------------------------------------------
  $RepoRoot = Join-Path $work 'bundle'
  New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot 'ship_hud') | Out-Null
  '#define HUD_FW_VERSION "9.8.7"' | Set-Content (Join-Path $RepoRoot 'ship_hud\ship_hud.ino')
  Check ((Get-ShippedFirmwareVersion) -eq '9.8.7') 'the shipped firmware version is read from the sketch'
  Check ($null -eq (Get-BundledFirmware)) 'a source checkout has no ready-made image'
  New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot 'firmware') | Out-Null
  Set-Content (Join-Path $RepoRoot 'firmware\msfs-cyd-hud.bin') 'x'
  Check ((Get-BundledFirmware) -like '*msfs-cyd-hud.bin') 'the image bundled by Setup.exe is found'
  $realSketch = Join-Path $PSScriptRoot '..\..\ship_hud\ship_hud.ino'
  $RepoRoot = Split-Path -Parent (Split-Path -Parent $realSketch)
  Check ((Get-ShippedFirmwareVersion) -match '^\d+\.\d+\.\d+$') 'the real sketch declares a firmware version'

  # ---- display detection -----------------------------------------------------
  function Get-CimInstance { param($ClassName, $ErrorAction)
    @(
      [pscustomobject]@{ Name = 'USB-SERIAL CH340 (COM5)'; DeviceID = 'USB\VID_1A86&PID_7523\5&1234'; ConfigManagerErrorCode = 0 },
      [pscustomobject]@{ Name = 'Standard Serial over Bluetooth link (COM3)'; DeviceID = 'BTHENUM\{0000}'; ConfigManagerErrorCode = 0 },
      [pscustomobject]@{ Name = 'USB Composite Device'; DeviceID = 'USB\VID_046D&PID_C52B'; ConfigManagerErrorCode = 0 }
    )
  }
  $devices = @(Get-SerialDevices)
  Check ($devices.Count -eq 2) 'only devices with a COM port are listed'
  Check (@($devices | Where-Object Known).Count -eq 1 -and @($devices | Where-Object Known)[0].Port -eq 'COM5') 'the CH340 is recognised as the display'
  Check (-not @($devices | Where-Object Port -eq 'COM3')[0].Known) 'a Bluetooth port is not mistaken for the display'
  Check (-not (Test-MissingDriver)) 'no driver problem when the port exists'

  function Get-CimInstance { param($ClassName, $ErrorAction)
    @([pscustomobject]@{ Name = 'USB2.0-Serial'; DeviceID = 'USB\VID_1A86&PID_7523\1'; ConfigManagerErrorCode = 28 })
  }
  Check (Test-MissingDriver) 'a CH340 without driver is detected'

  # ---- ready-built sender -------------------------------------------------------
  $SenderExe = 'msfs-hud-sender.exe'
  $RepoRoot = Join-Path $work 'bundle'
  Check ($null -eq (Get-BundledSender)) 'a source checkout has no ready-built sender'
  New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot 'sender') | Out-Null
  Set-Content (Join-Path $RepoRoot 'sender\msfs-hud-sender.exe') 'x'
  Check ((Get-BundledSender) -like '*sender*msfs-hud-sender.exe') 'the sender bundled by Setup.exe is found'

  # ---- exe.xml -----------------------------------------------------------------
  $exeXml = Join-Path $work 'exe.xml'
  function Get-ExeXmlPaths { $exeXml }
  @'
<?xml version="1.0" encoding="Windows-1252"?>
<SimBase.Document Type="Launch" version="1,0">
  <Descr>Launch</Descr>
  <Filename>exe.xml</Filename>
  <Disabled>False</Disabled>
  <Launch.ManualLoad>False</Launch.ManualLoad>
  <Launch.Addon>
    <Name>Other Tool</Name>
    <Disabled>False</Disabled>
    <Path>C:\other.exe</Path>
  </Launch.Addon>
</SimBase.Document>
'@ | Set-Content -Path $exeXml

  function Get-Addons { ([xml](Get-Content $exeXml -Raw)).SelectNodes('/SimBase.Document/Launch.Addon') }

  Register-AutoStart 'C:\HUD\msfs-hud-sender.exe' 'auto'
  Register-AutoStart 'C:\HUD\msfs-hud-sender.exe' '192.168.1.50 --udp'
  $addons = @(Get-Addons)
  $ours = @($addons | Where-Object { $_.SelectSingleNode('Name').InnerText -eq $AddonName })
  Check ($addons.Count -eq 2) 'registering twice keeps one entry and the other add-on'
  Check ($ours[0].SelectSingleNode('CommandLine').InnerText -eq '192.168.1.50 --udp') 'the second registration updates the command line'
  Check ($ours[0].SelectSingleNode('Path').InnerText -eq 'C:\HUD\msfs-hud-sender.exe') 'the exe path is written'
  Check (@(Get-ChildItem $work -Filter 'exe.xml.bak-*').Count -ge 1) 'a backup is written before changing exe.xml'

  Unregister-AutoStart
  $addons = @(Get-Addons)
  Check ($addons.Count -eq 1 -and $addons[0].SelectSingleNode('Name').InnerText -eq 'Other Tool') 'uninstall removes only our entry'

  Remove-Item $exeXml
  Register-AutoStart 'C:\HUD\msfs-hud-sender.exe' 'auto'
  Check (@(Get-Addons).Count -eq 1) 'a missing exe.xml is created'
  Check ((Get-Content $exeXml -Raw) -match 'encoding="windows-1252"') 'exe.xml keeps the windows-1252 encoding MSFS uses'
} finally {
  Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { [Console]::WriteLine("$($script:failures) failure(s)"); exit 1 }
[Console]::WriteLine('installer: all tests passed')
