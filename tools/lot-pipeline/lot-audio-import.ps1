#Requires -Version 7.0
<#
.SYNOPSIS
  Stage Lord of Trojia audio (WAV) as Ogg Vorbis under content/art/lot/audio/.

.DESCRIPTION
  Reads audio-manifest.json (one row per staged file), runs ffmpeg per row,
  then proves every output with ffprobe/ffmpeg:

    1. codec == vorbis, sample rate == 48000, channels == 1 (mono rows) or the
       source channel count (music rows).
    2. loop rows: decoded sample count == round(src_samples * 48000 / src_rate)
       +- 1. Vorbis priming/padding that survives into a loop is a click on the
       seam; this catches it as a number instead of an ear.
    3. peak dBFS of the decoded output, recorded so gainDb decisions are numbers.

  Sources are resampled to 48 kHz OFFLINE through soxr so the runtime decoder's
  linear resampler (native/src/audio/decode_vorbis.cpp) is bypassed. No loudnorm,
  no limiter: gainDb in the manifest is the only loudness knob.

  Idempotent: an output newer than its source is not re-encoded (it is still
  probed and its assertions re-run). -Force re-encodes everything.

  Ledger: rewrites this tool's entry in <Out>/staged.json (tools.lot-audio-import,
  the shape lotstage.py uses; other tools' entries are kept) and its own marked
  section of docs/asset-manifest-lot.md -- the tracked human ledger, the only
  record in git of what the ignored tree should contain.

  Nothing under content/art/lot/ is ever committed (.gitignore). LOT is read-only.
  ASCII only in this file (the repo's .ps1 files are 7-bit by rule).

.PARAMETER Manifest        Rows JSON. Default: <this dir>/audio-manifest.json
.PARAMETER LotRoot         LOT Assets root. Default: C:\repositories\LordOfTrojia-MVP\Assets
.PARAMETER Out             Staging root. Default: <repo>/content/art/lot
.PARAMETER Only            Wildcard on pack name or dst path (e.g. 'music', 'audio/footsteps/mud/*').
.PARAMETER IncludeDisabled Also convert rows with enabled: false.
.PARAMETER Force           Re-encode even when the output is newer than the source.
.PARAMETER DryRun          Print the plan; write nothing.
.PARAMETER SkipLedger      Convert and assert, but do not rewrite staged.json / docs/asset-manifest-lot.md.

.EXAMPLE
  pwsh tools/lot-pipeline/lot-audio-import.ps1
  pwsh tools/lot-pipeline/lot-audio-import.ps1 -Only music -Force
  pwsh tools/lot-pipeline/lot-audio-import.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [string] $Manifest = '',
    [string] $LotRoot = 'C:\repositories\LordOfTrojia-MVP\Assets',
    [string] $Out = '',
    [string] $Only = '',
    [switch] $IncludeDisabled,
    [switch] $Force,
    [switch] $DryRun,
    [switch] $SkipLedger
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$toolName = 'lot-audio-import'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..\..')).Path
if (-not $Manifest) { $Manifest = Join-Path $here 'audio-manifest.json' }
if (-not $Out) { $Out = Join-Path $repo 'content\art\lot' }
$Manifest = (Resolve-Path $Manifest).Path
if (-not (Test-Path $LotRoot)) { throw "LOT Assets root not found: $LotRoot" }
$LotRoot = (Resolve-Path $LotRoot).Path

foreach ($exe in 'ffmpeg', 'ffprobe') {
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { throw "$exe not on PATH (winget install Gyan.FFmpeg)" }
}

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

# Run a native tool with explicit argument quoting and capture ONE stream.
# ffprobe answers on stdout, ffmpeg talks on stderr; never both at once, so a
# single synchronous ReadToEnd cannot deadlock.
function Invoke-Tool([string] $exe, [string[]] $argv, [string] $capture = 'stdout') {
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = (Get-Command $exe).Source
    foreach ($a in $argv) { $psi.ArgumentList.Add($a) }
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = ($capture -eq 'stdout')
    $psi.RedirectStandardError = ($capture -eq 'stderr')
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $text = if ($capture -eq 'stdout') { $p.StandardOutput.ReadToEnd() } else { $p.StandardError.ReadToEnd() }
    $p.WaitForExit()
    return @{ ExitCode = $p.ExitCode; Text = $text }
}

function Probe-Stream([string] $path) {
    $r = Invoke-Tool 'ffprobe' @('-v', 'error', '-select_streams', 'a:0',
        '-show_entries', 'stream=codec_name,sample_rate,channels,duration_ts', '-of', 'csv=p=0', $path) 'stdout'
    if ($r.ExitCode -ne 0 -or -not $r.Text.Trim()) { throw "ffprobe failed on $path" }
    $f = $r.Text.Trim().Split(',')
    return @{ Codec = $f[0]; Rate = [int] $f[1]; Channels = [int] $f[2]; DurationTs = [long] $f[3] }
}

# Decode the whole file to raw f32 in the scratch dir: the byte length IS the
# sample count (no framing, no granule arithmetic), and astats on the same pass
# gives the peak.
function Decode-Measure([string] $path, [int] $channels, [string] $scratch) {
    $raw = Join-Path $scratch 'decode.f32'
    $r = Invoke-Tool 'ffmpeg' @('-y', '-hide_banner', '-nostats', '-i', $path,
        '-af', 'astats=measure_perchannel=Peak_level:measure_overall=none', '-f', 'f32le', $raw) 'stderr'
    if ($r.ExitCode -ne 0) { throw "ffmpeg decode failed on $path`n$($r.Text)" }
    $bytes = (Get-Item -LiteralPath $raw).Length
    Remove-Item -LiteralPath $raw -Force
    $peak = [double]::NegativeInfinity
    foreach ($m in [regex]::Matches($r.Text, 'Peak level dB:\s*(-?[\d.]+|-inf)')) {
        $v = if ($m.Groups[1].Value -eq '-inf') { [double]::NegativeInfinity } else { [double] $m.Groups[1].Value }
        if ($v -gt $peak) { $peak = $v }
    }
    return @{ Samples = [long] ($bytes / 4 / $channels); PeakDb = $peak }
}

function Expand-Rows($rows) {
    $outRows = New-Object System.Collections.Generic.List[object]
    foreach ($row in $rows) {
        $n = 1
        if ($row.PSObject.Properties['variants'] -and $row.variants -gt 1) { $n = [int] $row.variants }
        for ($i = 1; $i -le $n; $i++) {
            $tag = if ($n -gt 1) { $i.ToString('00') } else { '' }
            $r = [ordered] @{}
            foreach ($p in $row.PSObject.Properties) {
                if ($p.Name -eq 'variants') { continue }
                $v = $p.Value
                if ($v -is [string]) { $v = $v.Replace('{n}', $tag) }
                $r[$p.Name] = $v
            }
            if (-not $r.Contains('gainDb')) { $r['gainDb'] = 0 }
            if (-not $r.Contains('enabled')) { $r['enabled'] = $true }
            if (-not $r.Contains('soundId')) { $r['soundId'] = '' }
            $outRows.Add([pscustomobject] $r)
        }
    }
    return $outRows
}

function Sha256Hex([string] $path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }

# ---------------------------------------------------------------------------
# plan
# ---------------------------------------------------------------------------

$doc = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$rows = Expand-Rows $doc.rows
if ($Only) {
    $rows = @($rows | Where-Object { $_.pack -like $Only -or $_.dst -like $Only })
    if (-not $rows.Count) { throw "-Only '$Only' matched no rows" }
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) "granadad-$toolName"
New-Item -ItemType Directory -Force $scratch | Out-Null

Write-Host "Manifest : $Manifest  ($($rows.Count) rows)"
Write-Host "LOT      : $LotRoot"
Write-Host "Out      : $Out"
if ($DryRun) { Write-Host 'DRY RUN  : nothing is written' }

$errors = New-Object System.Collections.Generic.List[string]
$results = New-Object System.Collections.Generic.List[object]
$stats = @{ encoded = 0; upToDate = 0; disabled = 0; failed = 0; bytes = 0L }
$sw = [Diagnostics.Stopwatch]::StartNew()

foreach ($row in $rows) {
    $src = Join-Path $LotRoot $row.src
    $dst = Join-Path $Out $row.dst
    $label = $row.dst

    if (-not (Test-Path -LiteralPath $src)) {
        $errors.Add("missing source: $($row.src)")
        $stats.failed++
        Write-Host "  FAIL  $label  (source missing)" -ForegroundColor Red
        continue
    }
    if (-not $row.enabled -and -not $IncludeDisabled) {
        $stats.disabled++
        $results.Add([pscustomobject] @{ row = $row; staged = $false; note = 'disabled' })
        continue
    }

    $needs = $Force -or -not (Test-Path -LiteralPath $dst) -or
             ((Get-Item -LiteralPath $dst).LastWriteTimeUtc -lt (Get-Item -LiteralPath $src).LastWriteTimeUtc)

    if ($DryRun) {
        $verb = if ($needs) { 'encode' } else { 'keep  ' }
        Write-Host "  $verb $label  <-  $($row.src)"
        continue
    }

    try {
        $srcInfo = Probe-Stream $src
        $wantCh = if ($row.mono) { 1 } else { $srcInfo.Channels }

        if ($needs) {
            New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
            $gain = [double] $row.gainDb
            $af = "aresample=resampler=soxr:precision=28,volume=${gain}dB"
            $argv = @('-y', '-hide_banner', '-loglevel', 'error', '-nostats', '-i', $src)
            if ($row.mono) { $argv += @('-ac', '1') }
            $argv += @('-ar', '48000', '-af', $af, '-c:a', 'libvorbis', '-q:a', "$($row.q)", $dst)
            $r = Invoke-Tool 'ffmpeg' $argv 'stderr'
            if ($r.ExitCode -ne 0) { throw "ffmpeg exit $($r.ExitCode): $($r.Text.Trim())" }
            $stats.encoded++
        } else {
            $stats.upToDate++
        }

        # 1. container/codec/rate/channels
        $dstInfo = Probe-Stream $dst
        if ($dstInfo.Codec -ne 'vorbis' -or $dstInfo.Rate -ne 48000 -or $dstInfo.Channels -ne $wantCh) {
            throw "format assertion: got $($dstInfo.Codec),$($dstInfo.Rate),$($dstInfo.Channels) want vorbis,48000,$wantCh"
        }

        # 2 + 3. decoded length and peak
        $m = Decode-Measure $dst $dstInfo.Channels $scratch
        $expected = [long] [math]::Round($srcInfo.DurationTs * 48000.0 / $srcInfo.Rate)
        $delta = $m.Samples - $expected
        if ($row.loop -and [math]::Abs($delta) -gt 1) {
            throw "loop seam assertion: decoded $($m.Samples) samples, source resampled = $expected (delta $delta)"
        }

        $bytes = (Get-Item -LiteralPath $dst).Length
        $stats.bytes += $bytes
        $results.Add([pscustomobject] @{
            row = $row; staged = $true; note = ''
            bytes = $bytes; sha256 = (Sha256Hex $dst)
            samples = $m.Samples; seconds = [math]::Round($m.Samples / 48000.0, 3)
            peakDb = [math]::Round($m.PeakDb, 2); seamDelta = $delta
            srcCodec = $srcInfo.Codec; srcRate = $srcInfo.Rate; srcChannels = $srcInfo.Channels
        })
        $verb = if ($needs) { 'ok    ' } else { 'kept  ' }
        Write-Host ("  {0} {1,-52} {2,7:n1}s  peak {3,7:n2} dBFS  {4,6:n0} KB" -f $verb, $label, ($m.Samples / 48000.0), $m.PeakDb, ($bytes / 1KB))
    } catch {
        $errors.Add("$label : $($_.Exception.Message)")
        $stats.failed++
        Write-Host "  FAIL  $label  $($_.Exception.Message)" -ForegroundColor Red
        if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }   # never leave a bad file staged
    }
}

