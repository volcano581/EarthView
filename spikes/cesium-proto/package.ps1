<#
.SYNOPSIS
  Builds a self-contained, copyable folder of the Cesium tactical-map prototype for offline
  Windows machines: exe, Qt runtime (incl. Qt WebEngine), MSVC runtime, CesiumJS, the page, and
  the DTED / borders / place-name data.

.EXAMPLE
  .\package.ps1
  .\package.ps1 -QtDir C:\Qt\6.11.1\msvc2022_64 -BuildDir .\build-611
#>
param(
    [string]$QtDir = 'C:\Qt\6.4.2\msvc2019_64',
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build-642'),
    [string]$DataDir = (Join-Path $PSScriptRoot '..\..\EarthView\Data'),
    [string]$Out = (Join-Path $PSScriptRoot 'dist\CesiumTacticalMapDemo')
)
$ErrorActionPreference = 'Stop'

$exe = Join-Path $BuildDir 'Release\cesium_proto.exe'
if (-not (Test-Path $exe)) { throw "Build first: $exe not found." }
if (-not (Test-Path (Join-Path $PSScriptRoot 'web\cesium\Cesium.js'))) { throw 'web\cesium is missing (see README.md).' }
if (-not (Test-Path (Join-Path $QtDir 'bin\windeployqt.exe'))) { throw "No windeployqt in $QtDir." }

# Start from an empty folder; only ever delete a previous package (it holds cesium_proto.exe).
if (Test-Path $Out) {
    if (-not (Test-Path (Join-Path $Out 'cesium_proto.exe'))) { throw "$Out exists and is not a previous package." }
    Remove-Item $Out -Recurse -Force
}
New-Item -ItemType Directory -Force $Out | Out-Null

Write-Host 'Program and Qt runtime...'
Copy-Item $exe $Out
& (Join-Path $QtDir 'bin\windeployqt.exe') --release --no-compiler-runtime (Join-Path $Out 'cesium_proto.exe') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }

Write-Host 'MSVC runtime (app-local)...'
$crt = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio', 'C:\Program Files (x86)\Microsoft Visual Studio' `
        -Recurse -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\Microsoft' -and $_.FullName -notmatch 'onecore|debug' } |
    Sort-Object { [version]($_.Parent.Parent.Name) } -Descending | Select-Object -First 1
if (-not $crt) { throw 'No Visual C++ x64 redistributable folder found.' }
Copy-Item (Join-Path $crt.FullName '*.dll') $Out

Write-Host 'Web page and CesiumJS...'
Copy-Item (Join-Path $PSScriptRoot 'web') (Join-Path $Out 'web') -Recurse

Write-Host 'Data (DTED, borders, place names)...'
$data = Join-Path $Out 'data'
New-Item -ItemType Directory -Force (Join-Path $data 'DEM') | Out-Null
Get-ChildItem (Join-Path $DataDir 'DEM') -File -Include *.dt0, *.dt1, *.dt2 -Recurse |
    Copy-Item -Destination (Join-Path $data 'DEM')
Copy-Item (Join-Path $DataDir 'Borders') (Join-Path $data 'Borders') -Recurse
Get-ChildItem (Join-Path $DataDir 'Cities') -Directory | ForEach-Object {
    $target = Join-Path $data "Cities\$($_.Name)"
    New-Item -ItemType Directory -Force $target | Out-Null
    Get-ChildItem $_.FullName -File | Where-Object { $_.Name -match 'city|town' } | Copy-Item -Destination $target
}

Copy-Item (Join-Path $PSScriptRoot 'package\*') $Out

$size = (Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Done: {0} ({1:N0} MB)" -f (Resolve-Path $Out), $size)
