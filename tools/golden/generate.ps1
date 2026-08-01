<#
Regenerates native/tests/golden_java_vectors.hpp from a running JVM.

    pwsh tools/golden/generate.ps1

WHY A SCRIPT AND NOT A BUILD STEP. The generated header is committed, and the
C++ suite asserts against it. If the docker build regenerated it, the assertion
would be "the C++ agrees with whatever Java says today", which is not a golden
master -- it is a mirror, and it goes green through a Java-side regression.
Regenerating is therefore a deliberate act with a reviewable diff.

WHAT IT NEEDS. javac and java on PATH (JDK 21) and nothing else: sim-core is
zero-dependency per sim-core/build.gradle.kts, so this compiles the whole main
source set with plain javac and never touches Gradle or the network.

ASCII ONLY -- same reason as scripts/verify-windows.ps1: Windows PowerShell 5.1
reads a BOM-less .ps1 as the system ANSI code page and a single non-ASCII
character in a comment makes the file fail to parse.
#>

[CmdletBinding()]
param(
    [string] $Output
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Output) {
    $Output = Join-Path $repoRoot 'native\tests\golden_java_vectors.hpp'
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("granadad-golden-" + [guid]::NewGuid().ToString('N'))
$classes = Join-Path $work 'classes'
New-Item -ItemType Directory -Force -Path $classes | Out-Null

try {
    Write-Host '=== granadad: regenerating the Java golden vectors ==='
    Write-Host "  repo:   $repoRoot"
    Write-Host "  output: $Output"
    Write-Host "  jdk:    $((& java -version 2>&1)[0])"

    Write-Host ''
    Write-Host '--- 1. compile sim-core (zero dependencies, plain javac)'
    $sources = Join-Path $work 'sources.txt'
    Get-ChildItem -Path (Join-Path $repoRoot 'sim-core\src\main\java') -Recurse -Filter '*.java' |
        ForEach-Object { $_.FullName } |
        Set-Content -LiteralPath $sources -Encoding ascii
    $count = (Get-Content -LiteralPath $sources).Count
    Write-Host "    $count source files"
    & javac -d $classes ('@' + $sources)
    if ($LASTEXITCODE -ne 0) { throw "javac failed on sim-core (exit $LASTEXITCODE)" }

    Write-Host ''
    Write-Host '--- 2. compile the generator against them'
    & javac -cp $classes -d $classes (Join-Path $PSScriptRoot 'GoldenVectors.java')
    if ($LASTEXITCODE -ne 0) { throw "javac failed on GoldenVectors.java (exit $LASTEXITCODE)" }

    Write-Host ''
    Write-Host '--- 3. run it against the real baked worlds'
    & java -cp $classes GoldenVectors (Join-Path $repoRoot 'content') $Output
    if ($LASTEXITCODE -ne 0) { throw "GoldenVectors failed (exit $LASTEXITCODE)" }

    Write-Host ''
    Write-Host '=== done. `git diff` the header: any change is a determinism finding. ==='
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
