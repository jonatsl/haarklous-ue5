# Package a standalone Windows build of the game (cook + stage + pak) into dist\Haarklous-UE5\.
#   powershell -File tools\package-game.ps1 [-Config Shipping|Development] [-Out <folder>]
# Start it with dist\Haarklous-UE5\Windows\Haarklous.exe (online stays off; see README). -Out packages somewhere else
# (for example while the packaged game is running and its files are in use).
param([string]$Config = "Shipping", [string]$Out = "")
$root = Split-Path -Parent $PSScriptRoot
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$out = if ($Out) { $Out } else { Join-Path $root "dist\Haarklous-UE5" }
$logDir = Join-Path $root "tools\out\logs"; New-Item -ItemType Directory -Force $logDir | Out-Null
$lockPath = Join-Path $env:TEMP "haarklous-ue5-build.lock"
$lock = $null
for ($i = 0; $i -lt 3600 -and -not $lock; $i++) { try { $lock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') } catch { Start-Sleep -Seconds 1 } }
if (-not $lock) { throw "Could not get the build lock" }
$t0 = Get-Date
try {
  & "$engine\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$project" -noP4 -platform=Win64 "-clientconfig=$Config" -build -cook -stage -pak -iostore -archive "-archivedirectory=$out" -unattended -utf8output 2>&1 | Tee-Object -FilePath (Join-Path $logDir "package.log") | Out-Null
  $code = $LASTEXITCODE
} finally { $lock.Close() }
"Packaging ($Config) took {0:N1} min, exit code $code" -f ((Get-Date) - $t0).TotalMinutes
Get-Content (Join-Path $logDir "package.log") | Where-Object { $_ -match 'ERROR|Error:|BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting' } | Select-Object -Last 20
exit $code
