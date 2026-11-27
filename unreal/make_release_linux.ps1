# The OpenTeeGolf Unreal game as a release for Linux (x86-64): dist\OpenTeeGolf-Unreal-<version>-linux-x64.tar.gz
# Unpack it anywhere and run ./OpenTeeGolf.sh. Made here on Windows by cross-compiling.
#
#   powershell -ExecutionPolicy Bypass -File unreal\make_release_linux.ps1
#
# Needs, once: UE 5.8's Linux target platform (Epic Games Launcher > UE 5.8 > Options > Linux) and
# Epic's Linux cross-compile toolchain for it, v26_clang-20.1.8-rockylinux8
# (cdn.unrealengine.com/CrossToolchain_Linux/v26_clang-20.1.8-rockylinux8.exe, which sets
# LINUX_MULTIARCH_ROOT).
#
# Like make_release.ps1, without the installer, the updater's file list or the world course list
# refresh (run make_release.ps1 first for that). The course helper (Helper/) gets Linux's Node.js;
# bridges/ the Linux builds of the R10 and Square Golf bridges (they use BlueZ).
#
#   -SkipBuild: package what's already in OpenTeeGolf\Release-Linux
param([switch]$SkipBuild, [string]$NodeVersion = '24.19.0')

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $Here
$Project = Join-Path $Here 'OpenTeeGolf\OpenTeeGolf.uproject'
$Engine = 'C:\Program Files\Epic Games\UE_5.8\Engine'
$out = Join-Path $Here 'OpenTeeGolf\Release-Linux'

if (-not $env:LINUX_MULTIARCH_ROOT) { $env:LINUX_MULTIARCH_ROOT = [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'Machine') }
if (-not $env:LINUX_MULTIARCH_ROOT -or -not (Test-Path $env:LINUX_MULTIARCH_ROOT)) { throw 'No Linux cross-compile toolchain (LINUX_MULTIARCH_ROOT): see the top of this script.' }
if (-not (Test-Path (Join-Path $Engine 'Binaries\Linux\UnrealGame-Linux-Shipping.target'))) { throw "UE 5.8 has no Linux target platform: see the top of this script." }

# ---- the version, into the game's settings (shown on its menu)
$version = (Get-Content (Join-Path $Root 'package.json') -Raw | ConvertFrom-Json).version
$gameIni = Join-Path $Here 'OpenTeeGolf\Config\DefaultGame.ini'
$ini = Get-Content $gameIni -Raw
$ini = [regex]::Replace($ini, '(?m)^ProjectVersion=.*$', "ProjectVersion=$version")
Set-Content -Path $gameIni -Value $ini -NoNewline -Encoding utf8
Write-Host "OpenTeeGolf $version for Linux"

# ---- the game (Shipping)
if (-not $SkipBuild) {
  & (Join-Path $Here 'make_assets.ps1')
  if (Test-Path $out) { Remove-Item -Recurse -Force $out }
  & (Join-Path $Engine 'Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$Project" -platform=Linux -clientconfig=Shipping `
    -build -cook -stage -pak -iostore -archive "-archivedirectory=$out" -nodebuginfo -unattended -utf8output -nop4 -noP4
  if ($LASTEXITCODE -ne 0) { throw "Packaging failed ($LASTEXITCODE)" }
}

# ---- the release folder
$name = "OpenTeeGolf-Unreal-$version-linux-x64"
$dist = Join-Path $Root 'dist'
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item -Recurse (Join-Path $out 'Linux\*') $stage
Copy-Item (Join-Path $Here 'release\README.txt'), (Join-Path $Here 'release\NOTICE.md'), (Join-Path $Here 'release\WHATS_NEW.md') $stage
Copy-Item (Join-Path $Root 'LICENSE') (Join-Path $stage 'LICENSE-OpenTeeGolf.txt')

# ---- the Garmin R10 and Square Golf bridges: their Linux builds (lib/bridges.js: pinned, checked)
Push-Location $Root
try { node -e "import('./lib/bridges.js').then((m) => Promise.all(['r10', 'square'].map((k) => m.ensureBridge(k, console.log, 'linux')))).catch((e) => { console.error(e.message); process.exit(1); })" }
finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw 'Getting the launch monitor bridges failed.' }
$bridgesOut = Join-Path $stage 'bridges'
New-Item -ItemType Directory -Force $bridgesOut | Out-Null
Copy-Item (Join-Path $Root 'bridges\tenover-frp-linux-x86_64'), (Join-Path $Root 'bridges\allsquare-frp-linux-x86_64') $bridgesOut
foreach ($b in @(@('10over', 'v0.2.0'), @('allsquare', 'v0.2.0'))) {
  Invoke-WebRequest "https://raw.githubusercontent.com/divotmaker/$($b[0])/$($b[1])/LICENSE-MIT" -OutFile (Join-Path $bridgesOut "$($b[0])-LICENSE-MIT.txt") -UseBasicParsing
}

