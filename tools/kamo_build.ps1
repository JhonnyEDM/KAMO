# Builds the recompiled KAMO (KAMO.exe and KAMOViewProbe.exe) with a RAM watchdog.
# The generated game is large: each compile job can take ~2 GB. The build is stopped if free
# physical memory falls below -MinFreeGB, so a build can never exhaust the machine.
param([int]$Jobs = 6, [double]$MinFreeGB = 5, [string]$BuildDir = "build-recomp")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if (Test-Path "C:\msys64\mingw64\bin") { $env:PATH = "C:\msys64\mingw64\bin;$env:PATH" }
New-Item -ItemType Directory -Force $BuildDir | Out-Null

cmake -S recomp -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release *> "$BuildDir\configure.log"
if ($LASTEXITCODE -ne 0) { Get-Content "$BuildDir\configure.log" -Tail 20; exit 1 }

$p = Start-Process cmake -ArgumentList "--build $BuildDir --target KAMO KAMOViewProbe -j $Jobs" `
    -NoNewWindow -PassThru -RedirectStandardOutput "$BuildDir\build.log" -RedirectStandardError "$BuildDir\build.err"
$minSeen = [double]::MaxValue
while (-not $p.HasExited) {
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
    if ($free -lt $minSeen) { $minSeen = $free }
    if ($free -lt $MinFreeGB) {
        Get-Process cc1plus, ninja, cmake -ErrorAction SilentlyContinue | Stop-Process -Force
        "ABORTED: free RAM {0:N1} GB is below {1} GB" -f $free, $MinFreeGB
        exit 3
    }
    Start-Sleep -Seconds 2
}
$p.WaitForExit()
"build exit={0}, lowest free RAM {1:N1} GB" -f $p.ExitCode, $minSeen
Select-String "$BuildDir\build.log", "$BuildDir\build.err" -Pattern " error|FAILED|undefined reference" |
    Select-Object -First 15 | ForEach-Object Line
exit $p.ExitCode
