# tools/lot-pipeline -- Lord of Trojia assets into Granadad

Re-runnable tools that stage purchased/owner-authored assets from the Lord of
Trojia Unity project (`C:\repositories\LordOfTrojia-MVP`, read-only) into
`content/art/lot/` (gitignored). Design and rulings: `docs/design/ASSET-PIPELINE-SPEC.md`.

**Git discipline.** Every byte under `content/art/lot/` is licensed or a
derivative of licensed content and never enters the public repo. Tracked here:
the tools, their JSON manifests, and the generated `MANIFEST.md` -- the only
record of what the ignored tree should contain. `content/art/lot/staged.json`
is its machine twin (dst, bytes, sha256) for a fetch/verify step.

| tool | manifest | stages | status |
|---|---|---|---|
| `lot-audio-import.ps1` | `audio-manifest.json` | `audio/{music,footsteps,sfx}/` as Ogg Vorbis 48 kHz | done, run 2026-09-10 |
| `unity/render-lot.ps1` | `unity/jobs/*.json` | `unity-renders/<job>/` alpha PNGs from Synty/Malbers rigs | committed, first run pending |
| `select-icons.py` | `icons-manifest.json` | `icons/64/<id>.png` (92) + `icons/icons-white.png` + json | done, run 2026-09-10 |
| `pack-vfx.py` | `vfx-manifest.json` | `vfx/cells/<id>.png` (16) + `vfx/vfx.png` + json | done, run 2026-09-10 |
| `lot-viewmodel-pack.py` | -- | `viewmodel/<weapon>.png` + json | spec 2.4, not written |
| `lot-manifest.ps1` / `lot-fetch.ps1` | all of the above | regenerates the combined ledger / pulls the private remote | spec 2.5, not written |

## lot-audio-import.ps1

```powershell
pwsh tools/lot-pipeline/lot-audio-import.ps1            # everything in the manifest (~1 min)
pwsh tools/lot-pipeline/lot-audio-import.ps1 -DryRun    # plan only
pwsh tools/lot-pipeline/lot-audio-import.ps1 -Only music -Force
pwsh tools/lot-pipeline/lot-audio-import.ps1 -IncludeDisabled   # also the Grass rows
```

Needs ffmpeg/ffprobe on PATH (`winget install Gyan.FFmpeg`; 8.1 with libvorbis
and libsoxr verified) and PowerShell 7.

Per row it runs one of two ffmpeg lines and then asserts the result:

```
one-shots (mono: true):
  ffmpeg -i <src> -ac 1 -ar 48000 -af "aresample=resampler=soxr:precision=28,volume=<gainDb>dB" -c:a libvorbis -q:a 4 <dst>
music / bed loops (mono: false or loop: true):
  ffmpeg -i <src>       -ar 48000 -af "aresample=resampler=soxr:precision=28,volume=<gainDb>dB" -c:a libvorbis -q:a 5 <dst>
```

1. `ffprobe` says `vorbis,48000,<1|src channels>`.
2. `loop: true` rows: the decoded sample count (raw f32 decode, byte length / 4 /
   channels) equals `round(src_samples * 48000 / src_rate)` within 1. A miss means
   Vorbis priming/padding survived into the loop and there is a click on the seam.
3. Peak dBFS of the decoded output (`astats`), written to `MANIFEST.md` so the
   `gainDb` column is set from a number.

48 kHz is done offline through soxr so the runtime decoder's linear resampler
(`native/src/audio/decode_vorbis.cpp`) never runs on these files. No `loudnorm`,
no limiter: sources are mastered packs and `gainDb` is the only knob. Music rows
keep their channel count so a later stereo voice path costs no re-encode; the
runtime decoder downmixes today.

Idempotent: an output newer than its source is kept (still probed and asserted);
`-Force` re-encodes. A row that fails any assertion has its output deleted so a
bad file is never left staged, and the tool exits 1 after the whole batch.

Manifest row shape (`variants: N` expands `{n}` to `01..N`):

