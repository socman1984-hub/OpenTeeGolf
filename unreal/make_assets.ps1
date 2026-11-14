# The OpenTeeGolf Unreal game's own assets (make_game_assets.py: the rain, and what exported courses
# are built with at run time - the ground and turf materials, turf patches, boulders, water, the
# tree stand-ins and the template level L_Course), made headless in the game project.
#
#   powershell -ExecutionPolicy Bypass -File unreal/make_assets.ps1
#
# make_release.ps1 runs it first. Courses aren't built here: the game builds each one when it's
# opened, from the folder the web version's Export to Unreal writes (Documents\OpenTeeGolf\Courses).

$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Project = Join-Path $Here 'OpenTeeGolf\OpenTeeGolf.uproject'
$Engine = 'C:\Program Files\Epic Games\UE_5.8\Engine'
$Editor = Join-Path $Engine 'Binaries\Win64\UnrealEditor-Cmd.exe'

# (Unreal cuts a script path at its first space: the scripts run from a copy in a folder without one)
$scripts = Join-Path ([IO.Path]::GetTempPath()) 'OTGPack'
New-Item -ItemType Directory -Force $scripts | Out-Null
Copy-Item (Join-Path $Here 'make_game_assets.py'), (Join-Path $Here 'build_course.py') $scripts -Force
$env:OTG_SCRIPT_DIR = $scripts
Remove-Item (Join-Path $scripts 'assets_done.txt') -ErrorAction SilentlyContinue
$log = Join-Path $Here 'OpenTeeGolf\Saved\game_assets.log'
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
& $Editor $Project -run=pythonscript "-script=$(Join-Path $scripts 'make_game_assets.py')" -unattended -nosplash -RenderOffscreen -stdout "-LogCmds=LogPython Log" | Out-File -Encoding utf8 $log
if (-not (Test-Path (Join-Path $scripts 'assets_done.txt'))) { throw "Making the game's assets failed: see ${log}" }
Write-Host "The game's assets made"
