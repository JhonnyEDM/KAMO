# Builds KAMO on Windows with the Visual Studio C++ Build Tools, CMake and Ninja.
#   .\build.ps1            Release build in build\
#   .\build.ps1 -Debug     Debug build in build-debug\
param([switch]$Debug)

$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$config = if ($Debug) { 'Debug' } else { 'Release' }
$dir = if ($Debug) { 'build-debug' } else { 'build' }

# Run CMake inside the MSVC developer environment.
cmd /c "`"$vcvars`" >nul && cmake -S . -B $dir -G Ninja -DCMAKE_BUILD_TYPE=$config && cmake --build $dir"
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Write-Host "Built: $dir\KAMO.exe"
