# Bake the static lighting of the level with Lightmass (CPU), from the command line.
#   powershell -File tools\unreal\build-lighting.ps1 [-Quality Preview|Medium|High|Production]
# Shares the lock of tools\build-unreal.ps1.
param([string]$Quality = "Medium")
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$logDir = Join-Path $root "tools\out\logs"; New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir "build-lighting.log"
$lockPath = Join-Path $env:TEMP "haarklous-ue5-build.lock"
$lock = $null
for ($i = 0; $i -lt 3600 -and -not $lock; $i++) { try { $lock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') } catch { Start-Sleep -Seconds 1 } }
if (-not $lock) { throw "Could not get the build lock" }
$t0 = Get-Date
try {
  & "$engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=resavepackages -buildlighting "-quality=$Quality" -allowcommandletrendering -map=Torshov -unattended -nosplash -nopause "-abslog=$log" -stdout 2>&1 | Out-Null
  $code = $LASTEXITCODE
} finally { $lock.Close() }
"Lighting build ($Quality) took {0:N1} min, exit code $code" -f ((Get-Date) - $t0).TotalMinutes
Get-Content $log | Where-Object { $_ -match 'Lightmass|lighting|Error' -and $_ -notmatch 'Verbose' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -Last 25
exit $code
