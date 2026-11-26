# The grass biome studio's pictures, one for each kind of grass: unreal/grass_icons/<kind>.png.
# The studio grows each kind on its own and photographs it close (-studioicons:
# Saved/Screenshots/grassicon_<kind>.png, 1600x900); this crops the middle of the plot, clear of the
# panel on the left, to a 128-pixel square. make_game_assets.py makes them textures
# (/Game/OpenTeeGolf/Game/Runtime/GrassIcons/T_GrassIcon_<kind>) the studio shows beside each name.
#
#   UnrealEditor.exe <uproject> -game -windowed -ResX=1600 -ResY=900 -studio=biomes -studioicons
#   powershell -ExecutionPolicy Bypass -File unreal\grass_icons.ps1
param([int]$Size = 128)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$shots = Join-Path $here 'OpenTeeGolf\Saved\Screenshots'
$out = Join-Path $here 'grass_icons'
New-Item -ItemType Directory -Force $out | Out-Null
$n = 0
foreach ($f in Get-ChildItem $shots -Filter 'grassicon_*.png') {
  $src = [System.Drawing.Image]::FromFile($f.FullName)
  try {
    # (a square on the plot right of the panel, scaled from the 1600x900 the test runs at)
    $k = $src.Width / 1600.0
    $rect = New-Object System.Drawing.Rectangle ([int](880 * $k)), ([int](240 * $k)), ([int](420 * $k)), ([int](420 * $k))
    $bmp = New-Object System.Drawing.Bitmap $Size, $Size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, $Size, $Size), $rect, [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()
    $name = $f.Name.Substring('grassicon_'.Length)
    $bmp.Save((Join-Path $out $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $n++
  } finally { $src.Dispose() }
}
"$n pictures in $out"
