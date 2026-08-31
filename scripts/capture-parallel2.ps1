# Frames for the parallel2 integration -- the three lanes' shot lists,
# headless captures only (the pad-worded windowed shots are taken separately
# through --padcreation / --padscript).
#
# Usage:  powershell -ExecutionPolicy Bypass -File .\scripts\capture-parallel2.ps1

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $repo 'dist\granadad.exe'
$out  = Join-Path $repo 'docs\frames\parallel2'

if (-not (Test-Path -LiteralPath $exe)) { throw "dist\granadad.exe is missing" }
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Shot {
    param([string]$Name, [string[]]$ArgList)
    $png = Join-Path $out ($Name + ".png")
    Write-Host ("--- " + $Name) -ForegroundColor Cyan
    $all = @($ArgList) + @("--screenshot=$png")
    & $exe @all
    if ($LASTEXITCODE -ne 0) { throw "$Name exited $LASTEXITCODE" }
}

$s640 = @('--width=640', '--height=360', '--scale=1')

# --- MEASURE lane ---
Shot 'creation-origin-640'    ($s640 + @('--creation=origin'))
Shot 'creation-sheet-640'     ($s640 + @('--creation=customize'))
Shot 'creation-quiz-640'      ($s640 + @('--creation=quiz'))
Shot 'creation-devin-640'     ($s640 + @('--creation=devin'))
Shot 'casebook-newgame-640'   ($s640 + @('--smoke=1', '--case-tab=leads'))
# regressions: sizes where the clamp lands on the window
Shot 'creation-origin-320'    (@('--width=320', '--height=180', '--scale=1', '--creation=origin'))
Shot 'creation-sheet-1920'    (@('--width=1920', '--height=1080', '--scale=1', '--creation=customize'))
# regression pair: pages that never call the measure
Shot 'kb-wardmap-640'         ($s640 + @('--smoke=1', '--map-overlay'))
Shot 'kb-keys-640'            ($s640 + @('--smoke=1', '--pause=controls'))

# --- MENU lane ---
Shot 'menu-character-640'     ($s640 + @('--smoke=1', '--character'))
Shot 'menu-character-320'     (@('--width=320', '--height=180', '--scale=1', '--smoke=1', '--character'))
Shot 'menu-letters-640'       ($s640 + @('--smoke=1', '--character', '--settle-steps=32', '--refocus=letters', '--refocus-steps=32'))
Shot 'menu-chart-640'         ($s640 + @('--smoke=1', '--map'))

Write-Host 'done' -ForegroundColor Green
