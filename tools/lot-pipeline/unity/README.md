# LOT Unity export tool — 3D rigs to 2D sprites, and to glTF/GLB for the raylib renderer

Turns Lord of Trojia's 3D assets (Synty humanoids, Malbers humanoid clips, Synty
weapons) into what Granadad can put on screen: alpha PNG frames — a first-person
**viewmodel** (arms + weapon per combat state) and head-and-shoulders
**portraits** (per character prefab, several views) — and, for the 3D renderer,
**gltf** (static prefabs as `.gltf`+`.bin`, one atlas per pack) and **glb**
(humanoids as one `.glb` each with the Malbers clips baked onto their bones).
The 3D modes are described in [3D modes](#3d-modes-gltf--glb) below.

Nothing here is copied into LOT permanently and nothing rendered is committed:
every output is a derivative of purchased asset-store content and stays under
`content/art/lot/` (gitignored). Only this tool, its job files and `MANIFEST.md`
go to git.

## Files

| file | what |
|---|---|
| `LotSpriteRenderer.cs` | Unity Editor script. `RenderFromCommandLine` (batch) or `Tools/Granadad/*` menu items. Modes: viewmodel, portraits, gltf, glb. |
| `Granadad.LotPipeline.Editor.asmdef` | Staged beside the .cs: references glTFast/UnityGLTF/URP and turns the packages into the `LOT_GLTFAST` / `LOT_UNITYGLTF` defines. |
| `render-lot.ps1` | Runner. Copies the .cs + asmdef into `<LOT>/Assets/Editor/LotPipeline/`, runs Unity `-batchmode`, removes them again; for glb also adds/restores UnityGLTF in LOT's manifest and runs the ledger step. |
| `jobs/docks-3d-static.json` | The curated docks set: 154 Synty prefabs (Generic Base walls/floors/doors/roofs/stairs, Knights houses/castle/canal/paths/props, DungeonRealms barrels/crates/lanterns, water planes, 11 atlas weapons) → `content/art/lot-3d/static/<pack>/`. |
| `jobs/docks-3d-rigs.json` | Five rigs → `content/art/lot-3d/characters/<name>.glb`: `townsman`, `dockhand`, `watchman` (10 clips each, index 0–7 = `ActorClip`), `viewmodel_fists`, `viewmodel_sword` (8 clips, index = `ViewmodelState`). |
| `../lot3d-manifest.py` | After a gltf/glb run: verifies every file, writes `content/art/lot-3d/MANIFEST.md` + `manifest.json` and the `lot3d` section of `docs/asset-manifest-lot.md`. |
| `jobs/viewmodel-fists.json` | Bare-hand viewmodel: idle / charge / swing×3 / hard_swing×4 / block / cast / hit (`Weapon::Fists`, and `Improvised` until a bottle prefab is picked). |
| `jobs/viewmodel-sword.json` | Same states with `SM_Wep_Sword_01` in the right hand and the one-hand-sword clips (`Weapon::Edged`). |
| `jobs/viewmodel-blunt.json` | `SM_Wep_Mace_01` (Synty FantasyHero) with the one-hand-sword clips — a one-handed mace swings like a one-handed sword (`Weapon::Blunt`). |
| `jobs/viewmodel-evictor.json` | `SM_Wep_Mace_Large_01` (Synty DungeonRealms) with the two-hand-axe clips: idle, Swing_Right for charge/swing, Swing_Ground for the hard swing (`Weapon::Evictor`). |
| `jobs/portraits-synty-heroes.json` | First 12 `Chr_FantasyHero_Preset_*` heads, four views each, 128×160. |

## Prerequisites (this machine, verified 2026-09-10)

- Unity **6000.3.6f1** at `C:\Program Files\Unity\Hub\Editor\6000.3.6f1\Editor\Unity.exe`
  (the exact version in `LOT/ProjectSettings/ProjectVersion.txt`; 6000.3.19f1 is also installed).
- LOT at `C:\repositories\LordOfTrojia-MVP` with its `Library/` already imported
  (it is — a cold import would add ~10–20 min to the first run).
- The LOT project must be **closed** in the editor for a batch run (Unity holds
  `Temp/UnityLockfile`). The runner refuses to start if a Unity process has LOT
  open; a lockfile with no such process (a crash, a licence refusal) is stale
  and the runner removes it.
- **A valid Unity licence.** The first run (2026-09-10) died on this: the Unity
  Personal entitlement cached in `%LOCALAPPDATA%\Unity\licenses\UnityEntitlementLicense.xml`
  has an offline validity window that expired on 2026-08-14 (Hub log: "Entitlement
  group offline validity period is expired"), and the Hub had no signed-in session
  to renew it. Exit code 198, "No valid Unity Editor license found". **Fix: open
  Unity Hub, sign in once, let it refresh the licence** (Personal renews itself
  from the account), then rerun. Nothing in this repo can do that step. Done
  2026-09-11 06:58; `-batchmode` then ran with the headless entitlement present.
- **LOT must compile.** `-batchmode` aborts before `-executeMethod` on any C# error in
  the project. On 2026-09-11 one uncommitted LOT edit did not
  (`Assets/Trojia3D/Scripts/Editor/TourneyValeBuilder.cs:586`, `Object.` → `UnityEngine.Object.`,
  an ambiguity between `UnityEngine.Object` and `System.Object`); that one token was
  changed in the owner's working copy so the export could run.
- `-batchmode` additionally needs the `com.unity.editor.headless` entitlement,
  which the Hub named-user licence carries. If a future licence lacks it, the
  runner falls back to a windowed editor (`-executeMethod -quit` without
  `-batchmode`) by itself, or pass `-Windowed`. With no entitlement at all it
  stops and says so instead (a windowed editor would only sit on the sign-in
  dialog). Either mode is killed after `-TimeoutMinutes` (20) — a warm render is
  1–3 min, anything longer is a dialog. The script exits the editor itself in
  both modes and puts the editor back on the scene it had open.
- Blender is **not** installed and is not needed.

## Run

```powershell
# from the fom-assets worktree root
pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/viewmodel-fists.json
pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/viewmodel-sword.json
pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/portraits-synty-heroes.json
```

Output: `content/art/lot/unity-renders/<jobName>/<state>_<n>.png` + `frames.json`
(one record per PNG: state, clip asset, clip name, sampled time, size, rig, weapon).
Expect 1–3 min per job on a warm Library (the editor's cold start is ~2 min of that;
the work itself is seconds); the Unity log is written beside the output. The sprite
jobs have not been run yet — the 3D modes below have (2026-09-11).

Interactive tuning (grip offsets, camera, light): run with `-KeepScript`, open LOT
in the editor, use `Tools/Granadad/Render Viewmodel Job...`, edit the job JSON,
repeat. Delete `Assets/Editor/LotPipeline/` from LOT when done (or rerun the
runner without `-KeepScript`).

## How it renders

1. New empty scene, flat ambient + one directional light, no shadows, no post, no fog.
2. Rig prefab instantiated, unpacked, every `MonoBehaviour` stripped (Synty's
   `CharacterRandomizer`, Malbers controllers). The prefab's own `Animator` +
   Humanoid avatar is kept when valid (Synty presets ship one); `avatarModel`
   (`ModularCharacters.fbx`, animationType=3) is the fallback for bare rigs.
3. Renderers kept only if the GameObject was **active in the prefab** and its name
   contains a `keepRenderers` substring. Preset prefabs carry all 720 modular parts
   with the chosen ones enabled, so `ArmUpper/ArmLower/Hand/ShoulderAttach/ElbowAttach`
   yields exactly that preset's arms.
4. Optional weapon prefab parented to `HumanBodyBones.RightHand` (fallback: a
   transform named `Hand_R`) with `localPos/localEuler/localScale` from the job.
   **Grip offsets ship as zero — tune once per weapon in the editor and write the
   numbers back into the job.**
5. Each state samples a Malbers humanoid clip at normalised `times[]` via
   `AnimationMode.SampleAnimationClip` (Mecanim retargets Malbers → Synty skeleton).
6. Viewmodel camera: at the Head bone + `eyeOffset` in **character** space, looking
   along the character's forward with a fixed `pitchDeg`, so the head animation
   never shakes the frame. Portrait camera: `distance` in front of the head bone,
   looking at it, yawing the character and pitching the camera per view.
7. `Camera.Render()` into an ARGB32 RenderTexture cleared to (0,0,0,0) → `ReadPixels`
   → PNG. URP camera data is forced to Base / no post / no AA / no shadows so alpha
   survives.

## What the source can and cannot give

- Malbers `Common/Human Anims` clips are Humanoid (retargetable). Combat set:
  `Attacks/H_Punching Left|Right`, `H_Punching Right_Aereal`, `H_Kick Right`;
  `Block/H_Block_Unharmed` (clips `H_Block_Disarmed`, `_Simple`), `H_Block_Axe`,
  `H_Block_Shield`, `H_Parry_Axe|Shield`; `Idle/Idle_Combat` and five idles;
  `Hit/H_Hit_Front|Left|Right`; `Recover/S_Recover_FaceDown|FaceUp`;
  `Weapons/Sword_OneHand/*` (Left, Right (+`_Charge`), Stab, SwipeUp, 360,
  Finisher1, three aerials), `Spear/*` (Left, Right, Stab, Idle), `Axe_2Hand/*`
  (Swing Left/Right/Up/Ground, Idle), `Spell_1H/H_1HSpell_Blast_Start|Loop|End`,
  `Bow/*`, `Throwable/*`, `Torch/S_Hold_Torch`, `DrawStore/Weapon_Draw`,
  `Deaths/H_Death1|3` (+`H_Death2.anim`), `Knock Back`, `Dodge Roll/*`.
- Synty heads have **no blendshapes**: portraits get views, not expressions.
  Expression variety must come from head/hair/beard/headwear combos (23 male +
  23 female heads, 38 hair, 18 beards, 28 head coverings, 13 helmet attachments)
  and from Granadad's own face-parts system, which already exists
  (`content/art/faces/`, FACES-SPEC.md, "zero copied pixels"). Rendered Synty
  portraits therefore compete with a shipped design decision — treat them as a
  proposal for the owner, not a replacement.
- Malbers' own rig (`Animal Controller/Human/Model/Steve_v2.fbx`, Humanoid,
  `SteveNaked.tga.png`) is a second viable viewmodel body: set `rigPrefab` to the
  FBX path and `keepRenderers` to `[]` if the Synty arms read wrong. Its punch
  clips were authored on it, so there is no retarget.

## Downstream

- `content/art/lot/` is gitignored; the pipeline stages there.
- `python tools/lot-pipeline/lot-viewmodel-pack.py --proof proof.png` packs every
  `unity-renders/viewmodel-*/frames.json` into `content/art/lot/viewmodel/<weapon>.png`
  (4 × 3 grid of 192×144 cells, alpha cut at 128) + `<weapon>.json` (state → cell
  indices) and writes the ledgers. Until the render has run,
  `lot-viewmodel-silhouette.py` produces `<weapon>-silhouette.*` in the same
  schema from code (no LOT pixels). See `../README.md`.
- Drawing the sheet is the viewmodel feature (ASSET-PIPELINE-SPEC 3.5): a
  screen-space cutout blit between `drawSignage` and the impact washes, under
  the HUD — a separate lane, not this tool.

## 3D modes (gltf / glb)

```powershell
# from the fom-assets worktree root; -Out may point at another worktree's content/art/lot-3d
pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/docks-3d-static.json -Out C:/repositories/fom-3d/content/art/lot-3d
pwsh tools/lot-pipeline/unity/render-lot.ps1 -Job tools/lot-pipeline/unity/jobs/docks-3d-rigs.json   -Out C:/repositories/fom-3d/content/art/lot-3d -TimeoutMinutes 40
python tools/lot-pipeline/lot3d-manifest.py --out C:/repositories/fom-3d/content/art/lot-3d   # the runner does this itself
```

**gltf** (glTFast 6.14.1, already resolved in LOT as a dependency of `com.unity.ai.assistant`):
every prefab named in the job is instantiated, unpacked, stripped of behaviours and
LOD1+, its materials swapped for URP/Lit clones carrying the pack atlas in `_BaseMap`
(Synty's own shader graphs use `_Albedo_Map` / `_Texture_Map`, which no exporter reads —
without the swap every mesh comes out untextured), then written as
`static/<pack>/<prefab>.gltf` + `.bin` with the atlas PNG copied once per pack folder
(`.glb` would embed the 1–4 MB atlas in every file). Meshes only — no lights, no
cameras. `manifest.json` records source prefab, role (world/prop/weapon), tris, bounds
in Unity and glTF space, materials/textures; `contact-sheet.png` (+ `.json` cell names)
is the proof. 154 prefabs take ~3 s once the editor is up (first real run 2026-09-11:
66 PolygonGeneric, 61 PolygonKnights, 22 PolygonDungeonRealms, 5 PolygonFantasyHeroCharacters,
10 atlas PNGs, 12 MB).

Three things the tool does that glTFast's own menu entry does not, each found by the
first run hanging or dying (all in `LotSpriteRenderer.cs`, look for the date):

- **It pumps the job system.** glTFast's export is `async` and spins on `JobHandle.IsCompleted`;
  a job scheduled from the main thread never starts until `JobHandle.ScheduleBatchedJobs()`
  or the end of a player-loop frame — and a blocking `-executeMethod` never ends a frame.
  The exclusive `SynchronizationContext` that drains the export's continuations kicks
  the batched jobs on every pass.
- **It hands glTFast readable mesh copies.** Synty imports every mesh with Read/Write
  off; for those glTFast reads the vertex buffers back through `AsyncGPUReadback`, an
  engine `Awaitable` that only completes from the player loop. The Editor can read
  the source data regardless of the flag (only a Player enforces it), so a script-made
  copy of each mesh goes to the exporter instead.
- **It writes its own material.** `FlatMaterialExport`/`AtlasImageExport`: the glTF
  material is exactly baseColorTexture = the atlas + baseColorFactor, metallic 0,
  roughness 1, BLEND for the water/glass clones; the atlas PNG is *copied from disk*
  once per pack folder. glTFast's default picks JPEG q60 for any texture imported
  without alpha (most Synty atlases) and re-encodes it for every prefab.

**glb** (UnityGLTF 2.21.0): the rig is spawned exactly as for the viewmodel sprites
(prefab's Humanoid avatar or `avatarModel`, `keepRenderers`, optional weapon on
`Hand_R`), materials flattened the same way, then **every active SkinnedMeshRenderer
and every bone-parented MeshRenderer is merged into ONE SkinnedMeshRenderer over one
bone list** — raylib reads `skins[0]` only and indexes every primitive's joints
against it, so the 700-part Synty presets and the Knights' item meshes must arrive
as a single skin. Clips are cloned from the Malbers FBX, renamed to the job's names
and put on an in-memory AnimatorController in job order; UnityGLTF samples each
humanoid clip on the rig's own bones at 30 fps (LINEAR node TRS; raylib resamples to
its own 60 keyframes/s on load, which is the rate the C++ side counts in; the scene
root stays at the origin — the sim owns position) and writes `characters/<name>.glb`
with the atlas embedded. **Animation index = job order = the C++ enum** (`ActorClip`
for actors, `ViewmodelState` for the arms); names match too, so either works from
raylib. FantasyHero presets lose their per-preset tint colours (masks are not baked;
the plain atlas colours ship). A run is ~35 s of editor time on a warm Library.

