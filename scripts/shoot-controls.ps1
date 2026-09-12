<#
Shoots the controls lane's proof set at 960x540 off dist\granadad.exe into
docs\frames\controls. Every frame is the real exe playing the real verbs and
the real drives. Each run's summary line is kept beside its frame in
shoot-controls.log so a frame that fell short cannot pass as one that did.

    powershell -ExecutionPolicy Bypass -File .\scripts\shoot-controls.ps1

The pad frames need a window. A virtual SDL gamepad is attached and driven
with --padscript; the rest are headless captures. The pad runs get past the
character screen with --padcreation (down x3 to GABRI, A, then UP wraps the
sheet's cursor onto BEGIN, A).
#>

[CmdletBinding()]
param(
    [string] $Exe,
    [string] $OutDir,
    [switch] $SkipPad
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe)    { $Exe    = Join-Path $repoRoot 'dist\granadad.exe' }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'docs\frames\controls' }
$env:GRANADAD_CONTENT_DIR = Join-Path $repoRoot 'content'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'shoot-controls.log'
if (Test-Path $log) { Remove-Item $log }

$size = @('--smoke=0', '--hold', '--width=960', '--height=540', '--scale=1')

# --- headless: name, then the line's own arguments -------------------------
$shots = @(
    @('keys-page-kb',        @('--pause=controls')),
    @('options-page-kb',     @('--pause=settings')),
    @('prompt-door',         @('--face=The Gilded Gull')),
    @('prompt-body-steel',   @('--watch-halt=halt', '--settle-steps=20')),
    @('prompt-pickpocket',   @('--burgle=taproom', '--settle-steps=20')),
    @('prompt-box',          @('--burgle=lock', '--settle-steps=0')),
    @('prompt-take-him-up',  @('--case=down', '--settle-steps=20')),
    @('talk-counter',        @('--street=hand', '--settle-steps=20')),
    @('fists-up-punch',      @('--punch', '--settle-steps=20')),
    @('guard-up-block',      @('--block', '--settle-steps=20')),
    @('cast-row',            @('--flame', '--cast', '--settle-steps=20')),
    @('notes-page',          @('--character')),
    @('grimoire-page',       @('--flame', '--grimoire')),
    @('ward-map-kb',         @('--map-overlay')),
    @('casebook-page-kb',    @('--case=gull')),
    @('quick-bar-kb',        @('--flame', '--quickbar', '--settle-steps=12'))
)

$failed = 0
foreach ($shot in $shots) {
    $name = $shot[0]
    $args = $size + $shot[1] + @("--screenshot=$OutDir\$name-960x540.png")
    Write-Host "--- $name : granadad.exe $($args -join ' ')"
    $out = & $Exe @args 2>&1 | Out-String
    $code = $LASTEXITCODE
    Add-Content -Path $log -Value ("=== {0} : granadad.exe {1}" -f $name, ($args -join ' '))
    Add-Content -Path $log -Value $out.TrimEnd()
    Add-Content -Path $log -Value ("exit={0}" -f $code)
    Add-Content -Path $log -Value ''
    $summary = ($out -split "`n" | Where-Object { $_ -match 'granadad: steps' } | Select-Object -Last 1)
    Write-Host "    $($summary.Trim())"
    Write-Host "    exit=$code"
    if ($code -ne 0) { $failed++ }
}

if (-not $SkipPad) {
    # --- the pad, through a window: a virtual gamepad plays the beats -------
    $creation = 'wait:600,down,wait:150,down,wait:150,down,wait:150,a,wait:600,up,wait:300,a,wait:800'
    $pads = @(
        # name, the hour, the beats. A new game opens on the casebook page. The
        # first RT there is the sub-tab step (LEADS -> THE CASE), photographed
        # as such; B puts the page down and the street is the pad's.
        @('opening-page-pad', 16, 'wait:900,rt,wait:600,shot:casebook-page-pad-960x540,wait:200,b'),
        # the controls page with a pad in hand: START, down x2 to CONTROLS, A
        @('keys-page-pad',    16, 'wait:900,b,wait:400,start,wait:400,down,wait:200,down,wait:200,a,wait:600,shot:keys-page-pad-960x540,wait:200,b,wait:200,b'),
        # the settings page: START, down x3 to SETTINGS, A
        @('options-page-pad', 16, 'wait:900,b,wait:400,start,wait:400,down,wait:200,down,wait:200,down,wait:200,a,wait:600,shot:options-page-pad-960x540,wait:200,b,wait:200,b'),
        # the ring: NOTES (D-pad up), RB onto the ward map, RB onto the grimoire
        @('ring-pad',         16, 'wait:900,b,wait:400,up,wait:700,shot:notes-pad-960x540,rb,wait:700,shot:ward-map-pad-960x540,rb,wait:700,shot:grimoire-pad-960x540,wait:200,b'),
        # the bar stepped by the D-pad: right, right
        @('quick-bar-pad',    16, 'wait:900,b,wait:400,right,wait:300,right,wait:200,shot:quick-bar-pad-960x540'),
        # WAIT on SELECT
        @('wait-pad',         16, 'wait:900,b,wait:400,back,wait:700,shot:wait-pad-960x540,wait:200,b'),
        # the street with a pad speaking, then the pad's primary hand with a
        # serf in reach: RT raises the fists, the reticle keeps offering TALK
        # (a person in reach beats lowering, the walk order)
        @('street-pad',       16, 'wait:900,b,wait:600,shot:street-pad-960x540,rt,wait:900,shot:fists-up-pad-960x540'),
        # the small hours, nobody in reach: RT raises the fists and USE offers
        # LOWER HANDS; A takes it and the row goes down
        @('lower-hands-pad',   3, 'wait:900,b,wait:600,rt,wait:900,shot:lower-hands-pad-960x540,a,wait:700,shot:hands-down-pad-960x540')
    )
    foreach ($pad in $pads) {
        $name = $pad[0]
        $args = @('--width=960', '--height=540', '--scale=1', "--time=$($pad[1])", "--padcreation=$creation", "--padscript=$($pad[2])", "--padshots=$OutDir")
        Write-Host "--- $name : granadad.exe $($args -join ' ')"
        $out = & $Exe @args 2>&1 | Out-String
        $code = $LASTEXITCODE
        Add-Content -Path $log -Value ("=== {0} : granadad.exe {1}" -f $name, ($args -join ' '))
        Add-Content -Path $log -Value $out.TrimEnd()
        Add-Content -Path $log -Value ("exit={0}" -f $code)
        Add-Content -Path $log -Value ''
        $summary = ($out -split "`n" | Where-Object { $_ -match 'padscript' } | Select-Object -Last 2)
        Write-Host "    $(($summary | ForEach-Object { $_.Trim() }) -join ' / ')"
        Write-Host "    exit=$code"
        if ($code -ne 0) { $failed++ }
    }
}

Write-Host ''
Write-Host ("{0} run(s) failed. Log: {1}" -f $failed, $log)
exit $failed
