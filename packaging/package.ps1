# Builds the game and packages a release zip into release\:
#   fallout-ce.exe, a default fallout.cfg and README.txt - no Fallout 1 data
#   (players use their own copy; the game finds it on first launch).
# Usage, from the project folder (the one holding src\, runtime\ and tools\):
#   powershell -ExecutionPolicy Bypass -File src\packaging\package.ps1 [-Version 2026-10-26]
param([string]$Version = (Get-Date -Format 'yyyy-MM-dd'))

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$name = "Fallout-The-Unofficial-Remaster-$Version"
$stage = Join-Path $root "release\$name"
$zip = Join-Path $root "release\$name.zip"

& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'tools\build.ps1') -Config RelWithDebInfo
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
if (Test-Path $zip) { Remove-Item $zip -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $root 'build\RelWithDebInfo\fallout-ce.exe') $stage

# Default settings: the project's own, with the data file locations reset so
# the first launch finds the player's Fallout 1, and no local history.
$cfg = Get-Content (Join-Path $root 'runtime\fallout.cfg')
$cfg = $cfg -replace '^master_dat=.*$', 'master_dat=master.dat' `
            -replace '^critter_dat=.*$', 'critter_dat=critter.dat' `
            -replace '^master_patches=.*$', 'master_patches=data' `
            -replace '^critter_patches=.*$', 'critter_patches=data' `
            -replace '^splash=.*$', 'splash=0' `
            -replace '^times_run=.*$', 'times_run=0'
[System.IO.File]::WriteAllLines((Join-Path $stage 'fallout.cfg'), $cfg)

Copy-Item (Join-Path $PSScriptRoot 'README-release.txt') (Join-Path $stage 'README.txt')

# Never ship Fallout 1's own files or anyone's saves.
$forbidden = Get-ChildItem $stage -Recurse | Where-Object { $_.Name -match '\.(dat|sav)$' -or $_.PSIsContainer }
if ($forbidden) { throw "Release would contain game data: $($forbidden.FullName -join ', ')" }

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "Packaged $zip"
Get-ChildItem $stage | Format-Table Name, Length
