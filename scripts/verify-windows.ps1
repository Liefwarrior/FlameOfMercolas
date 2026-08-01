<#
Granadad: The Darkstreets -- the half of the build gate that only Windows can run.

    docker compose run --rm --build build      <- compiles, and proves Linux/GCC
    .\scripts\verify-windows.ps1               <- proves mingw/Windows, and compares

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
1. The 57-case content suite passes under mingw/Windows, not just GCC/Linux.
2. The stronger comparator: both platforms emit a report of the DECODED state
   of every shipped world -- section CRCs of what miniz inflated, a CRC32C over
   every decoded lane, and per-form / per-material / per-flags / per-fluid
   histograms over all ~2.7M tiles -- and the two reports are compared byte for
   byte. "57 passed" on both sides is a weak comparator: the two strings are
   equal no matter what the binaries decoded. These bytes are not.

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
    [string] $DistDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ContentDir) { $ContentDir = Join-Path $repoRoot 'content' }
if (-not $DistDir)    { $DistDir    = Join-Path $repoRoot 'dist' }

$exe        = Join-Path $DistDir 'granadad-content-tests.exe'
$simExe     = Join-Path $DistDir 'granadad-tests.exe'
$linuxReport = Join-Path $DistDir 'content-fingerprint-linux-gcc.txt'
$winReport   = Join-Path $DistDir 'content-fingerprint-windows-mingw.txt'

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

Write-Host '=== granadad: windows half of the cross-toolchain gate ==='
Write-Host "  repo:        $repoRoot"
Write-Host "  content dir: $ContentDir"
Write-Host "  dist dir:    $DistDir"

Require $ContentDir  'This is the owner''s read-only canon; the suite reads content\maps\baked from it.'
Require $exe         'Run: docker compose run --rm --build build'
Require $linuxReport 'Run: docker compose run --rm --build build (it publishes the Linux report).'

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

# --- 2. the comparator ------------------------------------------------------

Write-Host ''
Write-Host '--- 2. the decoded-state report, under mingw/Windows'
if (Test-Path -LiteralPath $winReport) { Remove-Item -LiteralPath $winReport -Force }
& $exe --fingerprint $winReport
if ($LASTEXITCODE -ne 0) { Fail "could not produce the Windows report (exit $LASTEXITCODE)." }
Require $winReport 'the binary reported success but wrote nothing.'

# --- 3. byte for byte -------------------------------------------------------

Write-Host ''
Write-Host '--- 3. byte-for-byte comparison'

$linuxBytes = [System.IO.File]::ReadAllBytes($linuxReport)
$winBytes   = [System.IO.File]::ReadAllBytes($winReport)

$linuxHash = (Get-FileHash -LiteralPath $linuxReport -Algorithm SHA256).Hash
$winHash   = (Get-FileHash -LiteralPath $winReport   -Algorithm SHA256).Hash

Write-Host ("  linux/gcc     {0,7} bytes  sha256 {1}" -f $linuxBytes.Length, $linuxHash)
Write-Host ("  mingw/windows {0,7} bytes  sha256 {1}" -f $winBytes.Length, $winHash)

$firstDiff = -1
$shared = [Math]::Min($linuxBytes.Length, $winBytes.Length)
for ($i = 0; $i -lt $shared; $i++) {
    if ($linuxBytes[$i] -ne $winBytes[$i]) { $firstDiff = $i; break }
}
if ($firstDiff -lt 0 -and $linuxBytes.Length -ne $winBytes.Length) { $firstDiff = $shared }

if ($firstDiff -ge 0) {
    Write-Host ''
    Write-Host "  FIRST DIFFERENCE AT BYTE OFFSET $firstDiff" -ForegroundColor Red
    # Line-level, so the finding is readable rather than a hex dump.
    $linuxLines = [System.IO.File]::ReadAllLines($linuxReport)
    $winLines   = [System.IO.File]::ReadAllLines($winReport)
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
    Fail @'
the two toolchains decoded the owner's worlds DIFFERENTLY.

        This is a real finding, not a flaky test. The determinism claim that
        justifies banning float/double from sim-core does not hold on these
        two targets. Do NOT regenerate either report to make them agree.

        Both reports are in dist/ -- keep them.
'@
}

Write-Host ''
Write-Host '  IDENTICAL -- the two toolchains decoded every shipped world to the same state.' -ForegroundColor Green
Write-Host ''
Write-Host '=== PASS ==='
Write-Host '  linux/gcc     : 57 content cases + report'
Write-Host '  mingw/windows : 57 content cases + report'
Write-Host '  comparison    : byte-for-byte identical'
exit 0
