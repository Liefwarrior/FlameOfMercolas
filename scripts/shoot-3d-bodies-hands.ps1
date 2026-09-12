<#
Shoots the 3D bodies-and-hands set at 1280x720 off dist\granadad.exe into
docs\frames\3d-bodies-hands. Every frame is the real exe on the real drives
through the shutter, nothing staged; the shutter's own report line (the scene
hash, the bodies in frame by rig, the hands' state) is kept beside each frame
in shoot.log so a frame that fell short cannot pass as one that did.

    powershell -ExecutionPolicy Bypass -File .\scripts\shoot-3d-bodies-hands.ps1
#>

[CmdletBinding()]
param(
    [string] $Exe,
    [string] $OutDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe)    { $Exe    = Join-Path $repoRoot 'dist\granadad.exe' }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'docs\frames\3d-bodies-hands' }
$env:GRANADAD_CONTENT_DIR = Join-Path $repoRoot 'content'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'shoot.log'
if (Test-Path $log) { Remove-Item $log }

# name, then the line's own arguments (--smoke=40 and the screenshot are added).
$shots = @(
    # the bodies
    @('01-tarwalk-10',        @('--spawn=156,63,19', '--yaw=265', '--hold', '--time=10')),
    @('02-tarwalk-16',        @('--spawn=156,63,19', '--yaw=265', '--hold', '--time=16')),
    @('03-townswoman',        @('--spawn=156,63,19', '--yaw=265', '--fov=35', '--hold', '--time=10')),
    @('04-wastrel',           @('--spawn=156,63,19', '--yaw=265', '--fov=35', '--hold', '--time=10')),
    @('05-urchin',            @('--spawn=156,63,19', '--yaw=265', '--fov=35', '--hold', '--time=10')),
    # the hands, bare
    @('10-fists-idle',        @('--punch', '--settle-steps=60', '--time=10')),
    @('11-fists-charge',      @('--charge=40', '--settle-steps=0', '--time=10')),
    @('12-fists-swing',       @('--punch', '--settle-steps=6', '--time=10')),
    @('13-fists-block',       @('--block', '--settle-steps=20', '--time=10')),
    @('14-fists-cast',        @('--cast', '--flame=away', '--settle-steps=8')),
    @('15-fists-hit',         @('--punch', '--settle-steps=30', '--time=10')),
    @('16-fists-down',        @('--spawn=156,63,19', '--yaw=265', '--hold', '--time=10')),
    # the hands, the sword
    @('20-sword-idle',        @('--watch-halt=halt')),
    @('21-sword-block',       @('--watch-halt=halt', '--block', '--settle-steps=20')),
    # the roofs
    @('30-roof-overview',     @('--spawn=153,72,21', '--yaw=300', '--pitch=-20', '--hold', '--time=10')),
    @('31-roof-close',        @('--roofs', '--time=10')),
    # the posts, the lantern, the quay building
    @('40-door-posts',        @('--spawn=153,58,19', '--yaw=180', '--pitch=-6', '--fov=45', '--hold', '--time=10')),
    @('41-lantern-arm',       @('--spawn=152,64,19', '--yaw=180', '--pitch=20', '--fov=50', '--hold', '--time=21')),
    @('42-lantern-3m',        @('--spawn=152,61,19', '--yaw=180', '--pitch=6', '--fov=50', '--hold', '--time=21')),
    @('43-quay-building',     @('--spawn=147,59,19', '--yaw=330', '--pitch=-14', '--hold', '--time=10')),
    @('44-door-leaf',         @('--spawn=153,58,19', '--yaw=180', '--pitch=-6', '--fov=45', '--hold', '--time=10'))
)

$failed = 0
foreach ($shot in $shots) {
    $name = $shot[0]
    $args = @('--smoke=40') + $shot[1] + @("--screenshot=$OutDir\$name.png")
    Write-Host "--- $name : granadad.exe $($args -join ' ')"
    $out = & $Exe @args 2>&1 | Out-String
    $code = $LASTEXITCODE
    Add-Content -Path $log -Value ("=== {0} : granadad.exe {1}" -f $name, ($args -join ' '))
    Add-Content -Path $log -Value (($out -split "`n" | Where-Object { $_ -match '3d shutter|3d bodies|hands |watch-halt|granadad: steps' }) -join "`n")
    Add-Content -Path $log -Value ("exit={0}" -f $code)
    Add-Content -Path $log -Value ''
    $summary = ($out -split "`n" | Where-Object { $_ -match '3d shutter' } | Select-Object -First 1)
    Write-Host "    $($summary.Trim())"
    Write-Host "    exit=$code"
    if ($code -ne 0) { $failed += 1 }
}
Write-Host ''
Write-Host ("shot {0} frames, {1} fell short; log at {2}" -f $shots.Count, $failed, $log)
