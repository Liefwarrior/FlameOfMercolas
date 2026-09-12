<#
Shoots the Kit at 960x540 (and the dropped thing on the Tarwalk at 1280x720)
off dist\granadad.exe into docs\frames\kit. Every frame is the real exe playing
the real verbs (--kit=WHERE, the scripted line in Session::runKitLine); the
summary line each run prints is kept beside its frame in shoot-kit.log so a
frame that fell short cannot pass as one that did.

    powershell -ExecutionPolicy Bypass -File .\scripts\shoot-kit.ps1
#>

[CmdletBinding()]
param(
    [string] $Exe,
    [string] $OutDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe)    { $Exe    = Join-Path $repoRoot 'dist\granadad.exe' }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'docs\frames\kit' }
$env:GRANADAD_CONTENT_DIR = Join-Path $repoRoot 'content'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'shoot-kit.log'
if (Test-Path $log) { Remove-Item $log }

# name, size, then the line's own arguments (the screenshot is added).
$shots = @(
    @('kit-take',    '960x540',  @('--kit=take',   '--settle-steps=0')),
    @('kit-theirs',  '960x540',  @('--kit=theirs', '--settle-steps=0')),
    @('kit-sheet',   '960x540',  @('--kit=sheet')),
    @('kit-equip',   '960x540',  @('--kit=equip')),
    @('kit-slot',    '960x540',  @('--kit=slot',   '--settle-steps=0')),
    @('kit-drop',    '1280x720', @('--kit=drop',   '--settle-steps=0')),
    @('kit-search',  '960x540',  @('--kit=search')),
    @('kit-load',    '960x540',  @('--kit=load')),
    @('kit-dr',      '960x540',  @('--kit=dr',     '--settle-steps=12')),
    @('kit-full',    '960x540',  @('--kit'))
)

$failed = 0
foreach ($shot in $shots) {
    $name = $shot[0]
    $size = $shot[1] -split 'x'
    $args = @('--smoke=0', '--hold', "--width=$($size[0])", "--height=$($size[1])", '--scale=1') + $shot[2] + @("--screenshot=$OutDir\$name-$($shot[1]).png")
    Write-Host "--- $name : granadad.exe $($args -join ' ')"
    $out = & $Exe @args 2>&1 | Out-String
    $code = $LASTEXITCODE
    Add-Content -Path $log -Value ("=== {0} : granadad.exe {1}" -f $name, ($args -join ' '))
    Add-Content -Path $log -Value $out.TrimEnd()
    Add-Content -Path $log -Value ("exit={0}" -f $code)
    Add-Content -Path $log -Value ''
    $summary = ($out -split "`n" | Where-Object { $_ -match 'kit beats=|granadad: steps' } | Select-Object -First 1)
    Write-Host "    $($summary.Trim())"
    Write-Host "    exit=$code"
    if ($code -ne 0) { $failed++ }
}

Write-Host ''
if ($failed -gt 0) {
    Write-Host "$failed frame(s) fell short -- read $log"
    exit 1
}
Write-Host "every frame landed its beats -- $OutDir"
