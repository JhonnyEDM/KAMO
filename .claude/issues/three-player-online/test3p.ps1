# Three KAMO windows on the real lobby: A creates a 3-player room, B and C join, A starts.
# -From: a KAMO folder holding rom.cfg and a signed-in netplay_secret (sign in with Discord once there).
# -Row: list row of the new room in the Play online list (1 = first). Never join someone else's room.
# -Bin: the build to test (default build-recomp). -Public: guests dial the host's public address
# as real players do (needs NAT hairpinning on this PC's router); by default KAMO_LOCAL_PEER=1
# makes them dial 127.0.0.1, which tests the lobby and the match without the router.
param([int]$Players = 3, [int]$Wait = 20, [int]$Row = 1, [string]$From = "$env:LOCALAPPDATA\Programs\KAMO",
      [string]$Bin = "", [switch]$Public)
if (-not $Bin) { $Bin = "$PSScriptRoot\..\..\..\build-recomp" }
$s = $PSScriptRoot; $b = (Resolve-Path $Bin).Path; $t = "$env:TEMP\kamo-3p"
$env:KAMO_LOCAL_PEER = if ($Public) { $null } else { "1" }
Get-Process KAMO -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$t*" } | Stop-Process -Force
Start-Sleep 1; Remove-Item -Recurse -Force $t -ErrorAction SilentlyContinue; New-Item -ItemType Directory $t | Out-Null
Copy-Item "$b\KAMO.exe","$b\*.dll" $t; Copy-Item -Recurse "$b\assets" $t; Copy-Item "$From\rom.cfg", "$From\netplay_secret" $t
$env:SDL_VIDEODRIVER = $null
$names = "a","b","c","d" | Select-Object -First $Players
$ids = @{}; $x = 0
foreach ($n in $names) {
    $p = Start-Process "$t\KAMO.exe" -WorkingDirectory $t -PassThru -RedirectStandardError "$t\log-$n.txt" -RedirectStandardOutput "$t\out-$n.txt"
    $ids[$n] = $p.Id; Start-Sleep 2
}
Start-Sleep 2
$pos = @{ a = @(0,0); b = @(980,0); c = @(0,700); d = @(980,700) }
foreach ($n in $names) { & "$s\move.ps1" -ProcId $ids[$n] -X $pos[$n][0] -Y $pos[$n][1] }
function Tap($n, $x, $y) { & "$s\click.ps1" -X $x -Y $y -ProcId $ids[$n]; Start-Sleep -Milliseconds 600; & "$s\click.ps1" -X $x -Y $y -ProcId $ids[$n]; Start-Sleep -Milliseconds 900 }
Start-Sleep 3
foreach ($n in $names) { Tap $n 487 405 }          # Jugar online
Start-Sleep 4
for ($try = 0; $try -lt 3; $try++) {                 # retry a window that did not reach the lobby
    $missing = $names | Where-Object { -not (Select-String -Path "$t\log-$_.txt" -Pattern "hello as" -Quiet) }
    if (-not $missing) { break }
    foreach ($n in $missing) { Tap $n 487 405 }
    Start-Sleep 4
}
Tap "a" 840 135                                       # Crear una sala
& "$s\click.ps1" -X (83 + 66 * ($Players - 2)) -Y 286 -ProcId $ids["a"]; Start-Sleep 1   # players radio
& "$s\click.ps1" -X 487 -Y 477 -ProcId $ids["a"]; Start-Sleep 4                          # Crear
foreach ($n in $names | Select-Object -Skip 1) {     # join: our room is the last row
    & "$s\shot.ps1" -Out "$t\list-$n.png" -ProcId $ids[$n] | Out-Null
    & "$s\click.ps1" -X 850 -Y (181 + 44 * $Row) -ProcId $ids[$n]; Start-Sleep 4
}
& "$s\shot.ps1" -Out "$t\room.png" -ProcId $ids["a"] | Out-Null
& "$s\click.ps1" -X 623 -Y 486 -ProcId $ids["a"]; Start-Sleep 1
& "$s\click.ps1" -X 623 -Y 486 -ProcId $ids["a"]; Start-Sleep $Wait  # Empezar partida
Get-Process KAMO | Where-Object { $_.Path -like "$t*" } | Select-Object Id, MainWindowTitle | Format-Table | Out-String
foreach ($n in $names) { & "$s\shot.ps1" -Out "$t\game-$n.png" -ProcId $ids[$n] | Out-Null }
foreach ($n in $names) { "--- $n"; Get-Content "$t\log-$n.txt" | Select-String "KAMO online|rnet_lobby: (launch|room)|refused" | Select-Object -Last 12 | ForEach-Object { $_.Line } }