$sw.Stop()
Write-Host ("`n{0} encoded, {1} up to date, {2} disabled, {3} failed; {4:n1} MB staged; {5:n0}s" -f
    $stats.encoded, $stats.upToDate, $stats.disabled, $stats.failed, ($stats.bytes / 1MB), $sw.Elapsed.TotalSeconds)

if ($DryRun) { exit 0 }

# ---------------------------------------------------------------------------
# ledger: staged.json + MANIFEST.md (two copies: beside the files, and tracked)
# ---------------------------------------------------------------------------

# Both ledgers follow tools/lot-pipeline/lotstage.py (the Python tools' shared
# module) so every lane's rows live in the same two files:
#   content/art/lot/staged.json    {"schemaVersion":1,"tools":{"<tool>":{"generated","rows"}}}
#   docs/asset-manifest-lot.md     one section per tool between lot-pipeline:begin/end markers
# LF line endings, UTF-8 without BOM, sorted by dst: a byte-identical staged tree
# yields a byte-identical ledger apart from the one "generated" stamp.

function Write-Utf8Lf([string] $path, [string] $text) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
    [IO.File]::WriteAllText($path, $text.Replace("`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))
}

if (-not $SkipLedger -and -not $Only) {
    $generated = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    $staged = @($results | Where-Object staged | Sort-Object { $_.row.dst })
    $held = @($results | Where-Object { -not $_.staged })

    # --- staged.json --------------------------------------------------------
    $stagedPath = Join-Path $Out 'staged.json'
    $data = [ordered] @{ schemaVersion = 1; tools = [ordered] @{} }
    if (Test-Path -LiteralPath $stagedPath) {
        try {
            $prev = Get-Content -LiteralPath $stagedPath -Raw | ConvertFrom-Json -AsHashtable
            if ($prev -and $prev.Contains('tools')) { $data = $prev }
        } catch { Write-Host "staged.json : unreadable, rewriting" -ForegroundColor Yellow }
    }
    $rows = @($staged | ForEach-Object {
        [ordered] @{
            pack = $_.row.pack; src = $_.row.src.Replace('\', '/'); dst = $_.row.dst.Replace('\', '/')
            bytes = $_.bytes; sha256 = $_.sha256; purpose = $_.row.purpose; license = $_.row.license
            soundId = $_.row.soundId; loop = [bool] $_.row.loop; seconds = $_.seconds; peakDb = $_.peakDb
            samples = $_.samples; srcRate = $_.srcRate; srcChannels = $_.srcChannels
        } })
    $data['tools'][$toolName] = [ordered] @{ generated = $generated; rows = $rows }
    Write-Utf8Lf $stagedPath (($data | ConvertTo-Json -Depth 6) + "`n")
    Write-Host "staged.json : $stagedPath ($($rows.Count) rows under tools.$toolName; $($data['tools'].Count) tools)"

    # --- docs/asset-manifest-lot.md ------------------------------------------
    $manifestPath = Join-Path $repo 'docs\asset-manifest-lot.md'
    $header = @(
        '# LOT asset manifest -- what tools/lot-pipeline stages under content/art/lot/'
        ''
        'Generated sections. Each tool under `tools/lot-pipeline/` rewrites its own block between'
        '`lot-pipeline:begin` / `lot-pipeline:end` markers when it runs; do not hand-edit inside them.'
        '`content/art/lot/` is gitignored (`.gitignore:48`): every file listed here is a derivative of a'
        'purchased asset-store pack unless its licence column says otherwise, and none of them are in git.'
        '`content/art/lot/staged.json` is the machine twin of this file (bytes + sha256 per staged file).'
        ''
        'Columns: pack | source (relative to the LOT `Assets/`) | staged (relative to `content/art/lot/`) |'
        'bytes | sha256[:12] | purpose | licence.'
        ''
    ) -join "`n"
    $begin = "<!-- lot-pipeline:begin $toolName -->"
    $end = "<!-- lot-pipeline:end $toolName -->"

    $byPack = @($staged | Group-Object { $_.row.pack } | Sort-Object Name | ForEach-Object {
        '{0} {1} ({2:n1} MB)' -f $_.Name, $_.Count, (($_.Group | Measure-Object bytes -Sum).Sum / 1MB) }) -join ', '
    $intro = @(
        "Audio from ``tools/lot-pipeline/lot-audio-import.ps1`` (rows: ``tools/lot-pipeline/audio-manifest.json``), generated $generated."
        "Every file is Ogg Vorbis at 48 kHz, resampled offline through soxr (the runtime decoder's linear resampler never runs on"
        "these), one-shots mono q4, music stereo q5, no loudness processing: ``gainDb`` per row is the only knob and the peak"
        "in the purpose column is how it gets set. Loop rows passed the seam guard (decoded sample count == source count"
        "resampled, +-1). The purpose column leads with the Granadad ``SoundId`` / ``TrackId`` / ``BedLoop`` the file is staged"
        "for; _unmapped_ means staged and held. Ingestion is a later build lane (ASSET-PIPELINE-SPEC section 3); nothing is wired."
        ''
        ("Staged: {0} files, {1:n1} MB -- {2}. Not staged: {3} rows with ``enabled: false`` ({4})." -f
            $staged.Count, ($stats.bytes / 1MB), $byPack, $held.Count,
            ((@($held | ForEach-Object { $_.row.pack } | Sort-Object -Unique) -join ', ') + ' -- see the manifest JSON'))
    ) -join "`n"

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add($begin)
    $lines.Add('## Audio -- Dark Fantasy music loops, Footsteps Pack single steps, Malbers foley, Trojia3D bakes')
    $lines.Add('')
    $lines.Add($intro)
    $lines.Add('')
    $lines.Add('| pack | source | staged | bytes | sha256 | purpose | licence |')
    $lines.Add('|---|---|---|---|---|---|---|')
    foreach ($r in $staged) {
        $id = if ($r.row.soundId) { "**$($r.row.soundId)**" } else { '_unmapped_' }
        $purpose = ('{0} -- {1} ({2:n2} s, peak {3:n1} dBFS)' -f $id, $r.row.purpose, $r.seconds, $r.peakDb).Replace('|', '\|')
        $lines.Add(('| {0} | `{1}` | `{2}` | {3} | `{4}` | {5} | {6} |' -f
            $r.row.pack, $r.row.src.Replace('\', '/'), $r.row.dst.Replace('\', '/'), $r.bytes, $r.sha256.Substring(0, 12), $purpose, $r.row.license))
    }
    $lines.Add('')
    $lines.Add(('{0} files, {1:n0} bytes.' -f $staged.Count, $stats.bytes))
    $lines.Add($end)
    $section = ($lines -join "`n") + "`n"

    $text = if (Test-Path -LiteralPath $manifestPath) { (Get-Content -LiteralPath $manifestPath -Raw).Replace("`r`n", "`n") } else { $header }
    $bi = $text.IndexOf($begin); $ei = $text.IndexOf($end)
    if ($bi -ge 0 -and $ei -ge 0) {
        $head = $text.Substring(0, $bi)
        $tail = $text.Substring($ei + $end.Length).TrimStart("`n")
        $text = $head + $section + $(if ($tail) { "`n" + $tail } else { '' })
    } else {
        $text = $text.TrimEnd("`n") + "`n`n" + $section
    }
    Write-Utf8Lf $manifestPath $text
    Write-Host "manifest    : $manifestPath (section $toolName, tracked)"
} elseif ($Only) {
    Write-Host 'ledger      : skipped (-Only is a partial run; run without -Only to regenerate staged.json / docs/asset-manifest-lot.md)'
}

if ($errors.Count) {
    Write-Host "`n$($errors.Count) error(s):" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}
exit 0
