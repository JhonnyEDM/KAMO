# Standard test suite for the recompiled KAMO build. Prints one PASS/FAIL line per test.
# Needs the ROM and BIOS paths in local/rom.ini and local/bios.ini ("path = ...").
# All runs are windowless and muted. Output goes to -OutDir (default: %TEMP%\kamo-test-<time>).
param(
    [string]$BuildDir = "build-recomp",
    [string]$OutDir = "",
    [string[]]$Only = @()   # e.g. -Only boot,link2
)

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
function Get-IniPath($file) {
    $line = Get-Content $file | Where-Object { $_ -match '^\s*path\s*=' } | Select-Object -First 1
    if (-not $line) { throw "No 'path =' line in $file" }
    ($line -split '=', 2)[1].Trim()
}
$rom = Get-IniPath "local/rom.ini"
$bios = Get-IniPath "local/bios.ini"
$exe = Join-Path $root "$BuildDir\KAMO.exe"
$probe = Join-Path $root "$BuildDir\KAMOViewProbe.exe"
$py = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
if (-not (Test-Path $py)) { $py = "python" }
$loopback = Join-Path $root "external\gbarecomp\tests\link\view_network_loopback.py"
if (-not $OutDir) { $OutDir = Join-Path $env:TEMP ("kamo-test-" + (Get-Date -Format "yyyyMMdd-HHmmss")) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$env:SDL_AUDIODRIVER = "dummy"
$env:SDL_VIDEODRIVER = "dummy"

$results = [ordered]@{}
function Want($name) { $Only.Count -eq 0 -or $Only -contains $name }
function Report($name, $ok, $detail) {
    $results[$name] = $ok
    "{0,-13} {1}  {2}" -f $name, ($(if ($ok) { "PASS" } else { "FAIL" })), $detail
}
function First-Problem($files) {
    Select-String -Path $files -Pattern "missing|desync|mismatch|error|refus" -ErrorAction SilentlyContinue |
        Where-Object { $_.Line -notmatch '^rbe: |NativeCommandError|FullyQualifiedErrorId|CategoryInfo|^At |^\s*\+' } | Select-Object -First 1 | ForEach-Object Line
}

# Button script: press A on every console every 45 frames until <until>, then walk around.
function New-MenuScript($path, $players, $until, $frames, $offset = 0) {
    $lines = @()
    for ($f = 600; $f -lt $until; $f += 45) {
        $lines += "{0} {1}" -f ($f - $offset), ((@(1) * $players) -join " ")
        $lines += "{0} {1}" -f ($f + 6 - $offset), ((@(0) * $players) -join " ")
    }
    for ($f = $until; $f -lt $frames; $f += 40) {
        $lines += "{0} {1}" -f ($f - $offset), ((@(16, 32, 16, 32)[0..($players - 1)]) -join " ")
        $lines += "{0} {1}" -f ($f + 10 - $offset), ((@(17, 33, 18, 34)[0..($players - 1)]) -join " ")
    }
    $lines | Where-Object { [int](($_ -split ' ')[0]) -ge 1 } | Set-Content $path
}

if (Want "boot") {
    $log = "$OutDir\boot.log"
    & $exe --rom $rom --bios $bios --frames 900 --no-window *> $log
    $ok = $LASTEXITCODE -eq 0 -and (Select-String $log -Pattern "cpu_backend=static-recompiled" -Quiet)
    Report "boot" $ok "exit=$LASTEXITCODE $(First-Problem $log)"
}

foreach ($n in 2, 4) {
    if (-not (Want "link$n")) { continue }
    $script = "$OutDir\link$n.inputs"
    New-MenuScript $script $n 3000 6000
    $log = "$OutDir\link$n.log"
    & $probe $rom $bios cold "$OutDir\link$n" $n 6000 $script *> $log
    $problem = First-Problem $log
    Report "link$n" ($LASTEXITCODE -eq 0 -and -not $problem) "exit=$LASTEXITCODE $problem (screens: $OutDir\link$n-seat*.ppm)"
}

# Four consoles open each multiplayer sub-game (Speed Eaters, Crackity Hack, Kirby Wave Ride)
# from the Sub-Games menu and play it with scripted input. A missed function fails the run.
if (Want "subgames") {
    $fails = @()
    foreach ($game in 0, 1, 2) {
        $l = [System.Collections.Generic.List[string]]::new()
        function Press($f, $m) { $l.Add("$f $m $m $m $m"); $l.Add("$($f + 6) 0 0 0 0") }
        Press 600 1; Press 700 1; Press 800 1; Press 880 128; Press 940 1   # title, file, Sub-Games
        $f = 990; for ($d = 0; $d -lt $game; $d++) { Press $f 128; $f += 40 }
        Press ($f + 10) 1; Press ($f + 90) 128; Press ($f + 130) 1          # the game, Multi Player
        $r = New-Object Random (10 + $game); $f += 200
        while ($f -lt 7000) {
            $c = @(1, 1, 1, 2, 16, 32, 64, 128, 17, 33, 0)[$r.Next(11)]
            $l.Add("$f $c $c $c $c"); $l.Add("$($f + 4) 0 0 0 0"); $f += $r.Next(8, 30)
        }
        $l | Set-Content "$OutDir\sub$game.inputs"
        & $probe $rom $bios cold "$OutDir\sub$game" 4 7200 "$OutDir\sub$game.inputs" *> "$OutDir\sub$game.log"
        $problem = First-Problem "$OutDir\sub$game.log"
        if ($LASTEXITCODE -ne 0 -or $problem) { $fails += "game$game exit=$LASTEXITCODE $problem" }
    }
    Report "subgames" ($fails.Count -eq 0) "$($fails -join '; ') (screens: $OutDir\sub*-seat0.ppm)"
}

if ((Want "net-delay") -or (Want "net-rollback")) {
    $warm = "$OutDir\warm2"
    "1 0 0" | Set-Content "$OutDir\none.inputs"
    & $probe $rom $bios cold $warm 2 590 "$OutDir\none.inputs" *> "$OutDir\warm2.log"
    New-MenuScript "$OutDir\net.inputs" 2 3000 4190 590
    foreach ($mode in "delay", "rollback") {
        if (-not (Want "net-$mode")) { continue }
        $dir = "$OutDir\net-$mode"
        $la = @($loopback, $probe, $rom, $bios, "$warm.state", $dir, "--inputs", "$OutDir\net.inputs",
                  "--frames", "3600", "--widths", "240,240")
        if ($mode -eq "rollback") { $la += "--rollback" }
        & $py @la *> "$OutDir\net-$mode.log"
        $pass = Select-String "$OutDir\net-$mode.log" -Pattern "^PASS" | Select-Object -First 1 | ForEach-Object Line
        $problem = First-Problem (@("$OutDir\net-$mode.log") + (Get-ChildItem $dir -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object FullName))
        Report "net-$mode" ($LASTEXITCODE -eq 0 -and $pass) "$pass $problem"
    }
}

if (Want "peers4") {
    $dir = "$OutDir\peers4"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ps = @()
    $session = Get-Random -Minimum 1000 -Maximum 99999
    foreach ($i in 0..3) {
        $a = @("--rom", "`"$rom`"", "--bios", "`"$bios`"", "--frames", "900", "--netplay-players", "4",
               "--netplay-seat", "$i", "--netplay-session", "$session",
               "--netplay-bind", "127.0.0.1:$(7845 + $i)", "--netplay-checkpoint", "`"$dir\p$i.paired`"")
        if ($i -gt 0) { $a += @("--netplay-peer", "127.0.0.1:7845") }
        $ps += Start-Process $exe -ArgumentList $a -PassThru -WorkingDirectory $dir `
            -RedirectStandardError "$dir\e$i.txt" -RedirectStandardOutput "$dir\o$i.txt"
    }
    $ps | Wait-Process -Timeout 300 -ErrorAction SilentlyContinue
    $codes = $ps | ForEach-Object { if ($_.HasExited) { $_.ExitCode } else { $_.Kill(); "timeout" } }
    $hashes = Get-ChildItem "$dir\*.paired" -ErrorAction SilentlyContinue | ForEach-Object { (Get-FileHash $_).Hash } | Sort-Object -Unique
    $ok = ($codes | Where-Object { $_ -ne 0 }).Count -eq 0 -and (Get-ChildItem "$dir\*.paired").Count -eq 4 -and @($hashes).Count -eq 1
    Report "peers4" $ok "exits=$($codes -join ',') checkpoints=$((Get-ChildItem "$dir\*.paired" -ErrorAction SilentlyContinue).Count) distinct=$(@($hashes).Count) $(First-Problem "$dir\e*.txt")"
}

"logs: $OutDir"
if ($results.Values -contains $false) { exit 1 }
exit 0
