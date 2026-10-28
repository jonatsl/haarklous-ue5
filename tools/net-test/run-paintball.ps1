# Online paintball test: the real game (-hktest=pbonline) and seven bot clients (pb-bots.mjs) in one lobby on the
# separate TEST server (8 players: the test server allows 12 a lobby). Never the live game server.
#   powershell -File tools\net-test\run-paintball.ps1 -Mode local      -> npx wrangler dev on 127.0.0.1:8787 (started if needed)
#   powershell -File tools\net-test\run-paintball.ps1 -Mode deployed   -> wss://haarklous-ue5-test.server-smoke.workers.dev
param([ValidateSet("local", "deployed")][string]$Mode = "local", [int]$Bots = 7, [int]$TimeoutS = 300)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$out = Join-Path $root "tools\out\net-tests"
New-Item -ItemType Directory -Force $out | Out-Null
$a = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; $Lobby = -join (1..4 | ForEach-Object { $a[(Get-Random -Maximum 32)] })
$server = if ($Mode -eq "local") { "ws://127.0.0.1:8787" } else { "wss://haarklous-ue5-test.server-smoke.workers.dev" }
if ($server -match "haarklous-online|haarklous-plass") { throw "Refusing: that is the live game server" }
"Paintball test ($Mode), lobby $Lobby, server $server, $Bots bots"
$wrangler = $null
function Test-Health { try { (Invoke-WebRequest -UseBasicParsing -TimeoutSec 3 "http://127.0.0.1:8787/health").Content -eq "ok" } catch { $false } }
if ($Mode -eq "local" -and -not (Test-Health)) {
  $wrangler = Start-Process -FilePath "cmd.exe" -ArgumentList "/c npx --yes wrangler dev --ip 127.0.0.1 --port 8787 > `"$out\wrangler-dev.log`" 2>&1" -WorkingDirectory (Join-Path $root "server") -PassThru -WindowStyle Hidden
  for ($i = 0; $i -lt 90 -and -not (Test-Health); $i++) { Start-Sleep -Seconds 1 }
  if (-not (Test-Health)) { throw "wrangler dev did not start (see $out\wrangler-dev.log)" }
}
try {
  $botProc = Start-Process -FilePath "node" -ArgumentList "`"$root\tools\net-test\pb-bots.mjs`" --server=$server --lobby=$Lobby --bots=$Bots --timeout=$TimeoutS `"--out=$out`"" -RedirectStandardOutput "$out\pb-bots-$Mode.log" -RedirectStandardError "$out\pb-bots-$Mode.err.log" -PassThru -WindowStyle Hidden
  $ulog = "$out\unreal-paintball-$Mode.log"
  $uargs = "`"$project`" -game -windowed -ResX=1280 -ResY=720 -nosplash -dx11 -NoLoadingScreen -hktest=pbonline `"-hkout=$out\shots-paintball-$Mode`" -HKOnline -HKServer=$server -HKLobby=$Lobby `"-abslog=$ulog`""
  $p = Start-Process -FilePath "$engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $uargs -PassThru
  if (-not $p.WaitForExit(($TimeoutS + 120) * 1000)) { $p.Kill(); "TIMEOUT" }
  if (-not $botProc.HasExited) { $botProc.Kill() }
  Get-Content $ulog | Where-Object { $_ -match 'HK-TEST' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -Last 40
  Get-Content "$out\pb-bots-$Mode.log" | Select-Object -Last 12
} finally {
  if ($wrangler) { cmd /c "taskkill /T /F /PID $($wrangler.Id)" | Out-Null }
}
