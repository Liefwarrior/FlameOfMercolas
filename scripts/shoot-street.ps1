<#
Shoots the STREET SENSES leg (a) panic at 1280x720 off dist\granadad.exe into
docs\frames\street. Every frame is the real exe playing the real verb
(--street-assault: stand in the crowd on the Tarwalk, raise steel, no blow) --
the settle window does the scattering. The summary line each run prints is kept
beside its frame in shoot-street.log so a frame that fell short cannot pass as
one that did.

    powershell -ExecutionPolicy Bypass -File .\scripts\shoot-street.ps1
#>

[CmdletBinding()]
param(
    [string] $Exe,
    [string] $OutDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe)    { $Exe    = Join-Path $repoRoot 'dist\granadad.exe' }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'docs\frames\street' }
$env:GRANADAD_CONTENT_DIR = Join-Path $repoRoot 'content'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'shoot-street.log'
if (Test-Path $log) { Remove-Item $log }

# name, then the line's own arguments (the size and the screenshot are added).
# Leg (a): the three settle counts are the panic strip -- 0 the blade just up
# and the crowd intact, 180 three flee ticks in, 360 six ticks in and
# scattered. Leg (b): the blow landing on a docker, the docker on the floor,
# the docker up again, the corpse with WANTED FOR BLOOD, and the rope hearing
# page reading the street's own witness count.
$shots = @(
    @('street-panic-0',    @('--street-assault', '--settle-steps=0')),
    @('street-panic-180',  @('--street-assault', '--settle-steps=180')),
    @('street-panic-360',  @('--street-assault', '--settle-steps=360')),
    @('street-blow',       @('--street-assault=blow', '--settle-steps=0')),
    @('street-down',       @('--street-assault=down', '--settle-steps=0')),
    @('street-up',         @('--street-assault=up', '--settle-steps=0')),
    @('street-kill',       @('--street-assault=kill', '--settle-steps=0')),
    @('street-hearing',    @('--street-assault=hearing'))
)

$failed = 0
foreach ($shot in $shots) {
    $name = $shot[0]
    $args = @('--smoke=0', '--hold', '--width=1280', '--height=720', '--scale=1') + $shot[1] + @("--screenshot=$OutDir\$name-1280x720.png")
    Write-Host "--- $name : granadad.exe $($args -join ' ')"
    $out = & $Exe @args 2>&1 | Out-String
    $code = $LASTEXITCODE
    Add-Content -Path $log -Value ("=== {0} : granadad.exe {1}" -f $name, ($args -join ' '))
    Add-Content -Path $log -Value $out.TrimEnd()
    Add-Content -Path $log -Value ("exit={0}" -f $code)
    Add-Content -Path $log -Value ''
    $summary = ($out -split "`n" | Where-Object { $_ -match 'scripted|granadad: steps|assault' } | Select-Object -First 1)
    if ($summary) { Write-Host "    $($summary.Trim())" }
    Write-Host "    exit=$code"
    if ($code -ne 0) { $failed += 1 }
}
Write-Host ''
Write-Host ("shot {0} frames, {1} fell short; log at {2}" -f $shots.Count, $failed, $log)
