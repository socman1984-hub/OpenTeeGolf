# Make course packs for the OpenTeeGolf Unreal game (unreal/OpenTeeGolf) from courses exported in the
# web version (Ctrl+Shift+U: exports/<course id>/), and package the game with them.
#
#   powershell -File unreal/make_pack.ps1 Majestic_Springs_Golf_Club [more course ids]   (build their content)
#   powershell -File unreal/make_pack.ps1 -Package                                      (package the game)
#   powershell -File unreal/make_pack.ps1 -Assets                                       (just the game's own assets)
#
# Building a course's content runs Unreal headless on the game project (pack_course.py); packaging
# cooks the game with every course built so far, each in a pak of its own, into unreal/OpenTeeGolf/Dist.

param([switch]$Package, [switch]$Assets, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Courses)
$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $Here
$Project = Join-Path $Here 'OpenTeeGolf\OpenTeeGolf.uproject'
$Engine = 'C:\Program Files\Epic Games\UE_5.8\Engine'
$Editor = Join-Path $Engine 'Binaries\Win64\UnrealEditor-Cmd.exe'

foreach ($id in $Courses) {
  $export = Join-Path $Root "exports\$id"
  if (-not (Test-Path (Join-Path $export 'play.json'))) { throw "No play data for ${id} in ${export}: export it from the game (Ctrl+Shift+U)" }
  Write-Host "Building $id ..."
  $env:OTG_EXPORT_DIR = $export
  # (Unreal cuts a script path at its first space: the scripts run from a copy in a folder without one)
  $scripts = Join-Path ([IO.Path]::GetTempPath()) 'OTGPack'
  New-Item -ItemType Directory -Force $scripts | Out-Null
  Copy-Item (Join-Path $Here 'pack_course.py'), (Join-Path $Here 'build_course.py') $scripts -Force
  $env:OTG_SCRIPT_DIR = $scripts
  $log = Join-Path $Here "OpenTeeGolf\Saved\pack_$id.log"
  New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
  & $Editor $Project -run=pythonscript "-script=$(Join-Path $scripts 'pack_course.py')" -unattended -nosplash -RenderOffscreen -stdout "-LogCmds=LogPython Log" | Out-File -Encoding utf8 $log
  $ok = Test-Path (Join-Path $export 'pack_done.txt')
  Select-String -Path $log -Pattern '\[OpenTeeGolf( pack)?\]|LogOTGPack|Error:' | Select-Object -Last 40 | ForEach-Object { $_.Line }
  if (-not $ok) { throw "Building ${id} failed: see ${log}" }
}

# The game's own assets (make_game_assets.py: the rain), before packaging
if ($Package -or $Assets) {
  $scripts = Join-Path ([IO.Path]::GetTempPath()) 'OTGPack'
  New-Item -ItemType Directory -Force $scripts | Out-Null
  Copy-Item (Join-Path $Here 'make_game_assets.py') $scripts -Force
  $env:OTG_SCRIPT_DIR = $scripts
  Remove-Item (Join-Path $scripts 'assets_done.txt') -ErrorAction SilentlyContinue
  $log = Join-Path $Here 'OpenTeeGolf\Saved\game_assets.log'
  & $Editor $Project -run=pythonscript "-script=$(Join-Path $scripts 'make_game_assets.py')" -unattended -nosplash -RenderOffscreen -stdout "-LogCmds=LogPython Log" | Out-File -Encoding utf8 $log
  if (-not (Test-Path (Join-Path $scripts 'assets_done.txt'))) { throw "Making the game's assets failed: see ${log}" }
  Write-Host "The game's assets made"
}

if ($Package) {
  $dist = Join-Path $Here 'OpenTeeGolf\Dist'
  & (Join-Path $Engine 'Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$Project" -platform=Win64 -clientconfig=Development `
    -build -cook -stage -pak -iostore -archive "-archivedirectory=$dist" -unattended -utf8output -nop4 -noP4
  if ($LASTEXITCODE -ne 0) { throw "Packaging failed ($LASTEXITCODE)" }
  Write-Host "The game, with its course packs: $dist\Windows"
}
