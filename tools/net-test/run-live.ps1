# Live online test: the Unreal client and the web client (web-client.mjs, the real web game in headless Chrome) join one
# lobby on the separate test server and check that they see (and hear) each other.
#   powershell -File tools\net-test\run-live.ps1 -Mode local      -> npx wrangler dev on 127.0.0.1:8787 (started if needed)
#   powershell -File tools\net-test\run-live.ps1 -Mode deployed   -> https://haarklous-ue5-test.server-smoke.workers.dev
#   add -Voice for the voice test (Haarklous.Net.Voice; the editor runs with -game because CEF needs rendering)
# Without -Voice: Haarklous.Net.Live (states, chat, seats, doors, fire). Logs and summaries: tools\out\net-tests\.
# Never points at the live game server.
param([ValidateSet("local", "deployed")][string]$Mode = "local", [string]$Lobby = "", [int]$TimeoutS = 300, [switch]$Voice, [int]$WebDelayS = 0)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine = "C:\Program Files\Epic Games\UE_5.8"
$project = Join-Path $root "unreal\Haarklous\Haarklous.uproject"
$out = Join-Path $root "tools\out\net-tests"
New-Item -ItemType Directory -Force $out | Out-Null
if (-not $Lobby) { $a = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; $Lobby = -join (1..4 | ForEach-Object { $a[(Get-Random -Maximum 32)] }) }
if ($Mode -eq "local") {
  $server = "ws://127.0.0.1:8787"; $allow = "127.0.0.1"
  $page = "http://127.0.0.1:8787/?server=ws://127.0.0.1:8787&lobby=$Lobby"
} else {
  $hostName = "haarklous-ue5-test.server-smoke.workers.dev"
  $server = "wss://$hostName"; $allow = $hostName
  $page = "https://$hostName/?lobby=$Lobby"
}
if ($server -match "haarklous-online|haarklous-plass") { throw "Refusing: that is the live game server" }
$tag = if ($Voice) { "$Mode-voice" } else { $Mode }
"Test $tag, lobby $Lobby, server $server"

$wrangler = $null
function Test-Health { try { (Invoke-WebRequest -UseBasicParsing -TimeoutSec 3 "http://127.0.0.1:8787/health").Content -eq "ok" } catch { $false } }
if ($Mode -eq "local" -and -not (Test-Health)) {
  "Starting npx wrangler dev ..."
  $wrangler = Start-Process -FilePath "cmd.exe" -ArgumentList "/c npx --yes wrangler dev --ip 127.0.0.1 --port 8787 > `"$out\wrangler-dev.log`" 2>&1" -WorkingDirectory (Join-Path $root "server") -PassThru -WindowStyle Hidden
  for ($i = 0; $i -lt 90 -and -not (Test-Health); $i++) { Start-Sleep -Seconds 1 }
  if (-not (Test-Health)) { throw "wrangler dev did not start (see $out\wrangler-dev.log)" }
}
try {
  # Same lock as the build and test scripts (taken before the web client starts, so neither side waits for it).
  $lockPath = Join-Path $env:TEMP "haarklous-ue5-build.lock"
  $lock = $null
  for ($i = 0; $i -lt 3600 -and -not $lock; $i++) { try { $lock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') } catch { Start-Sleep -Seconds 1 } }
  $webArgs = "`"$root\tools\net-test\web-client.mjs`" --page=`"$page`" --lobby=$Lobby --allow=$allow --out=`"$out`" --mode=$Mode --timeout=$TimeoutS"
  if ($Voice) { $webArgs += " --voice" }
  if ($WebDelayS -gt 0) { $webArgs += " --delay-begin=$WebDelayS"; $tag += "-late" }
  $web = Start-Process -FilePath "node" -ArgumentList $webArgs -RedirectStandardOutput "$out\web-client-$tag.log" -RedirectStandardError "$out\web-client-$tag.err.log" -PassThru -WindowStyle Hidden
  $null = $web.Handle   # keeps the exit code readable later
  $ulog = "$out\unreal-client-$tag.log"
  try {
    if ($Voice) {
      # CEF (the hidden voice page) needs rendering: a small game window on the empty engine map.
      $uargs = "`"$project`" /Engine/Maps/Entry -game -windowed -ResX=320 -ResY=180 -nosplash -unattended -nosound -HKVoiceTest " +
        "`"-ExecCmds=Automation RunTests Haarklous.Net.Voice;Quit`" `"-TestExit=Automation Test Queue Empty`" " +
        "-HKLiveServer=$server -HKLiveLobby=$Lobby `"-HKLiveOut=$out`" `"-abslog=$ulog`""
      $p = Start-Process -FilePath "$engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $uargs -PassThru
      if (-not $p.WaitForExit(600000)) { $p.Kill() }
    } else {
      & "$engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -nullrhi -unattended -nosplash -nopause -nosound "-ExecCmds=Automation RunTests Haarklous.Net.Live;Quit" "-TestExit=Automation Test Queue Empty" `
        "-HKLiveServer=$server" "-HKLiveLobby=$Lobby" "-HKLiveOut=$out" "-abslog=$ulog" -stdout 2>&1 | Out-Null
    }
  } finally { if ($lock) { $lock.Close() } }
  if (-not $web.WaitForExit(120000)) { $web.Kill() }
  ""
  "--- Unreal client ---"
  Get-Content $ulog | Where-Object { $_ -match '\[(live|voice) |Test Completed|(live|voice) (local|deployed):|Voice (chat|page)' -and $_ -notmatch 'LogAutomationController: LogHaarklous' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
  ""
  "--- Web client ---"
  Get-Content "$out\web-client-$tag.log"
  $u = Get-Content $ulog | Where-Object { $_ -match 'Result=\{Success\} Name=\{(Live|Voice)\}' }
  $f = Get-Content $ulog | Where-Object { $_ -match 'Result=\{Fail' }
  Get-Content $ulog | Where-Object { $_ -match 'live full lobby:' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
  if (-not $u -or $f -or $web.ExitCode -ne 0) { "LIVE TEST FAILED"; exit 1 }
  "LIVE TEST PASSED ($tag, lobby $Lobby)"
} finally {
  if ($wrangler) { Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'wrangler' -and $_.CommandLine -match '8787' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } }
}
