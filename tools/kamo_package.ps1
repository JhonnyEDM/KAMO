# Packages a built KAMO for players: the KAMO folder, the portable zip, the installer
# (Inno Setup 6) and SHA256SUMS.txt, all in -OutDir. No ROM, BIOS or save is ever included.
param(
    [string]$BuildDir = "build-recomp",
    [string]$OutDir = "dist"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$version = (Select-String recomp/CMakeLists.txt -Pattern 'project\(KAMORecomp VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$out = Join-Path $root $OutDir
$pkg = Join-Path $out "KAMO"
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg }
New-Item -ItemType Directory -Force "$pkg\assets\fonts", "$pkg\assets\kamo", "$pkg\licenses" | Out-Null

$build = Join-Path $root $BuildDir
Copy-Item "$build\KAMO.exe", "$build\SDL2.dll", "$build\libgcc_s_seh-1.dll", "$build\libstdc++-6.dll",
          "$build\libwinpthread-1.dll" $pkg
Copy-Item "$build\assets\fonts\LatoLatin-Bold.ttf", "$build\assets\fonts\LatoLatin-Regular.ttf" "$pkg\assets\fonts"
Copy-Item assets/logo/kamo-logo.png "$pkg\assets\kamo\logo.png"
Copy-Item assets/icon/kamo-icon.png "$pkg\assets\kamo\icon.png"
Copy-Item -Recurse assets/menu "$pkg\assets\kamo\menu"
Copy-Item recomp/package/README.txt $pkg
Copy-Item recomp/package/recomplauncher.json $pkg

$licenses = @{
    "KAMO-LICENSE.txt"            = "LICENSE"
    "THIRD_PARTY_NOTICES.md"      = "THIRD_PARTY_NOTICES.md"
    "GBARecomp-LICENSE.md"        = "external/gbarecomp/LICENSE"
    "arm-recomp-core-LICENSE.txt" = "external/gbarecomp/external/arm-recomp-core/LICENSE"
    "rbengine-LICENSE.txt"        = "external/gbarecomp/external/rbengine/LICENSE"
    "recomp-net-LICENSE.txt"      = "external/gbarecomp/external/recomp-net/LICENSE"
    "recomp-ui-LICENSE.txt"       = "external/recomp-ui/LICENSE"
    "recomp-ui-fonts-NOTICE.md"   = "external/recomp-ui/assets/common/fonts/NOTICE.md"
}
foreach ($name in $licenses.Keys) { Copy-Item $licenses[$name] "$pkg\licenses\$name" }

# Never ship game content.
$bad = Get-ChildItem $pkg -Recurse -File | Where-Object { $_.Extension -in ".gba", ".bin", ".sav", ".state", ".paired", ".cfg" }
if ($bad) { throw "game or player files in the package: $($bad.FullName -join ', ')" }

$zip = Join-Path $out "KAMO-$version-Windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip }
# Files at the top of the zip: RecompLauncher reads recomplauncher.json next to KAMO.exe there.
Compress-Archive -Path "$pkg\*" -DestinationPath $zip -CompressionLevel Optimal

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 is not installed (winget install JRSoftware.InnoSetup)" }
& $iscc /Q "/DAppVersion=$version" "/DSourceDir=$pkg" "/DOutputDir=$out" recomp/package/kamo-setup.iss
if ($LASTEXITCODE -ne 0) { throw "installer build failed" }

$files = Get-ChildItem $out -File | Where-Object { $_.Name -like "KAMO-*$version*" }
$files | ForEach-Object { "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name } |
    Set-Content (Join-Path $out "SHA256SUMS.txt")
"KAMO $version packaged in $out"
$files | ForEach-Object { "  {0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB) }
