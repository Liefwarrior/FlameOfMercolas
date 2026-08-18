<#
Granadad: The Darkstreets -- build-free packaging of granadad-standalone.exe.

    powershell -ExecutionPolicy Bypass -File .\scripts\package-standalone.ps1

THIS IS HOW YOU RE-PACK AFTER A CONTENT TWEAK. Edit anything under content\
that ships (raws, maps\baked, the four art files, any OGG the compiled sound
manifest names), run this, send the exe. No docker, no compiler, no test
cycle. It reuses two artifacts the last green gate already published to
dist\ --

    granadad-pack-content.exe    the packer, cross-built by the gate
    granadad-base-stripped.exe   granadad.exe's exact compiled bytes,
                                 stripped in-container (where mingw-strip
                                 lives), BEFORE any pack was appended

-- runs the packer over the repo's content\, and concatenates base + pack
into dist\granadad-standalone.exe. Seconds, not minutes: packaging is
"take already-built artifacts + content files, pack, concatenate", and
nothing in that sentence needs a compiler.

WHAT IT REFUSES, LOUDLY, INSTEAD OF PRODUCING A STALE EXE
---------------------------------------------------------
1. A missing packer or base. Either the gate has never run, or it ran before
   packaging was decoupled. One green gate publishes both, forever after.
2. A packer, base, or dist\granadad.exe whose sha256 does not match what
   dist\BUILD-MANIFEST.txt records. The manifest is written once per gate
   run and hashes every artifact of THAT run -- so three matches prove the
   three files are siblings from the same revision. A mismatch means a
   leftover from a different gate is sitting in dist\, and appending fresh
   content to stale code would ship stale code with a new date on it.
3. A packer that exits non-zero. It refuses on any file the compiled sound
   manifest names that the tree does not have, rather than packing a game
   quietly missing a sound.
On any refusal the existing dist\granadad-standalone.exe is left exactly as
it was: the output is assembled under a temp name and only moved into place
after everything upstream succeeded.

CODE CHANGES STILL NEED THE GATE. This script never compiles; if native\
changed, what comes out of here is the OLD code carrying NEW content. The
manifest check pins the base to the same gate run as dist\granadad.exe --
it cannot see uncommitted .cpp edits. Run the gate for those:
    docker compose run --rm --build build

Prove the product any time with scripts\verify-standalone.ps1. One caveat
after a content tweak: its step 1 compares world hashes against
verify-windows.ps1's report, which describes the OLD content -- a hash
difference there is then expected and correct. Re-run verify-windows.ps1
first to refresh the reference.

ASCII ONLY, like the other two scripts: Windows PowerShell 5.1 reads a
BOM-less .ps1 in the ANSI code page.
#>

[CmdletBinding()]
param(
    [string] $DistDir,
    [string] $ContentDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DistDir)    { $DistDir    = Join-Path $repoRoot 'dist' }
if (-not $ContentDir) { $ContentDir = Join-Path $repoRoot 'content' }

$packerExe   = Join-Path $DistDir 'granadad-pack-content.exe'
$baseExe     = Join-Path $DistDir 'granadad-base-stripped.exe'
$gameExe     = Join-Path $DistDir 'granadad.exe'
$manifest    = Join-Path $DistDir 'BUILD-MANIFEST.txt'
$outExe      = Join-Path $DistDir 'granadad-standalone.exe'

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

Write-Host '=== granadad: package the standalone from published artifacts (no build) ==='
Write-Host "  dist:    $DistDir"
Write-Host "  content: $ContentDir"

$gateHint = 'Run one green gate: docker compose run --rm --build build  (it publishes the packer and the stripped base; after that this script never needs it for content tweaks).'
Require $packerExe $gateHint
Require $baseExe   $gateHint
Require $gameExe   'dist\granadad.exe is the revision anchor this script pins the base and packer to. Run: docker compose run --rm --build build'
Require $manifest  'Without BUILD-MANIFEST.txt there is no way to prove the base and packer came from the same gate run as dist\granadad.exe. Run: docker compose run --rm --build build'
if (-not (Test-Path -LiteralPath $ContentDir -PathType Container)) {
    Fail "$ContentDir is not a directory. Pass -ContentDir if the repo layout moved." 2
}

# --- 1. the three artifacts are siblings of ONE gate run ---------------------
# BUILD-MANIFEST.txt ends in `sha256sum *` over /out at publish time, so every
# artifact of that run is pinned by hash under the revision named at the top.
# Matching all three files against the ONE manifest is the whole revision
# check: a stale base (or packer, or a hand-copied granadad.exe) hashes to
# something the manifest never recorded, and that is a refusal, not a warning.
Write-Host ''
Write-Host '--- 1. pinning the packer and the base to dist\granadad.exe''s gate run'