The single skin is shaped for what the pinned raylib (6.0, `rmodels.c`) actually does,
verified against the source 2026-09-11 — not for the glTF spec in general:

- raylib **never reads `inverseBindMatrices`**: it skins with
  `inverse(joint world at rest) × joint world animated` after the mesh node's own
  world matrix. So the combiner rebases every vertex into the rig root's space at the
  rig's current (rest = bind) pose and writes each bind pose as
  `bone.worldToLocal × root.localToWorld`, so rest == bind for every loader. Without
  this the FantasyHero rigs (vertices in centimetres under a `Root` scaled 0.01,
  spec-legal) load 100× too big.
- raylib composes joints **in index order from their parent joint**: `joints[0]` is the
  only one that gets its ancestors' static transform, a joint whose parent is not a
  joint hangs at the origin, and a parent listed after its child is skipped. So the
  joint list is the whole skeleton subtree in depth-first order — `Root` at index 0
  (it carries the 0.01), `Hips`, spine, every fingertip and attachment point — 52
  joints for the Generic peasant, 63 for FantasyHero, 51 for the Knights soldier,
  all under the 128-bone GPU cap. Inactive subtrees (a soldier's alternate helmet)
  are removed rather than listed: UnityGLTF does not export inactive nodes and drops
  a skin whose bone node is missing. The weapon is merged as a rigid part weighted
  100% to `Hand_R` and its own transform is not a joint.
- UnityGLTF samples humanoid clips inside an Undo group it then `PerformUndo()`s;
  `AnimatorStateMachine.AddState` registers the controller's states with Undo as well,
  so the tool clears and closes the group before exporting (the second clip used to
  find a destroyed `AnimatorState`).

Grip offsets for the sword viewmodel (`weapon.localPos/localEuler`) ship as zero and
have not been eyeballed: the blade is fused into `viewmodel_sword.glb` at whatever
angle `Hand_R`'s local Y gives it. Tune once in the editor, write the numbers into the
job, rerun.

UnityGLTF is not part of LOT. For a glb run the runner backs up
`<LOT>/Packages/manifest.json` + `packages-lock.json`, adds
`"org.khronos.unitygltf": "https://github.com/KhronosGroup/UnityGLTF.git#release/2.21.0"`,
lets Unity fetch and compile it (git on PATH; the fetch is cached by UPM after the
first run, and 2.21.0 compiles clean against 6000.3.6f1 / URP 17.3 / VisualScripting
1.9.9), and copies both files back afterwards; it also removes the
`Assets/Resources/UnityGLTFSettings.asset` the package writes into whatever project
it lands in — LOT's tree is byte-identical at the end, `git status` shows nothing new
(`-KeepPackage` leaves the package and the settings asset in for interactive work).
This was chosen over a sibling exporter project with directory junctions: the junction
project would need its own cold import of Synty + Malbers (10–30 min), its own URP
setup, and would break on any Synty/Malbers editor script the trimmed project cannot
compile; the manifest line is one string and is undone on exit. Both packages register
a ScriptedImporter for `.glb`; Unity rejects both for the package's own test file and
logs it — harmless, nothing in LOT is a glTF.

Every mode: the IDE integration regenerates LOT's solution when it sees the staged
asmdef (a git-ignored `Granadad.LotPipeline.Editor.csproj` plus a line in the tracked
`LordOfTrojia-MVP.slnx`); the runner snapshots the solution files before the run and
puts them back, and deletes the csproj. One thing it does not undo: URP's material
updater re-saves any material it loads that lacks a newer property (Synty's
`Generic_Glass.mat` gained `_SrcBlendAlpha`/`_DstBlendAlpha` on 2026-09-11 — the
owner's own editor session does the same; `git checkout -- <file>` in LOT if it matters).

Outputs are licensed derivatives (Synty meshes/atlases, Malbers clip data inside
the rigs): `content/art/lot-3d/` is gitignored here and in fom-3d; only these
tools, the job files and the `docs/asset-manifest-lot.md` section are tracked.
Coordinates: glTF is right-handed Y-up, both exporters mirror X, 1 unit = 1 m =
1 tile; bone names survive (`Root`, `Hips`, `Spine_01`…, `Hand_R`) for sockets.