# ---- the course helper: the web version's server (as package-win.ps1 stages it), with Linux's Node.js
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\package-win.ps1') -NoZip -NodeVersion $NodeVersion
if ($LASTEXITCODE -ne 0) { throw 'Staging the course helper failed.' }
$web = Join-Path $dist "OpenTeeGolf-$version-win-x64"
$helperOut = Join-Path $stage 'Helper'
New-Item -ItemType Directory -Force $helperOut | Out-Null
Get-ChildItem $web | Where-Object { $_.Name -notin @('bridges', 'test', 'courses', 'node', 'Start OpenTeeGolf.bat') } | ForEach-Object { Copy-Item -Recurse $_.FullName $helperOut }
# (Node.js for Linux, checked against the official checksums; the game looks for Helper/node/node)
$cache = Join-Path $dist 'cache'
New-Item -ItemType Directory -Force $cache | Out-Null
$nodeTar = "node-v$NodeVersion-linux-x64.tar.xz"
$nodeFile = Join-Path $cache $nodeTar
$base = "https://nodejs.org/dist/v$NodeVersion"
if (-not (Test-Path $nodeFile)) { Write-Host "Downloading Node.js $NodeVersion for Linux..."; Invoke-WebRequest "$base/$nodeTar" -OutFile $nodeFile }
$sums = (Invoke-WebRequest "$base/SHASUMS256.txt" -UseBasicParsing).Content
$expected = ($sums -split "`n" | Where-Object { $_ -match [regex]::Escape($nodeTar) + '$' }) -replace '\s.*$', ''
if (-not $expected -or (Get-FileHash $nodeFile -Algorithm SHA256).Hash.ToLower() -ne $expected.Trim().ToLower()) {
  Remove-Item $nodeFile
  throw 'Node.js for Linux failed its checksum check. Run the script again.'
}
$nodeOut = Join-Path $helperOut 'node'
New-Item -ItemType Directory -Force $nodeOut | Out-Null
& python -c "import tarfile,sys; t=tarfile.open(sys.argv[1]); d='node-v'+sys.argv[3]+'-linux-x64/'; [open(sys.argv[2]+'/'+n,'wb').write(t.extractfile(d+p).read()) for p,n in (('bin/node','node'),('LICENSE','LICENSE'))]" $nodeFile $nodeOut $NodeVersion
if ($LASTEXITCODE -ne 0) { throw 'Unpacking Node.js for Linux failed.' }

# ---- the tarball (execute bits set here: Windows has none to keep)
$tgz = Join-Path $dist "$name.tar.gz"
if (Test-Path $tgz) { Remove-Item $tgz }
& python (Join-Path $Here 'make_tarball.py') $stage $tgz 'OpenTeeGolf.sh' 'OpenTeeGolf-Linux-Shipping' 'node' 'tenover-frp-linux-x86_64' 'allsquare-frp-linux-x86_64' 'CrashReportClient'
if ($LASTEXITCODE -ne 0) { throw "Making the tarball failed ($LASTEXITCODE)" }
