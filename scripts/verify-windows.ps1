<#
Granadad: The Darkstreets -- the half of the build gate that only Windows can run.

    docker compose run --rm --build build      <- compiles, and proves Linux/GCC
    .\scripts\verify-windows.ps1               <- proves mingw/Windows, and compares

or both halves as one command, which is the point of -Build:

    .\scripts\verify-windows.ps1 -Build

WHY THIS SCRIPT EXISTS
----------------------
sim-core bans float/double so that the same bytes decode to the same world
state on every toolchain. Until task #75 that claim had been tested on exactly
one toolchain. The docker build cross-compiled granadad-content-tests.exe and
then threw it away unexecuted -- it had to, because the content directory was a
compile-time constant naming a path inside the build container.

The container still cannot finish the job: it cannot run a PE binary, and
running one under wine would prove something about wine. So the last step is
here, on the machine the binary actually ships to.

WHAT IT PROVES
--------------
1. The content suite and the sim suite both pass under mingw/Windows, not just
   GCC/Linux, and the twin-run determinism gate passes there too.
2. The stronger comparator, now in two halves, both compared byte for byte:

   content-fingerprint  the DECODED state of every shipped world -- section
                        CRCs of what miniz inflated, a CRC32C over every
                        decoded lane, and per-form / per-material / per-flags
                        / per-fluid histograms over all ~2.7M tiles.

   world-hash           the canonical world HASH of every shipped world, plus
                        the final per-section hashes of a fixed simulation run
                        over it. Added in M1: the fingerprint proves the two
                        toolchains read the same bytes, and this proves they
                        then hash and simulate them the same way.

   "N passed" on both sides is a weak comparator: the two strings are equal no
   matter what the binaries decoded. These bytes are not.

A difference here is a REAL FINDING, not a flaky test. Report it; do not
regenerate the Linux side to make it match.

ASCII ONLY, deliberately. Windows PowerShell 5.1 reads a BOM-less .ps1 as the
system ANSI code page, so a single em dash in a comment makes the whole file
fail to parse -- which is how this script first ran. It has to work under both
5.1 (`powershell`) and 7 (`pwsh`), so it stays in the 7-bit range.
#>

[CmdletBinding()]
param(
    # The repo's content/ directory. The .exe reads it from the environment.
    [string] $ContentDir,
    # Where the docker build published its artifacts.
    [string] $DistDir,
    # Run the docker build first, so the whole gate is one command. Without
    # this the Windows half is a thing you have to remember, and a check you
    # have to remember is a check that rots.
    [switch] $Build
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ContentDir) { $ContentDir = Join-Path $repoRoot 'content' }
if (-not $DistDir)    { $DistDir    = Join-Path $repoRoot 'dist' }

$exe        = Join-Path $DistDir 'granadad-content-tests.exe'
$simExe     = Join-Path $DistDir 'granadad-tests.exe'
$gateExe    = Join-Path $DistDir 'granadad-twin-gate.exe'
$linuxReport = Join-Path $DistDir 'content-fingerprint-linux-gcc.txt'
$winReport   = Join-Path $DistDir 'content-fingerprint-windows-mingw.txt'
$linuxHashReport = Join-Path $DistDir 'world-hash-linux-gcc.txt'
$winHashReport   = Join-Path $DistDir 'world-hash-windows-mingw.txt'

function Fail([string] $message, [int] $code = 1) {
    Write-Host ''
    Write-Host "FAILED: $message" -ForegroundColor Red
    exit $code
}

function Require([string] $path, [string] $why) {
    if (-not (Test-Path -LiteralPath $path)) {
        Fail "$path is missing.`n        $why" 2
    }
}

