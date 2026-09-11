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
.PARAMETER Windowed    Run the editor with a window instead of -batchmode. The batch
                    entitlement (com.unity.editor.headless) is not part of the Hub
                    named-user licence on this machine: -batchmode exits 198 with
                    "No valid Unity Editor license found" (verified 2026-09-10).
                    A windowed editor with -executeMethod -quit needs only the ordinary
                    editor entitlement and renders exactly the same frames. The runner
                    falls back to this by itself when it sees exit 198 with entitlements
                    present; with NO entitlement at all (expired, never activated) it
                    stops instead, because a windowed editor only sits on the sign-in
                    dialog -- open Unity Hub, sign in, rerun.
.PARAMETER TimeoutMinutes  Kill the editor and fail if it has not exited by then (default 20).
                    A warm render takes 1-3 min; anything longer is a dialog waiting
                    for a human (licence, safe mode, save prompt).

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
    [switch] $KeepScript,
    [switch] $Windowed,
    [int] $TimeoutMinutes = 20
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

# Unity holds Temp\UnityLockfile while the project is open and deletes it on a clean
# exit. A crash or a licence refusal leaves a zero-byte one behind. The lock is real
# only if a Unity.exe process actually has this project open.
$lock = Join-Path $Lot 'Temp\UnityLockfile'
if (Test-Path $lock) {
    $holders = @(Get-CimInstance Win32_Process -Filter "Name='Unity.exe'" -ErrorAction SilentlyContinue |
                 Where-Object { $_.CommandLine -and $_.CommandLine -like "*$Lot*" })
    if ($holders.Count -gt 0) {
        throw "LOT is open in the Unity Editor (PID $($holders[0].ProcessId)). Close it, or run the job from Tools/Granadad inside the editor instead."
    }
    Write-Host "Stale Temp\UnityLockfile (no Unity process has $Lot open) -- removing it."
    Remove-Item $lock -Force
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

function Invoke-UnityRender([bool] $batch) {
    $unityArgs = @()
    if ($batch) { $unityArgs += '-batchmode' }
    $unityArgs += @(
        '-projectPath', $Lot,
        '-executeMethod', 'Granadad.LotPipeline.LotSpriteRenderer.RenderFromCommandLine',
        '-lotMode', $Mode, '-lotJob', $Job, '-lotOut', $Out,
        '-logFile', $log, '-quit'
    )
    Write-Host ("Launching Unity {0}..." -f $(if ($batch) { '-batchmode' } else { 'windowed (-executeMethod -quit)' }))
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $Unity -ArgumentList $unityArgs -PassThru -NoNewWindow
    if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        Write-Host "Unity has not exited after $TimeoutMinutes min -- a dialog waiting for a human (licence, safe mode, save prompt) is the usual cause. Killing it."
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        $proc.WaitForExit()
        $sw.Stop()
        return 124
    }
    $sw.Stop()
    Write-Host ("Unity exit {0} after {1:n0}s" -f $proc.ExitCode, $sw.Elapsed.TotalSeconds)
    return $proc.ExitCode
}

$code = Invoke-UnityRender (-not $Windowed)
if ($code -eq 198 -and (Select-String -Path $log -Pattern 'No valid Unity Editor license found' -Quiet)) {
    if (Select-String -Path $log -Pattern 'Found 0 entitlement groups and 0 free entitlements' -Quiet) {
        # Seen 2026-09-10: the Personal entitlement's offline validity window had lapsed
        # and the Hub had no signed-in session to renew it. Nothing here can fix that.
        Write-Host "Exit 198: no Unity entitlement on this machine at all (expired or never activated); a windowed editor would only sit on the sign-in dialog."
        Write-Host "Fix: open Unity Hub, sign in, let it refresh the licence, then rerun. Reason is logged in $env:APPDATA\UnityHub\logs\info-log.json ('Invalid license file ... reason: ...')."
    } elseif (-not $Windowed) {
        Write-Host "Exit 198: the licence has no batch (headless) entitlement. Retrying windowed."
        if (Test-Path $lock) { Remove-Item $lock -Force }   # the refused editor leaves one behind
        $code = Invoke-UnityRender $false
    }
}
if ($code -ne 0 -and (Test-Path $lock) -and -not (Get-Process -Name Unity -ErrorAction SilentlyContinue)) {
    Remove-Item $lock -Force   # a refused or killed editor never cleans up its lock
}

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
Write-Host ("{0} PNGs in {1}" -f $pngs.Count, (Join-Path $Out $jobName))
Write-Host '--- [LotSpriteRenderer] log lines ---'
Select-String -Path $log -Pattern '\[LotSpriteRenderer\]' | ForEach-Object { $_.Line }
if ($code -ne 0) {
    Write-Host '--- last 40 log lines ---'
    Get-Content $log -Tail 40
    exit $code
}
if ($pngs.Count -eq 0) { Write-Host 'Unity exited 0 but wrote no PNGs -- read the log.'; exit 3 }
