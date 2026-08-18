<#
Granadad: The Darkstreets -- proof that granadad-standalone.exe is what it
claims to be: ONE file that runs on a machine with nothing else on it, and
produces the SAME world, byte for byte, as the repo build.

    docker compose run --rm --build build       <- publishes the standalones
    .\scripts\verify-windows.ps1                <- the repo exe's own gate
    .\scripts\verify-standalone.ps1             <- this

WHAT IT PROVES
--------------
1. Isolation. Both standalone exes are copied to an EMPTY temp directory --
   no content\ within the walk-up's three parent levels, GRANADAD_CONTENT_DIR
   cleared. That is a friend's machine, minus the friend.
2. Determinism across the content source. granadad-twin-gate-standalone.exe
   --fingerprint from that empty directory must byte-match the world-hash
   report dist\granadad.exe's own gate produced from loose repo files
   (dist\world-hash-windows-mingw.txt, written by verify-windows.ps1). Same
   binary logic -- the standalone IS the shipped exe's compiled bytes,
   stripped, with the pack appended -- so the only variable is where the
   content came from, and the answer must not move.
3. The twin-run gate passes in that state (exit 0).
4. The game itself boots there, draws a real frame to a PNG, and the frame is
   big enough to be a real one. LOOK at it: real tiles and sprites, not the
   procedural fallback (compare dist\verify-smoke.png).
5. The extraction landed in the digest-keyed per-user cache
   (%LOCALAPPDATA%\Granadad\content-<digest>\) and a SECOND launch reuses it
   without re-extracting.
6. Regression: the repo exe still resolves content by walk-up exactly as
   before -- its world-hash report is unchanged byte for byte.

ASCII ONLY, for the same reason verify-windows.ps1 is: Windows PowerShell 5.1
reads a BOM-less .ps1 in the ANSI code page.
#>

[CmdletBinding()]
param(
    [string] $DistDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DistDir) { $DistDir = Join-Path $repoRoot 'dist' }

$standaloneGame = Join-Path $DistDir 'granadad-standalone.exe'
$standaloneGate = Join-Path $DistDir 'granadad-twin-gate-standalone.exe'
$repoGate       = Join-Path $DistDir 'granadad-twin-gate.exe'
$winHashReport  = Join-Path $DistDir 'world-hash-windows-mingw.txt'

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

function CompareBytes([string] $label, [string] $aPath, [string] $bPath) {
    $a = [System.IO.File]::ReadAllBytes($aPath)
    $b = [System.IO.File]::ReadAllBytes($bPath)
    $aHash = (Get-FileHash -LiteralPath $aPath -Algorithm SHA256).Hash
    $bHash = (Get-FileHash -LiteralPath $bPath -Algorithm SHA256).Hash
    Write-Host ("  {0}" -f $label)
    Write-Host ("    {0,-22} {1,7} bytes  sha256 {2}" -f (Split-Path -Leaf $aPath), $a.Length, $aHash)
    Write-Host ("    {0,-22} {1,7} bytes  sha256 {2}" -f (Split-Path -Leaf $bPath), $b.Length, $bHash)
    if ($aHash -ne $bHash -or $a.Length -ne $b.Length) {
        Fail @"
$label differ.

        The standalone read its content out of the extracted pack and hashed
        a DIFFERENT world from the repo binary reading loose files. Same
        compiled bytes, so the pack is not carrying what the loaders read --
        a real finding against granadad-pack-content's manifest. Keep both
        files; do not regenerate either to make them agree.
"@
    }
    Write-Host '    IDENTICAL' -ForegroundColor Green
}

Write-Host '=== granadad: the standalone exe, proven from an empty directory ==='
Write-Host "  dist dir: $DistDir"

Require $standaloneGame 'Run: docker compose run --rm --build build (it publishes the standalone).'
Require $standaloneGate 'Run: docker compose run --rm --build build (it publishes the standalone gate).'
Require $repoGate       'Run: docker compose run --rm --build build.'
Require $winHashReport  'Run: .\scripts\verify-windows.ps1 first -- it writes the Windows world-hash report this script compares against.'

