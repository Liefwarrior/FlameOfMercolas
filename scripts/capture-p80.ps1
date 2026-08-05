# Frames for #80 -- the roof slum with people on it, and the food chain.
#
# Every frame here is taken with the SHIPPED dist\granadad.exe, on this Windows
# host, through the headless capture path. The diagnostic line each run prints
# carries the two counters this round added:
#
#   roof=<beds on a deck>/<bodies standing on one right now>
#   mice=<prey on the board>/<prey on the roll>   ate=<catches so far>
#
# so a frame and the claim it is evidence for are printed together.
#
# Usage:  powershell -ExecutionPolicy Bypass -File .\scripts\capture-p80.ps1

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $repo 'dist\granadad.exe'
$out  = Join-Path $repo 'docs\frames\p80-roofs'

if (-not (Test-Path -LiteralPath $exe)) {
    throw "dist\granadad.exe is missing -- run the docker build first."
}
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Shot {
    param(
        [string]$Name,
        [string[]]$Args
    )
    $png = Join-Path $out ("p80-" + $Name + ".png")
    Write-Host ''
    Write-Host ("--- " + $Name) -ForegroundColor Cyan
    $all = @($Args) + @("--screenshot=$png")
    & $exe @all
    if ($LASTEXITCODE -ne 0) { throw "$Name exited $LASTEXITCODE" }
}

$common = @('--width=960', '--height=540', '--scale=1', '--smoke=1', '--hold')

# The opening shot, unchanged, at the hour the ward is busiest -- printed here
# so the two new counters are visible beside the numbers #79 measured.
Shot 'tarwalk-2000' ($common + @('--time=20'))
Shot 'tarwalk-0200' ($common + @('--time=2'))
