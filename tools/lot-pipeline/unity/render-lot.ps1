<#
.SYNOPSIS
  Batch-render Lord of Trojia rigs to alpha PNGs, or export them as glTF/GLB, with the Unity Editor.

.DESCRIPTION
  Copies LotSpriteRenderer.cs (+ its asmdef) into <LOT>/Assets/Editor/LotPipeline/, runs
  the matching Unity Editor in -batchmode (NOT -nographics: Camera.Render and the texture
  blits need a GPU), executes the job, then removes the script from LOT again so the LOT
  working tree is left exactly as it was found.

  Modes:
    viewmodel | portraits  PNG sprites under -Out/<jobName>/ (+ frames.json). Default -Out is
                           content/art/lot/unity-renders/ in this worktree (gitignored).
    gltf                   static Synty prefabs -> -Out/static/<pack>/<prefab>.gltf + .bin + atlas
                           (glTFast, already in LOT). Default -Out is content/art/lot-3d/ (gitignored).
    glb                    animated humanoids -> -Out/characters/<rig>.glb with Malbers clips baked
                           (UnityGLTF). UnityGLTF is not in LOT: for the run this script adds
                           "org.khronos.unitygltf" to <LOT>/Packages/manifest.json (backing up
                           manifest.json + packages-lock.json) and restores both afterwards, so LOT
                           is untouched at the end. -KeepPackage leaves it in. Unity fetches the
                           package over git on the first run (git.exe must be on PATH; it is).
  After a gltf/glb run the ledger step runs by itself:
    python tools/lot-pipeline/lot3d-manifest.py --out <Out>
  (verifies every file, writes <Out>/MANIFEST.md + manifest.json and the docs/asset-manifest-lot.md section).

.PARAMETER Job      Path to a job JSON (see jobs/). Required.
.PARAMETER Mode     viewmodel | portraits | gltf | glb. Default: inferred from the job file name
                    (portrait* -> portraits, *3d-static* -> gltf, *3d-rig* -> glb, else viewmodel).
.PARAMETER Out      Output root. Default: <repo>/content/art/lot/unity-renders (sprites) or
                    <repo>/content/art/lot-3d (gltf/glb). Point it at another worktree's
                    content/art/lot-3d to export straight into that tree.
.PARAMETER Lot      LOT project path. Default: C:\repositories\LordOfTrojia-MVP
.PARAMETER Unity    Unity.exe. Default: resolved from <Lot>/ProjectSettings/ProjectVersion.txt
                    under C:\Program Files\Unity\Hub\Editor\<version>\Editor\Unity.exe
.PARAMETER UnityGltf  The package reference written into LOT's manifest for glb runs.
.PARAMETER KeepScript  Leave the .cs/.asmdef in LOT (for interactive tuning via Tools/Granadad).
.PARAMETER KeepPackage Leave UnityGLTF in LOT's manifest after a glb run.
.PARAMETER NoLedger    Skip the lot3d-manifest.py step after a gltf/glb run.
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
.PARAMETER TimeoutMinutes  Kill the editor and fail if it has not exited by then (default 20;
                    use 40 for the first glb run, which also fetches and compiles UnityGLTF).
                    A warm render takes 1-3 min; anything longer is a dialog waiting
                    for a human (licence, safe mode, save prompt).

.EXAMPLE
  pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/viewmodel-fists.json
  pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/portraits-synty-heroes.json -Mode portraits
  pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/docks-3d-static.json -Out C:\repositories\fom-3d\content\art\lot-3d
  pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/docks-3d-rigs.json -Out C:\repositories\fom-3d\content\art\lot-3d -TimeoutMinutes 40
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Job,
    [ValidateSet('', 'viewmodel', 'portraits', 'gltf', 'glb')] [string] $Mode = '',
    [string] $Out = '',
    [string] $Lot = 'C:\repositories\LordOfTrojia-MVP',
    [string] $Unity = '',
    [string] $UnityGltf = 'https://github.com/KhronosGroup/UnityGLTF.git#release/2.21.0',
    [switch] $KeepScript,
    [switch] $KeepPackage,
    [switch] $NoLedger,
    [switch] $Windowed,
    [int] $TimeoutMinutes = 20
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..\..\..')).Path