# Byte-for-byte, with a line-level explanation when it fails. Returns nothing;
# calls Fail and exits when the two files differ.
function CompareReports([string] $label, [string] $linuxPath, [string] $winPath, [string] $meaning) {
    $linuxBytes = [System.IO.File]::ReadAllBytes($linuxPath)
    $winBytes   = [System.IO.File]::ReadAllBytes($winPath)

    $lHash = (Get-FileHash -LiteralPath $linuxPath -Algorithm SHA256).Hash
    $wHash = (Get-FileHash -LiteralPath $winPath   -Algorithm SHA256).Hash

    Write-Host ("  {0}" -f $label)
    Write-Host ("    linux/gcc     {0,7} bytes  sha256 {1}" -f $linuxBytes.Length, $lHash)
    Write-Host ("    mingw/windows {0,7} bytes  sha256 {1}" -f $winBytes.Length, $wHash)

    $firstDiff = -1
    $shared = [Math]::Min($linuxBytes.Length, $winBytes.Length)
    for ($i = 0; $i -lt $shared; $i++) {
        if ($linuxBytes[$i] -ne $winBytes[$i]) { $firstDiff = $i; break }
    }
    if ($firstDiff -lt 0 -and $linuxBytes.Length -ne $winBytes.Length) { $firstDiff = $shared }

    if ($firstDiff -lt 0) {
        Write-Host '    IDENTICAL' -ForegroundColor Green
        return
    }

    Write-Host ''
    Write-Host "  FIRST DIFFERENCE AT BYTE OFFSET $firstDiff" -ForegroundColor Red
    # Line-level, so the finding is readable rather than a hex dump.
    $linuxLines = [System.IO.File]::ReadAllLines($linuxPath)
    $winLines   = [System.IO.File]::ReadAllLines($winPath)
    $maxLines = [Math]::Max($linuxLines.Length, $winLines.Length)
    $shown = 0
    for ($n = 0; $n -lt $maxLines -and $shown -lt 40; $n++) {
        $l = if ($n -lt $linuxLines.Length) { $linuxLines[$n] } else { '<missing>' }
        $w = if ($n -lt $winLines.Length)   { $winLines[$n]   } else { '<missing>' }
        if ($l -ne $w) {
            Write-Host ("  line {0}" -f ($n + 1)) -ForegroundColor Yellow
            Write-Host ("    linux/gcc     : {0}" -f $l)
            Write-Host ("    mingw/windows : {0}" -f $w)
            $shown++
        }
    }
    Fail @"
the two toolchains disagreed about $meaning.

        This is a real finding, not a flaky test. The determinism claim that
        justifies banning float/double from sim-core does not hold on these
        two targets. Do NOT regenerate either report to make them agree.

        Both reports are in dist/ -- keep them.
"@
}

Write-Host '=== granadad: windows half of the cross-toolchain gate ==='
Write-Host "  repo:        $repoRoot"
Write-Host "  content dir: $ContentDir"
Write-Host "  dist dir:    $DistDir"

