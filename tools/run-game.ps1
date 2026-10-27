# Run the game (uncooked, from the editor build) with a scripted play-through and collect its results.
#   powershell -File tools\run-game.ps1 -Test shots [-Res 1600x900] [-Extra "-hkfoo"]
# Screenshots: tools\out\shots\<test>\*.png; log lines starting with HK-TEST are printed at the end.
param([string]$Test = "shots", [string]$Res = "1600x900", [string]$Extra = "", [int]$TimeoutMin = 40, [switch]$Fullscreen)
$root = Split-Path -Parent $PSScriptRoot
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$logDir = Join-Path $root "tools\out\logs"; New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir "game-$Test.log"
$w, $h = $Res.Split('x')
$args = @($project, "-game", $(if ($Fullscreen) { "-fullscreen" } else { "-windowed" }), "-ResX=$w", "-ResY=$h", "-hktest=$Test", "-abslog=$log", "-nosplash", "-dx11", "-NoLoadingScreen") + ($Extra.Split(' ') | Where-Object { $_ })
$p = Start-Process -FilePath "$engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $args -PassThru
if (-not $p.WaitForExit($TimeoutMin * 60 * 1000)) { Stop-Process -Id $p.Id -Force; "TIMEOUT after $TimeoutMin min" }
Get-Content $log | Where-Object { $_ -match 'HK-TEST|LogHaarklous: (Error|Warning)|Fatal error|Assertion failed' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -Last 120
