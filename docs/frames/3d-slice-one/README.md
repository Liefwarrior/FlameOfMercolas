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

## The live window (frames 26 and 27)

Frame 26 is the LIVE WINDOW as the ship lane photographed it, not the shutter:
`dist\granadad.exe --time=8` launched windowed on the host, five seconds in.
It is white -- BitBlt, PrintWindow and a DWM thumbnail all white, sandboxed or
not, moved, minimised and restored, at `--scale=1` and under `--2d`, while the
loop ran (4,837 frames in ~13 s) and the software `shot:` frames were correct.

Frame 27 is the same window twenty-five minutes later, off the same desktop, on
the gated exe of this tree: the Tarwalk at 08:00 past the character screen
(DEVIN, BEGIN, the casebook closed), captured with `Graphics.CopyFromScreen` of
the window's client rect -- mean luma 62.00, near-white fraction 0.0000 of
1280x720. The character screen, the casebook page and the street all reach the
screen; `PrintWindow(PW_RENDERFULLCONTENT)` agrees with the screen grab;
focused or unfocused, DPI-aware or not, launched from either shell, under
either exe name.

WHAT WAS WRONG WAS THE HOST, NOT THE PRESENT PATH. The path is what the
contract says: `BeginDrawing`/`ClearBackground`, the 3D pass, the overlay,
`rlDrawRenderBatchActive`, the readback, `EndDrawing` -- one window, GLFW's
(class `GLFW30`), SDL initialised for the gamepad and the audio device only.
Nothing in `native/` changed between the white captures and frame 27 except
the audio merge; the bytes that present correctly are the bytes that
photographed white. The white launches all fall between 05:56 and 06:08 on
this host; the System log holds one event in the hour around them --
`Kernel-Power 105, Power source change` at 06:11:24 (AC off) and 06:14:28 (AC
on), a display re-configuration on this NVIDIA RTX 5090 Laptop + Intel iGPU
machine (the panel is on the NVIDIA GPU; the iGPU has no output) -- and
every launch after it presents. A GL window that swaps but photographs white
is what the desktop's redirection surface looks like when the frames are
going straight to a hardware overlay plane or a stalled DWM path instead of
through the compositor: the frame may well have been ON THE PANEL the whole
time, where no capture of the lane's could see it. So: no code change for
this; the proof is frame 27 and the numbers above, and the thing to do if a
white window is ever seen again is to LOOK AT THE SCREEN before reading any
capture.

## The gate behind these frames

Docker gate on this tree (the audio pass merged: wip tip 9c8dc7a, the LOT
music director, real footsteps and combat impacts under the 3D session): GATE
EXIT 0, hostcheck ctest 92/92 (1,174 cases, floor 537), mingw cross published
`dist\`, native/ digest
`18a9beabc6d8047885494eec05230af14e9acfb61dc9d04b3d13265cb5df1a50`
(309 files). `scripts\verify-windows.ps1` on that dist: PASS -- content
suite, sim suite, twin gate, the shutter through a real window, the
content-fingerprint and world-hash reports byte-identical linux/gcc vs
mingw/windows and unchanged (`97850DCB...`, `924F6EA6...`). Baselines on the
gated exe: `--tavern --ticks 900` `0x86E05F527E54E795`, `--population
--population-hour 16 --ticks 7200` `0x2646C1AAA2BA38DF`; the scene hash
`0xD44BE903143928AE` twice as separate processes with byte-identical PNGs;
`--audio-selftest` starts the music director with all 111 LOT files found.
Nothing under `native/src/sim`, `native/include/granadad/sim` or `content/`
differs from the wip tip 9c8dc7a.
