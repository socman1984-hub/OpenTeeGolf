# The OpenTeeGolf Unreal game as a release for Windows: dist\OpenTeeGolf-Unreal-<version>-win-x64.zip
# Unzip it anywhere and double-click OpenTeeGolf.exe.
#
#   powershell -ExecutionPolicy Bypass -File unreal\make_release.ps1
#
# It packages the game (Shipping) with every course pack built so far (make_pack.ps1 <course id>),
# and adds the Garmin R10 and Square Golf bridge programs, a README for players and the notices.
# The version is the web version's (package.json).

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $Here
$Project = Join-Path $Here 'OpenTeeGolf\OpenTeeGolf.uproject'
$Engine = 'C:\Program Files\Epic Games\UE_5.8\Engine'

# ---- the version, into the game's settings (shown on its menu)
$version = (Get-Content (Join-Path $Root 'package.json') -Raw | ConvertFrom-Json).version
$gameIni = Join-Path $Here 'OpenTeeGolf\Config\DefaultGame.ini'
$ini = Get-Content $gameIni -Raw
$ini = [regex]::Replace($ini, '(?m)^ProjectVersion=.*$', "ProjectVersion=$version")
Set-Content -Path $gameIni -Value $ini -NoNewline -Encoding utf8
Write-Host "OpenTeeGolf $version"

# ---- the game's own assets, then the game (Shipping: no console, no logging, optimised)
& (Join-Path $Here 'make_pack.ps1') -Assets
$out = Join-Path $Here 'OpenTeeGolf\Release'
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
& (Join-Path $Engine 'Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$Project" -platform=Win64 -clientconfig=Shipping `
  -build -cook -stage -pak -iostore -archive "-archivedirectory=$out" -prereqs -nodebuginfo -unattended -utf8output -nop4 -noP4
if ($LASTEXITCODE -ne 0) { throw "Packaging failed ($LASTEXITCODE)" }

# ---- the release folder
$name = "OpenTeeGolf-Unreal-$version-win-x64"
$dist = Join-Path $Root 'dist'
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item -Recurse (Join-Path $out 'Windows\*') $stage
Copy-Item (Join-Path $Here 'release\README.txt'), (Join-Path $Here 'release\NOTICE.md') $stage
Copy-Item (Join-Path $Root 'LICENSE') (Join-Path $stage 'LICENSE-OpenTeeGolf.txt')

# The Garmin R10 and Square Golf bridges, next to the game (it looks for them in bridges\ there):
# the pinned releases, downloaded and checked by the web version's lib/bridges.js
Push-Location $Root
try { node -e "import('./lib/bridges.js').then((m) => Promise.all(['r10', 'square'].map((k) => m.ensureBridge(k)))).catch((e) => { console.error(e.message); process.exit(1); })" }
finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw 'Getting the launch monitor bridges failed.' }
$bridgesOut = Join-Path $stage 'bridges'
New-Item -ItemType Directory -Force $bridgesOut | Out-Null
Copy-Item (Join-Path $Root 'bridges\*.exe') $bridgesOut
# (their licence texts go with them)
foreach ($b in @(@('10over', 'v0.2.0'), @('allsquare', 'v0.2.0'))) {
  Invoke-WebRequest "https://raw.githubusercontent.com/divotmaker/$($b[0])/$($b[1])/LICENSE-MIT" -OutFile (Join-Path $bridgesOut "$($b[0])-LICENSE-MIT.txt") -UseBasicParsing
}

# ---- the zip (Windows' own tar: much faster than Compress-Archive on a few GB; not Git's, which
# may come first on the path and can't write zips)
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -a -c -f $zip -C $dist $name
if ($LASTEXITCODE -ne 0) { throw "Zipping failed ($LASTEXITCODE)" }
Write-Host "Built $zip ($([math]::Round((Get-Item $zip).Length / 1GB, 2)) GB)"