$manifestLines = Get-Content -LiteralPath $manifest
$revisionLine = $manifestLines | Where-Object { $_ -match '^revision:\s+(\S.*)$' } | Select-Object -First 1
$revision = if ($revisionLine -match '^revision:\s+(\S.*)$') { $Matches[1].Trim() } else { '(unrecorded)' }
Write-Host "    manifest revision: $revision"

$recorded = @{}
foreach ($line in $manifestLines) {
    if ($line -match '^([0-9a-f]{64})\s+(\S+)\s*$') {
        $recorded[$Matches[2]] = $Matches[1]
    }
}
if ($recorded.Count -eq 0) {
    Fail 'dist\BUILD-MANIFEST.txt carries no sha256 block at all -- it is not a manifest this script knows how to trust. Run the gate.'
}

foreach ($file in @($gameExe, $baseExe, $packerExe)) {
    $name = Split-Path -Leaf $file
    if (-not $recorded.ContainsKey($name)) {
        Fail "dist\BUILD-MANIFEST.txt does not record $name at all. The manifest predates decoupled packaging (the gate run that wrote it never published this artifact). Run one gate: docker compose run --rm --build build"
    }
    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $recorded[$name]) {
        Fail @"
$name does not hash to what dist\BUILD-MANIFEST.txt records.
        recorded: $($recorded[$name])
        actual:   $actual

        The file in dist\ is STALE -- from a different gate run than the
        manifest (and so, than dist\granadad.exe). Packing new content onto
        it would ship code of an unknown revision. Refusing. Run the gate
        once to bring dist\ back to one consistent run.
"@
    }
    Write-Host ("    {0,-28} matches the manifest" -f $name)
}
Write-Host "    ok: base + packer + granadad.exe are one gate run ($revision)"

# --- 2. the pack, written by the gate's own cross-built packer ---------------
Write-Host ''
Write-Host '--- 2. packing content\ (the packer refuses on any missing manifest file)'
$packFile = Join-Path $env:TEMP ("granadad-content-" + [System.Guid]::NewGuid().ToString('N').Substring(0, 8) + ".pack")
$tmpOut = "$outExe.packing"
try {
    & $packerExe --content $ContentDir --out $packFile
    if ($LASTEXITCODE -ne 0) {
        Fail "granadad-pack-content.exe exited $LASTEXITCODE. Read its stderr above: it names the exact content file that is missing or unreadable. Nothing was written to dist\."
    }
    Require $packFile 'the packer reported success but wrote no pack.'

    # --- 3. concatenate: base + pack = standalone ----------------------------
    # Assembled under a temp name and MOVED into place, so a failure anywhere
    # above -- or a mid-write crash here -- can never leave a half-written or
    # stale-but-fresh-looking granadad-standalone.exe behind.
    Write-Host ''
    Write-Host '--- 3. concatenating base + pack -> granadad-standalone.exe'
    Copy-Item -LiteralPath $baseExe -Destination $tmpOut -Force
    $outStream = [System.IO.File]::Open($tmpOut, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write)
    try {
        $inStream = [System.IO.File]::OpenRead($packFile)
        try     { $inStream.CopyTo($outStream) }
        finally { $inStream.Dispose() }
    } finally {
        $outStream.Dispose()
    }
    Move-Item -LiteralPath $tmpOut -Destination $outExe -Force
} finally {
    Remove-Item -LiteralPath $packFile -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $tmpOut   -Force -ErrorAction SilentlyContinue
}

$stopwatch.Stop()
$outInfo = Get-Item -LiteralPath $outExe
$outHash = (Get-FileHash -LiteralPath $outExe -Algorithm SHA256).Hash.ToLowerInvariant()

Write-Host ''
Write-Host '=== PACKAGED ===' -ForegroundColor Green
Write-Host ("  {0}" -f $outExe)
Write-Host ("  {0:N0} bytes  sha256 {1}" -f $outInfo.Length, $outHash)
Write-Host ("  base revision {0}, content packed from {1}" -f $revision, $ContentDir)
Write-Host ("  elapsed: {0:N1} s, zero compilation, zero docker" -f $stopwatch.Elapsed.TotalSeconds)
Write-Host ''
Write-Host 'Prove it: powershell -ExecutionPolicy Bypass -File .\scripts\verify-standalone.ps1'
Write-Host '(after a content tweak, re-run verify-windows.ps1 first -- its world-hash'
Write-Host 'report describes the old content and step 1 would rightly flag the change).'
exit 0