```json
{ "pack": "footsteps", "src": "Footsteps Pack Expanded/SingleSteps/ConcreteSteps/ConcreteSingelSteps{n}.wav",
  "dst": "audio/footsteps/concrete/concrete_{n}.ogg", "variants": 12,
  "mono": true, "loop": false, "q": 4, "gainDb": 0, "soundId": "FootstepStone",
  "purpose": "stone footstep, variant {n} of 12", "license": "asset-store-eula", "enabled": true }
```

`soundId` is the Granadad `SoundId` / `TrackId` / `BedLoop` the file is staged
for; empty means staged and held (listed as _unmapped_ in the manifest).
`enabled: false` rows are listed but not converted.

Ledger outputs (full runs only; `-Only` runs skip them), in the same two files
and the same shape as the Python tools' `lotstage.py`:

- `content/art/lot/staged.json` -- `tools.lot-audio-import.rows` (pack, src, dst,
  bytes, sha256, purpose, license, plus soundId / loop / seconds / peakDb /
  samples / srcRate / srcChannels); other tools' entries are kept.
- `docs/asset-manifest-lot.md` -- the tracked human ledger; this tool rewrites
  only its own `lot-pipeline:begin/end lot-audio-import` section.

Nothing here touches the game: ingestion (a second bank root, new ids, surfaces,
the music director) is a later build lane, spec section 3.

## select-icons.py and pack-vfx.py (Python 3 + Pillow)

Both share `lotstage.py` (repo-root discovery, the `.gitignore` guard, sha256,
the two ledgers). Both are idempotent (byte-identical PNGs, unchanged files
skipped, `--force` to rewrite), have `--dry-run`, take `--lot-root` (default
`C:\repositories\LordOfTrojia-MVP\Assets`, read-only), and refuse to run if
`/content/art/lot/` is not in `.gitignore`. `--preview <png>` writes a named
contact sheet (a feature of the tool, not a harness).

```powershell
python tools/lot-pipeline/select-icons.py --preview icons-proof.png   # ~2 s
python tools/lot-pipeline/pack-vfx.py     --preview vfx-proof.png     # ~2 s
```

**select-icons.py** -- `icons-manifest.json` maps 92 of the 400 Artsystack
`flaticon/white/64` names onto Granadad vocabulary ids (`weapon_fists`,
`contraband_quayfire`, `lead_followed`, ...). Each is staged as
`icons/64/<id>.png` (pixels untouched; the pack's four non-square icons are
centred in the 64 px cell) and packed into `icons/icons-white.png` (16 columns)
with `icons-white.json` (`id -> cell`). Asserted: RGBA, a true white mask
(RGB within 8/255 wherever alpha > 0, so a tint by the row's ink is lossless),
one source per id. Only the white 64 px set is staged: the textured set, the
`btn_` plates, the `colored_icon` renders, the 132 chrome components and the
beige keycaps are Unity chrome that would replace the register rather than sit
in it, and device-aware key glyphs already have a licence-clean source in
`content/art/kenney-input-prompts` (CC0, in git) through the UI-EA motif channel.

**pack-vfx.py** -- `vfx-manifest.json` cuts 16 cells out of five Piloto Studio
textures: `glint_block`, `fleck_landed`, `splat_kill`, `puff_0..3`,
`flame_0..7`, `slash_hard`. Row fields: `grid [cols, rows]`, `cell` (row-major
index or `"whole"`), `bbox` (crop to the alpha bounding box, square-padded,
before scaling), `mode` (`rgba` keep colour; `mask` alpha only, RGB forced white;
`luma` for rgb24-on-black sources; `packed:r|g|b` for channel-packed masks),
`out [w, h]`. BOX downsample. Asserted: after the sampler's cutout (alpha >= 128)
a cell keeps >= 4 % of its texels, so a soft source fails here instead of drawing
nothing. Output `vfx/cells/<id>.png` plus `vfx/vfx.png` on a 32 px grid with
`vfx.json` (`cells: {id: {x, y, w, h}}`, `animations: {puff, flame}`). No tint
is baked; the spec's hues, step counts and sizes (ASSET-PIPELINE-SPEC 3.4) live in
code and nothing is wired.

Ledgers (both tools): `content/art/lot/staged.json` under `tools.<tool>.rows`,
and the tracked `docs/asset-manifest-lot.md`, one section per tool between
`lot-pipeline:begin/end <tool>` markers (a rerun replaces its own section only).
