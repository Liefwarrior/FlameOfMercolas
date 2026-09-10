<#
.SYNOPSIS
  Batch-render Lord of Trojia rigs to alpha PNGs with the Unity Editor.

.DESCRIPTION
  Copies LotSpriteRenderer.cs into <LOT>/Assets/Editor/LotPipeline/, runs the
  matching Unity Editor in -batchmode (NOT -nographics: Camera.Render needs a GPU),
  executes the render job, then removes the script from LOT again so the LOT
  working tree is left exactly as it was found.

  Output lands under -Out/<jobName>/ as PNGs plus frames.json. The default -Out is
  content/art/lot/unity-renders/ in this worktree, which is gitignored: LOT assets
  are licensed and never enter the public repo as files.

.PARAMETER Job      Path to a job JSON (see jobs/). Required.
.PARAMETER Mode     viewmodel | portraits. Default: inferred from the job file name.
.PARAMETER Out      Output root. Default: <repo>/content/art/lot/unity-renders
.PARAMETER Lot      LOT project path. Default: C:\repositories\LordOfTrojia-MVP
.PARAMETER Unity    Unity.exe. Default: resolved from <Lot>/ProjectSettings/ProjectVersion.txt
                    under C:\Program Files\Unity\Hub\Editor\<version>\Editor\Unity.exe
.PARAMETER KeepScript  Leave the .cs in LOT (for interactive tuning via Tools/Granadad).

.EXAMPLE
  pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/viewmodel-fists.json
  pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/portraits-synty-heroes.json -Mode portraits
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Job,
    [ValidateSet('', 'viewmodel', 'portraits')] [string] $Mode = '',
    [string] $Out = '',
    [string] $Lot = 'C:\repositories\LordOfTrojia-MVP',
    [string] $Unity = '',
    [switch] $KeepScript
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..\..\..')).Path

$Job = (Resolve-Path $Job).Path
if (-not $Mode) { $Mode = if ((Split-Path -Leaf $Job) -like 'portrait*') { 'portraits' } else { 'viewmodel' } }
if (-not $Out) { $Out = Join-Path $repo 'content\art\lot\unity-renders' }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path

if (-not (Test-Path (Join-Path $Lot 'Assets'))) { throw "not a Unity project: $Lot" }
if (Test-Path (Join-Path $Lot 'Temp\UnityLockfile')) {
    throw "LOT is open in the Unity Editor (Temp\UnityLockfile present). Close it, or run the job from Tools/Granadad inside the editor instead."
}

if (-not $Unity) {
    $verLine = Get-Content (Join-Path $Lot 'ProjectSettings\ProjectVersion.txt') | Where-Object { $_ -match '^m_EditorVersion:\s*(\S+)' }
    $ver = $Matches[1]
    $Unity = "C:\Program Files\Unity\Hub\Editor\$ver\Editor\Unity.exe"
}
if (-not (Test-Path $Unity)) { throw "Unity not found at $Unity (install the project's version via Unity Hub, or pass -Unity)" }

$stage = Join-Path $Lot 'Assets\Editor\LotPipeline'
$src = Join-Path $here 'LotSpriteRenderer.cs'
$dst = Join-Path $stage 'LotSpriteRenderer.cs'
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $src $dst -Force

$log = Join-Path $Out ("unity-" + [IO.Path]::GetFileNameWithoutExtension($Job) + ".log")
Write-Host "Unity : $Unity"
Write-Host "Job   : $Job  ($Mode)"
Write-Host "Out   : $Out"
Write-Host "Log   : $log"

$args = @(
    '-batchmode', '-projectPath', $Lot,
    '-executeMethod', 'Granadad.LotPipeline.LotSpriteRenderer.RenderFromCommandLine',
    '-lotMode', $Mode, '-lotJob', $Job, '-lotOut', $Out,
    '-logFile', $log, '-quit'
)
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $Unity -ArgumentList $args -Wait -PassThru -NoNewWindow
$sw.Stop()

if (-not $KeepScript) {
    Remove-Item $dst -Force -ErrorAction SilentlyContinue
    Remove-Item "$dst.meta" -Force -ErrorAction SilentlyContinue
    if (-not (Get-ChildItem $stage -Force -ErrorAction SilentlyContinue)) {
        Remove-Item $stage -Force -ErrorAction SilentlyContinue
        Remove-Item "$stage.meta" -Force -ErrorAction SilentlyContinue
    }
}

$jobName = (Get-Content $Job -Raw | ConvertFrom-Json).jobName
$pngs = @(Get-ChildItem (Join-Path $Out $jobName) -Filter *.png -ErrorAction SilentlyContinue)
Write-Host ("Unity exit {0} after {1:n0}s; {2} PNGs in {3}" -f $p.ExitCode, $sw.Elapsed.TotalSeconds, $pngs.Count, (Join-Path $Out $jobName))
if ($p.ExitCode -ne 0) {
    Write-Host '--- last 40 log lines ---'
    Get-Content $log -Tail 40
    exit $p.ExitCode
}
