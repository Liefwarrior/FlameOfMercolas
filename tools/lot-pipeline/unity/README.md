# LOT Unity render tool — 3D rigs to 2D sprites

Turns Lord of Trojia's 3D assets (Synty humanoids, Malbers humanoid clips, Synty
weapons) into alpha PNG frames Granadad can put on screen: a first-person
**viewmodel** (arms + weapon per combat state) and head-and-shoulders
**portraits** (per character prefab, several views).

Nothing here is copied into LOT permanently and nothing rendered is committed:
every output is a derivative of purchased asset-store content and stays under
`content/art/lot/` (gitignored). Only this tool, its job files and `MANIFEST.md`
go to git.

## Files

| file | what |
|---|---|
| `LotSpriteRenderer.cs` | Unity Editor script. `RenderFromCommandLine` (batch) or `Tools/Granadad/*` menu items. |
| `render-lot.ps1` | Runner. Copies the .cs into `<LOT>/Assets/Editor/LotPipeline/`, runs Unity `-batchmode`, removes the .cs again. |
| `jobs/viewmodel-fists.json` | Bare-hand viewmodel: idle / charge / swing×3 / hard_swing×4 / block / cast / hit. |
| `jobs/viewmodel-sword.json` | Same states with `SM_Wep_Sword_01` in the right hand and the one-hand-sword clips. |
| `jobs/portraits-synty-heroes.json` | First 12 `Chr_FantasyHero_Preset_*` heads, four views each, 128×160. |

## Prerequisites (this machine, verified 2026-09-10)

- Unity **6000.3.6f1** at `C:\Program Files\Unity\Hub\Editor\6000.3.6f1\Editor\Unity.exe`
  (the exact version in `LOT/ProjectSettings/ProjectVersion.txt`; 6000.3.19f1 is also installed).
- LOT at `C:\repositories\LordOfTrojia-MVP` with its `Library/` already imported
  (it is — a cold import would add ~10–20 min to the first run).
- The LOT project must be **closed** in the editor for a batch run (Unity holds
  `Temp/UnityLockfile`). The runner refuses to start otherwise.
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
Expect 1–3 min per job on a warm Library; the Unity log is written beside the output.

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

## Downstream (Granadad side, not this tool)

- `content/art/lot/` is gitignored; the pipeline stages there. Quantise/scale for
  the terminal register with ffmpeg if wanted:
  `ffmpeg -i swing_1.png -vf "scale=160:120:flags=neighbor" swing_1@160.png`
- Pack `frames.json` + PNGs into a sheet + index in the `sprite-index.json` schema
  and hang it on `WorldRenderer::drawSprite` / `hud.cpp` (screen-space plate, not
  world-space) — that is the viewmodel feature, a separate lane.
