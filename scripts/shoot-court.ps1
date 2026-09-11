<#
Shoots the whole court at 960x540 off dist\granadad.exe into docs\frames\justice.
Every frame is the real exe playing the real verbs; the summary line each run
prints is kept beside its frame in shoot-court.log so a frame that fell short
cannot pass as one that did.

    powershell -ExecutionPolicy Bypass -File .\scripts\shoot-court.ps1
#>

[CmdletBinding()]
param(
    [string] $Exe,
    [string] $OutDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe)    { $Exe    = Join-Path $repoRoot 'dist\granadad.exe' }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'docs\frames\justice' }
$env:GRANADAD_CONTENT_DIR = Join-Path $repoRoot 'content'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'shoot-court.log'
if (Test-Path $log) { Remove-Item $log }

# name, then the line's own arguments (the size and the screenshot are added).
$shots = @(
    @('court-wanted',        @('--court=wanted')),
    @('court-cull',          @('--court=cull', '--settle-steps=0')),
    @('court-taken',         @('--court=taken', '--settle-steps=0')),
    @('court-page',          @('--court')),
    @('court-paper',         @('--court=paper')),
    @('court-armed',         @('--court=armed')),
    @('court-plea',          @('--court=plea')),
    @('court-deny',          @('--court=deny')),
    @('court-hand',          @('--court=hand')),
    @('court-flame-plea',    @('--flame', '--court=plea')),
    @('court-flame-deny',    @('--flame', '--court=deny')),
    @('court-serve',         @('--court=serve', '--settle-steps=30')),
    @('court-flame-serve',   @('--flame', '--court=serve', '--settle-steps=30')),
    @('court-bloodtag',      @('--court=bloodtag')),
    @('court-ropepage',      @('--court=ropepage')),
    @('court-rope',          @('--court=rope')),
    @('court-newman',        @('--court=newman')),
    @('court-newman-creation', @('--creation'))
)

$failed = 0
foreach ($shot in $shots) {
    $name = $shot[0]
    $args = @('--smoke=0', '--hold', '--width=960', '--height=540', '--scale=1') + $shot[1] + @("--screenshot=$OutDir\$name-960x540.png")
    Write-Host "--- $name : granadad.exe $($args -join ' ')"
    $out = & $Exe @args 2>&1 | Out-String
    $code = $LASTEXITCODE
    Add-Content -Path $log -Value ("=== {0} : granadad.exe {1}" -f $name, ($args -join ' '))
    Add-Content -Path $log -Value $out.TrimEnd()
    Add-Content -Path $log -Value ("exit={0}" -f $code)
    Add-Content -Path $log -Value ''
    $summary = ($out -split "`n" | Where-Object { $_ -match 'court beats=|creation|granadad: steps' } | Select-Object -First 1)
    Write-Host "    $($summary.Trim())"
    Write-Host "    exit=$code"
    if ($code -ne 0) { $failed += 1 }
}
Write-Host ''
Write-Host ("shot {0} frames, {1} fell short; log at {2}" -f $shots.Count, $failed, $log)