$Job = (Resolve-Path $Job).Path
if (-not $Mode) {
    $leaf = Split-Path -Leaf $Job
    $Mode = if ($leaf -like 'portrait*') { 'portraits' }
            elseif ($leaf -like '*3d-static*') { 'gltf' }
            elseif ($leaf -like '*3d-rig*') { 'glb' }
            else { 'viewmodel' }
}
$is3d = $Mode -eq 'gltf' -or $Mode -eq 'glb'
if (-not $Out) { $Out = if ($is3d) { Join-Path $repo 'content\art\lot-3d' } else { Join-Path $repo 'content\art\lot\unity-renders' } }
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

# ---- stage the tool into LOT (the .cs and the asmdef that gives it the package defines)
$stage = Join-Path $Lot 'Assets\Editor\LotPipeline'
$staged = @('LotSpriteRenderer.cs', 'Granadad.LotPipeline.Editor.asmdef')
New-Item -ItemType Directory -Force $stage | Out-Null
foreach ($f in $staged) { Copy-Item (Join-Path $here $f) (Join-Path $stage $f) -Force }

# The IDE integration regenerates the solution when it sees a new asmdef: it adds a
# Granadad.LotPipeline.Editor.csproj (git-ignored in LOT) and a line for it in the tracked
# .slnx/.sln. Snapshot the solution files now and put them back afterwards.
$solutionBackup = @{}
foreach ($sln in Get-ChildItem $Lot -File -Include *.slnx, *.sln -Depth 0 -ErrorAction SilentlyContinue) {
    $solutionBackup[$sln.FullName] = [IO.File]::ReadAllBytes($sln.FullName)
}

# ---- glb: UnityGLTF into LOT's manifest for the run, backed up so it can be put back exactly
$manifestPath = Join-Path $Lot 'Packages\manifest.json'
$lockPath = Join-Path $Lot 'Packages\packages-lock.json'
$backupDir = Join-Path ([IO.Path]::GetTempPath()) 'lot-pipeline-packages-backup'
$restorePackages = $false
$hadLock = Test-Path $lockPath
# UnityGLTF lazily writes Assets/Resources/UnityGLTFSettings.asset into whatever project
# it runs in (its importer touches it while importing the package's own test .glb).
# Remembered here so the run can take it out again.
$gltfSettingsAsset = Join-Path $Lot 'Assets\Resources\UnityGLTFSettings.asset'
$hadGltfSettings = Test-Path $gltfSettingsAsset
if ($Mode -eq 'glb') {
    $manifestText = [IO.File]::ReadAllText($manifestPath)
    if ($manifestText -match '"org\.khronos\.unitygltf"') {
        Write-Host "UnityGLTF already in $manifestPath -- leaving the manifest alone."
    } else {
        New-Item -ItemType Directory -Force $backupDir | Out-Null
        Copy-Item $manifestPath (Join-Path $backupDir 'manifest.json') -Force
        if ($hadLock) { Copy-Item $lockPath (Join-Path $backupDir 'packages-lock.json') -Force }
        $anchor = '"dependencies": {'
        if ($manifestText.IndexOf($anchor) -lt 0) { throw "cannot find '$anchor' in $manifestPath" }
        $line = "`n    `"org.khronos.unitygltf`": `"$UnityGltf`","
        $manifestText = $manifestText.Replace($anchor, $anchor + $line)
        [IO.File]::WriteAllText($manifestPath, $manifestText)
        $restorePackages = -not $KeepPackage
        Write-Host "Added org.khronos.unitygltf = $UnityGltf to $manifestPath for this run$(if ($restorePackages) { ' (restored afterwards; -KeepPackage leaves it)' })."
    }
}

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

