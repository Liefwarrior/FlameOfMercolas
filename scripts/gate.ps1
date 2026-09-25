<#
Granadad: The Darkstreets -- the docker half of the gate, with the commit stamped.

    .\scripts\gate.ps1

is

    docker compose run --rm --build build

run from the repo root with GRANADAD_REVISION set to `git rev-parse --short HEAD`,
plus `-dirty` when a tracked file differs from HEAD -- the rule `git describe
--dirty` uses. The container cannot work that out for itself: .git is not in the
build context (see .dockerignore), so without this dist\GATE-STAMP.txt and
`granadad.exe --version` say `unknown`. They used to say `docker`, and a stale
hash whenever somebody had once set the variable by hand; a reviewer was misled
by it. This exists so nobody has to remember the variable.

The exit code is the compose command's. `run`, not `up`: see docker-compose.yml.
Anything else -- BUILD_TYPE, say -- passes through the environment as before.
scripts/gate.sh is the same thing off Windows.

ASCII ONLY, same reason as verify-windows.ps1: Windows PowerShell 5.1 reads a
BOM-less .ps1 as the system ANSI code page.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$exit = 1
# Restored on the way out: a GRANADAD_REVISION left behind in this shell would
# make a bare compose command typed later stamp THIS commit -- the stale hash
# this script exists to end.
$previousRevision = $env:GRANADAD_REVISION

Push-Location $repoRoot
try {
    $rev = git rev-parse --short HEAD
    if ($LASTEXITCODE -ne 0 -or -not $rev) {
        Write-Host ''
        Write-Host 'FAIL: git cannot name HEAD here, so there is no revision to stamp.' -ForegroundColor Red
        Write-Host '      Run `docker compose run --rm --build build` by hand if you mean to build without one.'
        exit 1
    }
    # Refresh stat info first so a touched-but-unchanged file does not read as
    # dirty; then ask whether any tracked file differs from HEAD, staged or not.
    # Untracked files do not count, same as `git describe --dirty`.
    git update-index -q --refresh | Out-Null
    git diff-index --quiet HEAD --
    if ($LASTEXITCODE -ne 0) { $rev = "$rev-dirty" }

    Write-Host "=== granadad: gate for revision $rev ==="
    $env:GRANADAD_REVISION = $rev
    docker compose run --rm --build build
    $exit = $LASTEXITCODE
} finally {
    $env:GRANADAD_REVISION = $previousRevision
    Pop-Location
}
exit $exit
