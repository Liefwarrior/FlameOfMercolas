<#
Shoots the RUNG PLATE set off dist\granadad.exe into docs\frames\rung. One
plate per Legend track at 960x540 through --rung=TRACK (the real verbs: the
burglary's own box, the alley leapt, two leads read, the bounty paid, four
drinks stood to Cull) and the wire plate again at 320x180 for the strip pin.
Every run's summary line is kept beside its frame in shoot-rung.log, so a
frame whose rung did not rise cannot pass as one that did.

    powershell -ExecutionPolicy Bypass -File .\scripts\shoot-rung.ps1
#>

[CmdletBinding()]
param(
    [string] $Exe,
    [string] $OutDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe)    { $Exe    = Join-Path $repoRoot 'dist\granadad.exe' }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'docs\frames\rung' }
$env:GRANADAD_CONTENT_DIR = Join-Path $repoRoot 'content'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'shoot-rung.log'
if (Test-Path $log) { Remove-Item $log }

# name, width, height, then the line's own arguments. --settle-steps=0 is the
# plate at its first fully-up frame; each track sets its own hour (see
# scriptedStartHour) unless --time= is added here.
$shots = @(
    @('rung-wire-960',  960, 540, @('--rung=wire',  '--settle-steps=0')),
    @('rung-roofs-960', 960, 540, @('--rung=roofs', '--settle-steps=0')),
    @('rung-flame-960', 960, 540, @('--rung=flame', '--settle-steps=0')),
    @('rung-trade-960', 960, 540, @('--rung=trade', '--settle-steps=0')),
    @('rung-law-960',   960, 540, @('--rung=law',   '--settle-steps=0')),
    @('rung-wire-320',  320, 180, @('--rung=wire',  '--settle-steps=0'))
)

$failed = 0
foreach ($shot in $shots) {
    $name = $shot[0]
    $w = $shot[1]
    $h = $shot[2]
    $args = @('--smoke=0', '--hold', "--width=$w", "--height=$h", '--scale=1') + $shot[3] + @("--screenshot=$OutDir\$name.png")
    Write-Host "--- $name : granadad.exe $($args -join ' ')"
    $out = & $Exe @args 2>&1 | Out-String
    $code = $LASTEXITCODE
    Add-Content -Path $log -Value ("=== {0} : granadad.exe {1}" -f $name, ($args -join ' '))
    Add-Content -Path $log -Value $out.TrimEnd()
    Add-Content -Path $log -Value ("exit={0}" -f $code)
    Add-Content -Path $log -Value ''
    $summary = ($out -split "`n" | Where-Object { $_ -match 'rung=|scripted|granadad: steps' } | Select-Object -First 1)
    if ($summary) { Write-Host "    $($summary.Trim())" }
    Write-Host "    exit=$code"
    if ($code -ne 0) { $failed += 1 }
}
Write-Host ''
Write-Host ("shot {0} frames, {1} fell short; log at {2}" -f $shots.Count, $failed, $log)
