# Controls. Nine and the sticks

The controls lane on `lane/controls`. Every frame here is the shipped `dist\granadad.exe` (the docker gate's mingw build, gate green, Windows half PASS) playing the real verbs. `scripts\shoot-controls.ps1` shoots the whole set and keeps each run's summary line beside its frame in `shoot-controls.log`. Nothing in here is a mock.

    powershell -ExecutionPolicy Bypass -File .\scripts\shoot-controls.ps1

All frames are 960x540, `--scale=1`. The headless ones are `--smoke=0 --hold` captures. The pad ones open a real window with a virtual SDL gamepad on it and play a beat script through the game's own event pump (`--padscript`), which is the pad's actual code path, no controller plugged in. One harness fix landed on the way. SDL reads a virtual trigger at raw 0 as half pulled, so the virtual pad attached with both triggers already "down" and a trigger beat never released. Triggers rest at the raw minimum now, which is what a real trigger does (`kTriggerRest` in `main.cpp`). Under the old scheme nobody noticed because RT was a one-shot cast. Under this one RT is a held swing, and the first capture showed it.

## The count

Before. 13 core verbs, 42 keyboard inputs with a meaning, every pad button spent and five of them changing meaning per page.

After. 9 world verbs plus M on the keyboard. WASD and the mouse, then MOUSE1 SWING, MOUSE2 GUARD, C CAST, E USE, LCTRL SNEAK, SPACE JUMP, LSHIFT RUN, J NOTES, ESC PAUSE, and M for the map. On the pad, RT swings, LT guards, RB casts, A uses, B sneaks, Y jumps, the stick runs, D-pad up is NOTES, START pauses. The map is a page of NOTES on the pad. SELECT waits. D-pad left and right step the quick bar. X, R3 and D-pad down do nothing (an unknown press wakes the tutor bands).

What got cut. The quick wheel (Q hold, R3), the page pair as bindable verbs, F1, F2, F3, the walk toggle, the map's pad button, Attack on X and Cast on RT. Where each went is in `docs\design\CONTROLS-SPEC.md` section 4.

## The frames, against the design

The design is the NINE AND THE STICKS study and roadmap sections 3.1 and 3.2, decisions D1 and D2. Nothing in D1 or D2 was struck, so all of it is built.

| Design says | Frame | Command line | What is on it |
|---|---|---|---|
| D1. Nine verbs, both devices, on the controls page | `keys-page-kb-960x540.png` | `--pause=controls` | SWING MOUSE1, GUARD MOUSE2, CAST C, USE E, SNEAK LCTRL, JUMP SPACE, RUN LSHIFT, NOTES J, then PAUSE, MAP, WAIT, the bar and the shutter behind `+12`. The foot reads `TAB` for the OPTIONS sibling, no F-keys anywhere |
| D1. The same page with a pad in hand | `keys-page-pad-960x540.png` | `--padscript=...start,down,down,a,shot...` | The KEY column flips to the pad's half. SWING PAD RT, GUARD PAD LT, CAST PAD RB, USE PAD A, SNEAK PAD B, JUMP PAD Y, NOTES PAD UP. The foot reads `LT RT OPTIONS` |
| D1. The settings page, both devices | `options-page-kb-960x540.png`, `options-page-pad-960x540.png` | `--pause=settings`, `--padscript=...start,down,down,down,a,shot...` | Sliders first, every verb under them, the live hand's key beside each. A rebind lands in the slot of the key's own device, so a pad button never eats your mouse key |
| The prompt walk. A door | `prompt-door-960x540.png` | `--face=The Gilded Gull` | `THE GILDED GULL` / `E - LOOK` on the reticle |
| The prompt walk. A body, with steel out and the Watch reacting | `prompt-body-steel-960x540.png` | `--watch-halt=halt --settle-steps=20` | `SELLA BRINEWALL PATRON` / `E - TALK`, the `STEEL UP` row, and Cull's halt line. Eli's "the watch react to the violence", on one frame |
| The prompt walk. A body, crouched | `prompt-pickpocket-960x540.png` | `--burgle=taproom --settle-steps=20` | `KLED TARBECK BOUNCER` / `E - PICKPOCKET`, the same key resolving by stance |
| The prompt walk. A case object, a lock | `prompt-box-960x540.png` | `--burgle=lock --settle-steps=0` | `THE STRONGBOX ROOM 4 LOCKED` / `E - PICK LOCK` and the lock's own band |
| The prompt walk. A downed man, fists still up | `prompt-take-him-up-960x540.png` | `--case=down --settle-steps=20` | `FINCH DOWN, AND COMING WITH YOU` / `E - TAKE HIM UP`, the `FISTS UP` row after a brawl won bare-handed |
| The prompt walk. The counter (a conversation) | `talk-counter-960x540.png` | `--street=hand --settle-steps=20` | The numbered topic rows. Digits pick, ENTER confirms, ESC walks away |
| The prompt walk on a pad | `street-pad-960x540.png` | `--padscript=...b,shot...` | `PETRA BARNACRE SERF` / `A - TALK`. The same reticle, the pad's key. The opening band reads `D-PAD UP NOTES  A USE  RT SWING` |
| D2. One press from hands down raises the fists and swings | `fists-up-punch-960x540.png` | `--punch --settle-steps=20` | `FISTS UP` on the row, the blow landed, Tarn's own line back ("YOU SWUNG IN VENN'S HOUSE. NOW THERE ARE TWO OF US.") |
| D2. The pad's primary hand does the same | `fists-up-pad-960x540.png` | `--padscript=...rt,wait:900,shot...` | One RT pull on the street at 16:00. `FISTS UP` rises and the swing resolves on the release. It says `NOBODY IN REACH.` with a serf on the reticle, because a street body still cannot be hit (see the gaps). The reticle keeps offering `A - TALK`, which is the walk order (a person in reach beats lowering) |
| D2. USE with nothing in reach is LOWER HANDS, and it works | `lower-hands-pad-960x540.png`, `hands-down-pad-960x540.png` | `--time=3 --padscript=...rt,wait:900,shot,a,wait:700,shot...` | The small hours, nobody about. RT raises the fists (the swing whiffs, `NOBODY IN REACH.`) and the reticle reads `A - LOWER HANDS`. A takes it. The `FISTS UP` row is gone and the reticle is back to `A - LOOK`. The press itself says nothing (a stale "NOBODY HERE SELLS WIRE." used to print on every USE at nothing, and does not now) |
| D2. Guard | `guard-up-block-960x540.png` | `--block --settle-steps=20` | `GUARD UP` under `FISTS UP`. The guard raised the hands and never lowers them |
| D1. Cast on its own button | `cast-row-960x540.png` | `--flame --cast --settle-steps=20` | `CAST CLEAR THE HEAD` in the top right, `CLEAR THE HEAD -- HELD.` |
| D1. NOTES is one screen | `notes-page-960x540.png` | `--character` | The four tiles. Sheet, chart, letters, casebook |
| D1. NOTES on the pad, then the ring | `notes-pad-960x540.png`, `ward-map-pad-960x540.png`, `grimoire-pad-960x540.png` | `--padscript=...up,shot,rb,shot,rb,shot...` | D-pad up opens the casebook page (the foot says `LB RB NOTES`). RB turns the page to the ward map. RB again to the grimoire. That is the whole route a pad needs to both |
| D1. The ward map, keyboard | `ward-map-kb-960x540.png` | `--map-overlay` | The four-slot foot, unchanged. TAB views, `+ -` zoom, M close |
| D1. The ward map, pad | `ward-map-pad-960x540.png` | (the ring run above) | `LT RT OVERVIEW`, `RS ZOOM`, `LB RB NOTES`, `B CLOSE`. Views on the triggers, zoom on the right stick, the bumpers page on |
| D1. The grimoire is a page of NOTES | `grimoire-page-960x540.png` | `--flame --grimoire` | The list, ready to bind a slot on LEFT/RIGHT |
| D1. The casebook page's two views on the sub-tab keys | `casebook-page-kb-960x540.png`, `casebook-page-pad-960x540.png` | `--case=gull`, `--padscript=...rt,shot...` | The foot's `LEFT RIGHT CASE` was a lie before (the press was reserved and inert). It steps the view now, and so does RT on the pad. The pad frame is one RT pull on the opening page, landing on THE CASE, with `LB RB NOTES` on the foot |
| D1. The bar steps on the D-pad | `quick-bar-pad-960x540.png` | `--padscript=...right,right,shot...` | Two D-pad rights, `SLOT 3` picked, the strip up, the toast `D-PAD LEFT D-PAD RIGHT - STEP` |
| D1. The bar on the keyboard, the toast teaches the wheel | `quick-bar-kb-960x540.png` | `--flame --quickbar --settle-steps=12` | `SLOT 3 - CLEAR THE HEAD.`, the strip, `WHEEL - STEP` |
| D1. SELECT is WAIT | `wait-pad-960x540.png` | `--padscript=...back,shot...` | The hour page, straight off the Select button. `B` backs out |

## Every drive still works

Every scripted line calls Session verbs, not keys, so the scheme change cannot break them. Re-verified off this exe, every one exit 0, summary line in `shoot-controls.log` where it was shot here. `--punch`, `--charge`, `--block`, `--cast`, `--watch-halt`, `--nemesis`, `--case`, `--burgle`, `--street`, `--face`, `--character`, `--grimoire`, `--map-overlay`, `--quickbar`, `--pause`, `--padscript`. The full suite (`test_scripted_lines` and the rest, 94 suites, 1249 cases) runs in the docker gate and passed.

## Honest gaps

- The Gull fight frames (`fists-up-punch`, `guard-up-block`, `prompt-body-steel`) are point blank. The 3D build puts the camera a tile from the man you hit, so the plaster wall fills the frame. The HUD rows are the proof there. That is the 3D lane's framing, not the controls.
- A street body cannot be hit yet. RT on the Tarwalk with a serf on the reticle whiffs (`NOBODY IN REACH.`) because the swing's sightline still walks the Gull's roster only. That is the declared population move the roadmap calls THE STREET GETS SENSES (wave 2), not this lane. Inside the Gull, "hit whoever is in front of me" is true and photographed.
- The windowed pad captures render the world white on this host (a host display issue that has been seen before, not the code). The pages and the HUD are what those frames prove, and they render.
- A real controller was not plugged in. The virtual pad drives SDL's gamepad layer and everything downstream of it, which is where every defect of this kind has ever lived.