# --- 0. an empty directory, and a clean environment --------------------------
# %TEMP% is under ...\AppData\Local\Temp: no content\maps\baked within three
# parent levels, which is exactly the isolation the walk-up must fail in.
$sandbox = Join-Path $env:TEMP ("granadad-standalone-verify-" + [System.Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $sandbox | Out-Null
Copy-Item -LiteralPath $standaloneGame -Destination $sandbox
Copy-Item -LiteralPath $standaloneGate -Destination $sandbox
$sbGame = Join-Path $sandbox 'granadad-standalone.exe'
$sbGate = Join-Path $sandbox 'granadad-twin-gate-standalone.exe'

$savedContentDir = $env:GRANADAD_CONTENT_DIR
Remove-Item Env:\GRANADAD_CONTENT_DIR -ErrorAction SilentlyContinue

# The cache this run will extract into. Cleared first so this run proves the
# FIRST launch, not a leftover from an earlier one.
$cacheRoot = Join-Path $env:LOCALAPPDATA 'Granadad'
if (Test-Path -LiteralPath $cacheRoot) {
    Get-ChildItem -LiteralPath $cacheRoot -Directory |
        Where-Object { $_.Name -like 'content-*' } |
        Remove-Item -Recurse -Force
}

try {
    # --- 1. the world hash, from nothing ------------------------------------
    Write-Host ''
    Write-Host '--- 1. world hash from the extracted pack vs the repo''s loose files'
    $sbHash = Join-Path $sandbox 'world-hash-standalone.txt'
    & $sbGate --fingerprint $sbHash
    if ($LASTEXITCODE -ne 0) {
        Fail "the standalone gate could not produce a world hash from an empty directory (exit $LASTEXITCODE). The pack was not found or did not extract -- run it by hand and read stderr."
    }
    Require $sbHash 'the standalone gate reported success but wrote nothing.'
    CompareBytes 'world-hash reports (loose files vs extracted pack)' $winHashReport $sbHash

    # --- 2. the twin-run gate, in isolation ---------------------------------
    Write-Host ''
    Write-Host '--- 2. the twin-run gate from the extracted pack'
    & $sbGate --ticks 2000 --walkers 128
    if ($LASTEXITCODE -ne 0) { Fail "the twin-run gate failed against the extracted pack (exit $LASTEXITCODE)." }

    # --- 3. the game itself boots and draws ---------------------------------
    Write-Host ''
    Write-Host '--- 3. granadad-standalone.exe boots from nothing and writes a frame'
    $shot = Join-Path $DistDir 'verify-standalone-smoke.png'
    if (Test-Path -LiteralPath $shot) { Remove-Item -LiteralPath $shot -Force }
    & $sbGame "--smoke=40" "--screenshot=$shot"
    $gameExit = $LASTEXITCODE
    Write-Host "    exit code: $gameExit"
    if ($gameExit -ne 0) {
        Fail "granadad-standalone.exe could not start from an empty directory (exit $gameExit). This is what a friend gets."
    }
    Require $shot 'the standalone reported success but wrote no frame.'
    $shotBytes = (Get-Item -LiteralPath $shot).Length
    Write-Host "    wrote $shot ($shotBytes bytes)"
    if ($shotBytes -lt 1000) { Fail 'the captured frame is too small to be a real PNG.' }
    Write-Host '    LOOK AT IT: real tiles and sprites, not procedural fallback' -ForegroundColor Yellow
    Write-Host '    (compare dist\verify-smoke.png from the repo exe).' -ForegroundColor Yellow

    # --- 4. the cache is digest-keyed, and the second launch reuses it ------
    Write-Host ''
    Write-Host '--- 4. the per-user cache, and the second launch'
    $cacheDirs = @(Get-ChildItem -LiteralPath $cacheRoot -Directory -ErrorAction Stop |
        Where-Object { $_.Name -match '^content-[0-9a-f]{8}-[0-9]+$' })
    if ($cacheDirs.Count -ne 1) {
        Fail "expected exactly one content-<digest> cache dir under $cacheRoot, found $($cacheDirs.Count)."
    }
    $cacheDir = $cacheDirs[0].FullName
    Write-Host "    cache: $cacheDir"
    if (-not (Test-Path -LiteralPath (Join-Path $cacheDir 'maps\baked'))) {
        Fail 'the cache dir exists but holds no maps\baked -- a half extraction.'
    }
    # Second launch: same answer, and NO re-extraction. The extractor says
    # "extracting embedded content pack" on stderr exactly when it extracts.
    $sbHash2 = Join-Path $sandbox 'world-hash-standalone-2.txt'
    $stderrFile = Join-Path $sandbox 'second-run-stderr.txt'
    $proc = Start-Process -FilePath $sbGate `
        -ArgumentList @('--fingerprint', $sbHash2) `
        -RedirectStandardError $stderrFile -NoNewWindow -Wait -PassThru
    if ($proc.ExitCode -ne 0) { Fail "the second standalone launch failed (exit $($proc.ExitCode))." }
    $secondErr = if (Test-Path -LiteralPath $stderrFile) { Get-Content -LiteralPath $stderrFile -Raw } else { '' }
    if ($secondErr -match 'extracting embedded content pack') {
        Fail 'the second launch RE-EXTRACTED the same pack. The cache is digest-keyed exactly so this never happens.'
    }
    CompareBytes 'first vs second launch' $sbHash $sbHash2

    # --- 5. regression: the repo exe still resolves by walk-up --------------
    Write-Host ''
    Write-Host '--- 5. the repo exe is untouched: walk-up still wins, hash unchanged'
    $repoHash = Join-Path $sandbox 'world-hash-repo-regression.txt'
    & $repoGate --fingerprint $repoHash
    if ($LASTEXITCODE -ne 0) { Fail "dist\granadad-twin-gate.exe failed (exit $LASTEXITCODE) -- the repo path regressed." }
    CompareBytes 'repo exe now vs verify-windows.ps1''s report' $winHashReport $repoHash
} finally {
    $env:GRANADAD_CONTENT_DIR = $savedContentDir
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host '=== PASS ===' -ForegroundColor Green
Write-Host '  isolation   : both standalones ran from an empty dir, env cleared'
Write-Host '  determinism : extracted-pack world hash == loose-files world hash, byte for byte'
Write-Host '  boot        : the game drew a real frame with nothing around it'
Write-Host '  cache       : digest-keyed, extracted once, reused on the second launch'
Write-Host '  regression  : the repo exe''s own report is unchanged'
Write-Host ''
Write-Host 'Windows reality, for whoever you send it to: an unsigned exe downloaded'
Write-Host 'from the internet trips SmartScreen ("Windows protected your PC"). That'
Write-Host 'is not fixable without a code-signing certificate -- tell your friend to'
Write-Host 'click "More info" then "Run anyway". Zipping it before sending also'
Write-Host 'avoids some browsers flagging a bare .exe download.'
exit 0
