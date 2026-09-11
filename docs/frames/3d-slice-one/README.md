# 3D slice one -- the proof frames

Every frame here is `dist\granadad.exe` (the docker gate's mingw build, branch
`3d/build`) driven on the Windows host with a real GPU, through the shutter:
`--smoke=40 --screenshot=... <drive>`. The scripted drives are the game's own
verbs (the walk is real collision, the punch is the Attack key, the fights are
the room resolving them); nothing here is staged. 1280x720, the 640x360 render
upscaled 2x, the terminal HUD composited over the 3D world.

No licensed Synty/Malbers glTF was on disk (`content/art/lot-3d/` holds only
the Unity log: `No valid Unity Editor license found`), so every body, every
hand and every wall is the built-in placeholder: atlas-textured chunk meshes,
blocky rigs, box fists. That is the feel pass, not the fidelity pass.

## The walk (criterion 1)

| frame | drive | what it proves |
|---|---|---|
| 01 | `--time=8` | the Tarwalk at 08:00 from the spawn: floors, walls, the buildings, the sky, the crowd, a cat |
| 02 | `--time=12` | noon tint |
| 03 | `--time=18` | dusk tint |
| 04 | `--time=20` | night; scene `0xD44BE903143928AE` |
| 05 | `--threshold=piers --time=9` | the Long Piers: the wharf planks over the water, the far piers, the sky |
| 06 | `--threshold=piers --settle-steps=0` | the THRESHOLD PLATE ("THE LONG PIERS") on the frame at the crossing |
| 07 | `--threshold=gull --time=20` | inside the Gilded Gull: ceiling, walls, a patron at the door, the SEEN/LIT/QUIET readout |

## The people (criterion 2)

| frame | drive | what it proves |
|---|---|---|
| 01, 08, 09 | `--smoke=40/41/42 --time=8` | the same street one step apart: the bodies stand where the sim has them and move through it as the sim moves them |
| 07, 11, 20 | Gull frames | bodies inside the Gull where the sim has them (Tarn Wrenhale, the bouncer, patrons) |

Bodies: 208-301 per frame, 0 skinned, 0 rig files (placeholders). NOT
ANIMATED: the Walk / PunchLeft / PunchRight / Recover / Death clips are chosen
per body in the adapter (`actor_instances.cpp`) and `drawActor` plays them
only on a loaded glb rig; with none on disk the placeholder is a rigid mesh
(`drawInstance`) that translates and turns and does nothing else -- no
stride, no swing, no falling down. A downed man is a standing box.

## The hands (criterion 3)

| frame | drive | hands |
|---|---|---|
| 10 | `--punch --settle-steps=60` | `fists/idle up` -- the raised fists at rest, FISTS UP on the band |
| 11 | `--punch --settle-steps=6` | `fists/swing_light up` -- the tap, the right fist thrown at Tarn |
| 12 | `--charge=6 --settle-steps=0` | `fists/charging up` -- the hard swing's wind-up, mid-scrub |
| 13 | `--charge=15 --settle-steps=0` | `fists/charged_hard up` -- the hard tier held at the threshold |
| 14 | `--block --settle-steps=20` | `fists/block up` -- the guard, GUARD UP on the band |
| 15 | `--cast --flame=away --settle-steps=2` | `fists/cast down` -- the touch-cast gesture, CLEAR THE HEAD held |
| 16 | `--punch --settle-steps=30` | `fists/hit up` -- the flinch when Tarn hits back |
| 22 | `--watch-halt=halt` | `sword/idle down` -- the placeholder blade after the arrest |

`--charge=N` is new in this lane (press Attack, hold N steps, no release);
before it every scripted swing was a tap and the wind-up had no shutter.

## The HUD (criterion 4)

| frame | drive | what it proves |
|---|---|---|
| 17 | `--pause=controls` | the CONTROLS page over the (dimmed) 3D street |
| 18 | `--map-overlay` | the WARD MAP over it |
| 19 | `--character` | the tiled Menu (Character, Chart, Letters, Casebook) |
| every frame | | the reticle, the compass, the bottom band, the health/fatigue bars |

Byte-exactness is the in-process rlsw proof (test_viewmodel.cpp: the HUD
composite byte-identical again; the 18 HUD/page tests untouched). On the GPU
frame the HUD glyphs are identical to the `--2d` frame's; the world under a page
differs from the software raycaster's by 1 in most pixels and by more where the
two renderers disagree on geometry -- expected, the GPU frame is never hashed.

## The fight (criterion 5)

| frame | drive | what it proves |
|---|---|---|
| 11, 16 | `--punch` | the punch landing on Tarn Wrenhale, his brawl.join bark on the band, the flinch back |
| 21 | `--case=down` | "FINCH IS ON THE BOARDS. E TAKES HIM UP." -- a man down in the Gull at 02:00 (his body is out of the crosshair's frame) |
| 22 | `--watch-halt=halt` | steel out in Cull's sight: Sella Brinewall 24->13, the arrest, Cull's halt line |
| 20 | `--nemesis=talk` | the arc's end: Tarn x3, Foreman of the Ropewalk Gang, holds the Gullet, HOSTILE, the conversation over the 3D taproom |
| 23 | `--nemesis=death --settle-steps=30` | the DEATH CEREMONY held: "PUT DOWN IN THE GILDED GULL. BY TARN WRENHALE. BY THE CUDGEL." |
| 24 | `--nemesis=death --settle-steps=276` | the epitaph fading over the 3D quay revive |
| 25 | `--nemesis=death --settle-steps=282` | revived on the quay, TARN WRENHALE X2 on the band |

`--nemesis=death` is new in this lane: the line stops on the boards of the
rematch with the ceremony still playing (beats 1-3 are what it owes).

## Determinism (criterion 6)

`--smoke=40 --time=20` twice as separate processes: scene
`0xD44BE903143928AE` both times, the PNGs byte-identical, and identical to the
integrate lane's frame on the previous build. Population baseline on this
build's `granadad-twin-gate.exe --population --population-hour 16 --ticks
7200`: run A == run B == `0x2646C1AAA2BA38DF`, PASS.

## The gap that matters (frame 26)

Frame 26 is the LIVE WINDOW, not the shutter: `dist\granadad.exe --time=8`
launched windowed on the host, photographed five seconds in. It is white. The
loop runs (4,837 frames in ~13 s, the character screen and the world both
reached through a virtual-pad script, the software `shot:` frames correct), but
nothing the loop presents reaches the screen: BitBlt, PrintWindow and a DWM
thumbnail all show white, sandboxed or not, moved, minimised and restored, at
`--scale=1`, and under `--2d`. Host: NVIDIA RTX 5090 Laptop + Intel iGPU,
150 % DPI. The shutter's frames come from `LoadImageFromScreen` BEFORE the
swap, so they never proved the swap; this is the first look at it. Until it is
fixed the game cannot be played on this machine, whatever the frames say.
