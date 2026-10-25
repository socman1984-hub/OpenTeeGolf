# Builds the portable Windows release: dist\OpenTeeGolf-<version>-win-x64.zip
# The zip carries its own Node.js, so players just unzip and double-click "Start OpenTeeGolf.bat".
#
# Usage (from the project folder):  powershell -ExecutionPolicy Bypass -File scripts\package-win.ps1 [-NodeVersion 24.19.0]
param([string]$NodeVersion = '24.19.0')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$root = Split-Path -Parent $PSScriptRoot
$version = (Get-Content (Join-Path $root 'package.json') -Raw | ConvertFrom-Json).version
$name = "OpenTeeGolf-$version-win-x64"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

# App files (no courses, imagery or caches: every player builds their own)
foreach ($item in 'server.js', 'package.json', 'LICENSE', 'NOTICE.md', 'README.md', 'Start OpenTeeGolf.bat', 'lib', 'public', 'test') {
  Copy-Item -Recurse (Join-Path $root $item) $stage
}
New-Item -ItemType Directory -Force (Join-Path $stage 'courses') | Out-Null

# three.js: only the parts the browser loads
$three = Join-Path $root 'node_modules\three'
if (-not (Test-Path $three)) { throw 'node_modules\three is missing. Run "npm ci" first.' }
$threeOut = Join-Path $stage 'node_modules\three'
New-Item -ItemType Directory -Force (Join-Path $threeOut 'examples') | Out-Null
Copy-Item -Recurse (Join-Path $three 'build') $threeOut
Copy-Item -Recurse (Join-Path $three 'examples\jsm') (Join-Path $threeOut 'examples')
Copy-Item (Join-Path $three 'package.json'), (Join-Path $three 'LICENSE') $threeOut

# Jolt Physics: the single-file WebAssembly build (used by the browser and the tests)
$jolt = Join-Path $root 'node_modules\jolt-physics'
$joltOut = Join-Path $stage 'node_modules\jolt-physics'
New-Item -ItemType Directory -Force (Join-Path $joltOut 'dist') | Out-Null
Copy-Item (Join-Path $jolt 'dist\jolt-physics.wasm-compat.js') (Join-Path $joltOut 'dist')
Copy-Item (Join-Path $jolt 'package.json'), (Join-Path $jolt 'LICENSE') $joltOut

# ez-tree (procedural trees): just the ES module build the browser loads
$ez = Join-Path $root 'node_modules\@dgreenheck\ez-tree'
$ezOut = Join-Path $stage 'node_modules\@dgreenheck\ez-tree'
New-Item -ItemType Directory -Force (Join-Path $ezOut 'build') | Out-Null
Copy-Item (Join-Path $ez 'build\ez-tree.es.js') (Join-Path $ezOut 'build')
Copy-Item (Join-Path $ez 'package.json'), (Join-Path $ez 'LICENSE') $ezOut

# Leaflet and its marker clustering (the builder's map of courses): the browser builds, stylesheets
# and marker images
$leaflet = Join-Path $root 'node_modules\leaflet'
$leafletOut = Join-Path $stage 'node_modules\leaflet'
New-Item -ItemType Directory -Force $leafletOut | Out-Null
Copy-Item -Recurse (Join-Path $leaflet 'dist') $leafletOut
Copy-Item (Join-Path $leaflet 'package.json'), (Join-Path $leaflet 'LICENSE') $leafletOut
$cluster = Join-Path $root 'node_modules\leaflet.markercluster'
$clusterOut = Join-Path $stage 'node_modules\leaflet.markercluster'
New-Item -ItemType Directory -Force $clusterOut | Out-Null
Copy-Item -Recurse (Join-Path $cluster 'dist') $clusterOut
Copy-Item (Join-Path $cluster 'package.json'), (Join-Path $cluster 'MIT-LICENCE.txt') $clusterOut

# geotiff and its dependencies (the server reads tree-height maps with it)
foreach ($dep in 'geotiff', '@petamoriken\float16', 'lerc', 'pako', 'parse-headers', 'quick-lru', 'web-worker', 'xml-utils', 'zstddec') {
  $dst = Join-Path $stage "node_modules\$dep"
  New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
  Copy-Item -Recurse (Join-Path $root "node_modules\$dep") $dst
}

# Node.js runtime, checked against the official checksums
$cache = Join-Path $dist 'cache'
New-Item -ItemType Directory -Force $cache | Out-Null
$nodeZipName = "node-v$NodeVersion-win-x64.zip"
$nodeZip = Join-Path $cache $nodeZipName
$base = "https://nodejs.org/dist/v$NodeVersion"
if (-not (Test-Path $nodeZip)) {
  Write-Host "Downloading Node.js $NodeVersion..."
  Invoke-WebRequest "$base/$nodeZipName" -OutFile $nodeZip
}
$sums = (Invoke-WebRequest "$base/SHASUMS256.txt" -UseBasicParsing).Content
$expected = ($sums -split "`n" | Where-Object { $_ -match [regex]::Escape($nodeZipName) + '$' }) -replace '\s.*$', ''
$actual = (Get-FileHash $nodeZip -Algorithm SHA256).Hash.ToLower()
if (-not $expected -or $actual -ne $expected.Trim().ToLower()) {
  Remove-Item $nodeZip
  throw "Node.js download failed its checksum check. Run the script again."
}
$unz = Join-Path $cache "node-v$NodeVersion-win-x64"
if (-not (Test-Path $unz)) { Expand-Archive $nodeZip -DestinationPath $cache }
$nodeOut = Join-Path $stage 'node'
New-Item -ItemType Directory -Force $nodeOut | Out-Null
Copy-Item (Join-Path $unz 'node.exe'), (Join-Path $unz 'LICENSE') $nodeOut

# Zip it
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Built $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