$code = 1
try {
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
} finally {
    # Put LOT back the way it was: the package files first (Unity rewrote packages-lock.json
    # when it resolved UnityGLTF), then the staged script.
    if ($restorePackages) {
        Copy-Item (Join-Path $backupDir 'manifest.json') $manifestPath -Force
        if ($hadLock) { Copy-Item (Join-Path $backupDir 'packages-lock.json') $lockPath -Force }
        elseif (Test-Path $lockPath) { Remove-Item $lockPath -Force }
        Write-Host "Restored $manifestPath and packages-lock.json from $backupDir."
    }
    if ($Mode -eq 'glb' -and -not $hadGltfSettings -and -not $KeepPackage -and (Test-Path $gltfSettingsAsset)) {
        Remove-Item $gltfSettingsAsset -Force -ErrorAction SilentlyContinue
        Remove-Item "$gltfSettingsAsset.meta" -Force -ErrorAction SilentlyContinue
        Write-Host "Removed $gltfSettingsAsset (UnityGLTF wrote it during the run)."
    }
    if (-not $KeepScript) {
        foreach ($f in $staged) {
            Remove-Item (Join-Path $stage $f) -Force -ErrorAction SilentlyContinue
            Remove-Item (Join-Path $stage "$f.meta") -Force -ErrorAction SilentlyContinue
        }
        if (-not (Get-ChildItem $stage -Force -ErrorAction SilentlyContinue)) {
            Remove-Item $stage -Force -ErrorAction SilentlyContinue
            Remove-Item "$stage.meta" -Force -ErrorAction SilentlyContinue
        }
        Remove-Item (Join-Path $Lot 'Granadad.LotPipeline.Editor.csproj') -Force -ErrorAction SilentlyContinue
        foreach ($kv in $solutionBackup.GetEnumerator()) {
            if (-not (Test-Path $kv.Key)) { continue }
            $now = [IO.File]::ReadAllBytes($kv.Key)
            if ($now.Length -ne $kv.Value.Length -or [Convert]::ToBase64String($now) -ne [Convert]::ToBase64String($kv.Value)) {
                [IO.File]::WriteAllBytes($kv.Key, $kv.Value)
                Write-Host "Restored $($kv.Key) (the IDE integration had added the staged asmdef's project to it)."
            }
        }
    }
}
# Not undone here, said out loud: URP's material updater re-saves any material it finds
# missing a newer property when the editor loads it (seen: Synty's Generic_Glass.mat
# gained _SrcBlendAlpha/_DstBlendAlpha, 2026-09-11). The owner's own editor session does
# the same; `git checkout -- <file>` in LOT puts it back if it matters.

# ---- what came out
$jobJson = Get-Content $Job -Raw | ConvertFrom-Json
switch ($Mode) {
    'gltf' {
        $subdir = if ($jobJson.subdir) { $jobJson.subdir } else { 'static' }
        $outputs = @(Get-ChildItem (Join-Path $Out $subdir) -Recurse -Filter *.gltf -ErrorAction SilentlyContinue)
        Write-Host ("{0} .gltf files under {1}" -f $outputs.Count, (Join-Path $Out $subdir))
    }
    'glb' {
        $subdir = if ($jobJson.subdir) { $jobJson.subdir } else { 'characters' }
        $outputs = @(Get-ChildItem (Join-Path $Out $subdir) -Filter *.glb -ErrorAction SilentlyContinue)
        Write-Host ("{0} .glb files in {1}" -f $outputs.Count, (Join-Path $Out $subdir))
    }
    default {
        $outputs = @(Get-ChildItem (Join-Path $Out $jobJson.jobName) -Filter *.png -ErrorAction SilentlyContinue)
        Write-Host ("{0} PNGs in {1}" -f $outputs.Count, (Join-Path $Out $jobJson.jobName))
    }
}
Write-Host '--- [LotSpriteRenderer] log lines ---'
if (Test-Path $log) { Select-String -Path $log -Pattern '\[LotSpriteRenderer\]' | ForEach-Object { $_.Line } }
if ($code -ne 0) {
    Write-Host '--- last 40 log lines ---'
    if (Test-Path $log) { Get-Content $log -Tail 40 }
    exit $code
}
if ($outputs.Count -eq 0) { Write-Host 'Unity exited 0 but wrote nothing -- read the log.'; exit 3 }

if ($is3d -and -not $NoLedger) {
    $ledger = Join-Path $here '..\lot3d-manifest.py'
    Write-Host "Ledger: python $ledger --out $Out"
    & python $ledger --out $Out
    if ($LASTEXITCODE -ne 0) { Write-Host "lot3d-manifest.py reported problems (exit $LASTEXITCODE)."; exit $LASTEXITCODE }
}
