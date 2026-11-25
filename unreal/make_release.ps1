# The OpenTeeGolf Unreal game as a release for Windows: dist\OpenTeeGolf-Unreal-<version>-win-x64.zip
# Unzip it anywhere and double-click OpenTeeGolf.exe.
#
#   powershell -ExecutionPolicy Bypass -File unreal\make_release.ps1
#
# It packages the game (Shipping) with no courses - players build their own with the course helper
# (Helper\: the web version's server with its own Node.js, which the game runs hidden), and the
# game builds them when they're opened - and adds the Garmin R10 and Square Golf bridge programs, a
# README for players and the notices.
# The version is the web version's (package.json).
#
#   -SkipWorldCourses: leave lib/world-courses.json.gz as it is (its refresh asks OpenStreetMap's
#   servers about the whole world, area by area: when they're slow it can take hours)
param([switch]$SkipWorldCourses)

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
& (Join-Path $Here 'make_assets.ps1')
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
# (and the release notes, which the game's What's new page reads from next to OpenTeeGolf.exe)
Copy-Item (Join-Path $Here 'release\README.txt'), (Join-Path $Here 'release\NOTICE.md'), (Join-Path $Here 'release\WHATS_NEW.md') $stage
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

# ---- the world's golf courses for the course builder's map (lib/world-courses.json.gz): brought up
# to date from OpenStreetMap (tiles fetched in the last month come from data\cache)
if ($SkipWorldCourses) { Write-Host 'The world course list left as it is (-SkipWorldCourses)' }
else
{
  Push-Location $Root
  try { node scripts/world-courses.js | Select-Object -Last 1 } finally { Pop-Location }
  if ($LASTEXITCODE -ne 0) { throw 'Updating the world course list failed.' }
}

# ---- the course helper (Helper\: the web version's server and its Node.js, run hidden by the game
# to find, build and export courses: UOTGHelperSubsystem, lib/helper.js); not its launch monitor
# bridges (the game has its own), tests or starter
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\package-win.ps1') -NoZip
if ($LASTEXITCODE -ne 0) { throw 'Staging the course helper failed.' }
$web = Join-Path $dist "OpenTeeGolf-$version-win-x64"
$helperOut = Join-Path $stage 'Helper'
New-Item -ItemType Directory -Force $helperOut | Out-Null
Get-ChildItem $web | Where-Object { $_.Name -notin @('bridges', 'test', 'courses', 'Start OpenTeeGolf.bat') } | ForEach-Object { Copy-Item -Recurse $_.FullName $helperOut }

# ---- the file list the game's updater reads (manifest.json: each file's size and SHA-1; the
# download addresses come when publish_release.ps1 puts the release on GitHub)
$repo = (git -C $Root remote get-url origin) -replace '^.*github\.com[:/]', '' -replace '\.git$', ''
& python (Join-Path $Here 'release_manifest.py') $stage $version $repo
if ($LASTEXITCODE -ne 0) { throw 'Writing the file list failed.' }

# ---- the installer (Inno Setup: OpenTeeGolf-Setup-<version>.exe, the game without its scenery,
# which it downloads on its first start). This local one has no download addresses yet - fine for
# testing (-updateurl); publish_release.py builds the real one with them.
$iscc = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
if (Test-Path $iscc) {
  & $iscc /Q "/DVersion=$version" "/DSource=$stage" "/DOutDir=$dist" (Join-Path $Here 'installer\OpenTeeGolf.iss')
  if ($LASTEXITCODE -ne 0) { throw "Building the installer failed ($LASTEXITCODE)" }
  Write-Host "Built $(Join-Path $dist "OpenTeeGolf-Setup-$version.exe")"
} else { Write-Warning 'No Inno Setup 6 (jrsoftware.org): no installer' }

# ---- the zip (Windows' own tar: much faster than Compress-Archive on a few GB; not Git's, which
# may come first on the path and can't write zips)
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -a -c -f $zip -C $dist $name
if ($LASTEXITCODE -ne 0) { throw "Zipping failed ($LASTEXITCODE)" }
Write-Host "Built $zip ($([math]::Round((Get-Item $zip).Length / 1GB, 2)) GB)"