if ($Build) {
    Write-Host ''
    Write-Host '--- 0. docker compose run --rm --build build'
    Push-Location $repoRoot
    try {
        # `run`, not `up`: up exits 0 when the container inside it exits 1.
        # See the long comment in docker-compose.yml.
        docker compose run --rm --build build
        $buildExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    Write-Host "    exit code: $buildExit"
    if ($buildExit -ne 0) { Fail "the docker build failed (exit $buildExit); nothing downstream of it is worth running." $buildExit }
}

Require $ContentDir      'This is the owner''s read-only canon; the suite reads content\maps\baked from it.'
Require $exe             'Run: docker compose run --rm --build build'
Require $gateExe         'Run: docker compose run --rm --build build (it publishes the twin-run gate).'
Require $linuxReport     'Run: docker compose run --rm --build build (it publishes the Linux report).'
Require $linuxHashReport 'Run: docker compose run --rm --build build (it publishes the Linux world-hash report).'

# The whole point of task #75: the path is an argument to the binary, not a
# constant inside it. Set for this process only -- nothing persistent.
$env:GRANADAD_CONTENT_DIR = $ContentDir

# --- 1. the suites, natively -----------------------------------------------

Write-Host ''
Write-Host '--- 1. the content suite, under mingw/Windows'
& $exe
$suiteExit = $LASTEXITCODE
Write-Host "    exit code: $suiteExit"
if ($suiteExit -ne 0) {
    Fail "the content suite failed on Windows (exit $suiteExit) -- it passes on Linux/GCC, so this is a genuine cross-toolchain divergence."
}

if (Test-Path -LiteralPath $simExe) {
    Write-Host ''
    Write-Host '--- 1b. the sim suite, under mingw/Windows'
    & $simExe | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { Fail "granadad-tests.exe failed on Windows (exit $LASTEXITCODE)." }
}

Write-Host ''
Write-Host '--- 1c. the twin-run determinism gate, under mingw/Windows'
# The gate proved determinism WITHIN the Linux process. Running it here proves
# it within a Windows process too -- a different allocator, a different C
# runtime, a different startup order.
& $gateExe --ticks 2000 --walkers 128
if ($LASTEXITCODE -ne 0) { Fail "the twin-run gate failed on Windows (exit $LASTEXITCODE) -- it passes on Linux/GCC, so the same seed produced two different runs in a Windows process." }

# --- 1d. the SHIPPED game, with nothing set --------------------------------
#
# S1 published a dist\granadad.exe that could not start here. It resolved its
# content directory to the build container's /src/content, and nothing in this
# script or in the docker build ever ran it -- both set GRANADAD_CONTENT_DIR
# first, for the TEST binaries, so the gate was structurally blind to the one
# path a player takes. README.md documents this exact command as "the game".
#
# So: environment cleared, real .exe, real frame written to disk.

$gameExe = Join-Path $DistDir 'granadad.exe'
if (Test-Path -LiteralPath $gameExe) {
    Write-Host ''
    Write-Host '--- 1d. the shipped game boots with GRANADAD_CONTENT_DIR cleared'
    $shot = Join-Path $DistDir 'verify-smoke.png'
    if (Test-Path -LiteralPath $shot) { Remove-Item -LiteralPath $shot -Force }

    $saved = $env:GRANADAD_CONTENT_DIR
    Remove-Item Env:\GRANADAD_CONTENT_DIR -ErrorAction SilentlyContinue
    try {
        & $gameExe "--smoke=40" "--screenshot=$shot"
        $gameExit = $LASTEXITCODE
    } finally {
        $env:GRANADAD_CONTENT_DIR = $saved
    }
    Write-Host "    exit code: $gameExit"
    if ($gameExit -ne 0) {
        Fail @"
the shipped game could not start with no environment set (exit $gameExit).

        This is what a player gets. granadad.exe has to find content\maps\baked
        by walking up from its own directory -- dist\granadad.exe is one level
        below <repo>\content. See granadad::content::searchForContentDir.
"@
    }
    Require $shot 'granadad.exe reported success but wrote no frame.'
    $shotBytes = (Get-Item -LiteralPath $shot).Length
    Write-Host "    wrote $shot ($shotBytes bytes)"
    if ($shotBytes -lt 1000) { Fail 'the captured frame is too small to be a real PNG.' }
} else {
    Write-Host ''
    Write-Host '--- 1d. SKIPPED: dist\granadad.exe is not present (client build off?)'
}

# --- 2. the comparators -----------------------------------------------------

Write-Host ''
Write-Host '--- 2. the two reports, under mingw/Windows'
if (Test-Path -LiteralPath $winReport)     { Remove-Item -LiteralPath $winReport -Force }
if (Test-Path -LiteralPath $winHashReport) { Remove-Item -LiteralPath $winHashReport -Force }

& $exe --fingerprint $winReport
if ($LASTEXITCODE -ne 0) { Fail "could not produce the Windows content report (exit $LASTEXITCODE)." }
Require $winReport 'granadad-content-tests.exe reported success but wrote nothing.'

& $gateExe --fingerprint $winHashReport
if ($LASTEXITCODE -ne 0) { Fail "could not produce the Windows world-hash report (exit $LASTEXITCODE)." }
Require $winHashReport 'granadad-twin-gate.exe reported success but wrote nothing.'

# --- 3. byte for byte -------------------------------------------------------

Write-Host ''
Write-Host '--- 3. byte-for-byte comparison'

CompareReports 'decoded world state' $linuxReport $winReport `
    'what the shipped worlds DECODE to'
CompareReports 'world hash + simulation run' $linuxHashReport $winHashReport `
    'what the shipped worlds HASH to, or where a run over them ends up'

Write-Host ''
Write-Host '=== PASS ===' -ForegroundColor Green
Write-Host '  linux/gcc     : content suite + sim suite + twin-run gate + both reports'
Write-Host '  mingw/windows : content suite + sim suite + twin-run gate + both reports'
Write-Host '  comparison    : both reports byte-for-byte identical'
exit 0
