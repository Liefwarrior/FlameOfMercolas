# #77 — the controls, photographed while being played

**None of these were taken with `--screenshot`.**

Every sprint before this one verified movement with the headless capture path:
run N scripted movement steps, write a PNG, exit, never open a window. That
proves the renderer and it proves the simulation, and it cannot prove one single
thing Eli complained about — input latency, camera smoothness, whether a
modifier feels like a modifier, whether walking into a wall climbs it. **That gap
is how the archaic feel shipped past a green gate, sprint after sprint.**

So these are captures of the game **being driven**. `scripts/drive-windowed.ps1`
opens the window, takes the foreground (and refuses to send anything at all if it
cannot — the first version of it opened the Start menu and photographed that),
presses real PS/2 scancodes through `SendInput`, moves the mouse with real
relative deltas, and then fires the game's **own** `F12` shutter — so the PNG is
the renderer's framebuffer and not a screen grab of a window with a title bar on
it.

## The runs

```
powershell -ExecutionPolicy Bypass -File .\scripts\drive-windowed.ps1 `
  -Script "wait:1200,shot:01-first-run,tap:f1,shot:02-the-keys,tap:f2,shot:03-options,tap:f2,w:900,shot:04-the-street"

powershell -ExecutionPolicy Bypass -File .\scripts\drive-windowed.ps1 `
  -ExeArgs "--width=960 --height=540 --scale=1 --time=10 --spawn=150,67,20 --yaw=0" `
  -Script "wait:1200,shot:05-under-the-wall,w:2000,shot:06-on-the-lead"

powershell -ExecutionPolicy Bypass -File .\scripts\drive-windowed.ps1 `
  -ExeArgs "--width=960 --height=540 --scale=1 --time=10 --spawn=150,66,21 --yaw=0" `
  -Script "wait:1200,shot:07-on-the-lead,tap:x,wait:400,shot:08-the-fall"

powershell -ExecutionPolicy Bypass -File .\scripts\drive-windowed.ps1 `
  -Script "wait:1200,tap:space,shot:09-mid-jump,ctrl:200,shot:10-crouched,ctrl:200,w+shift:1300,shot:11-sprinted"
```

| Frame | What it is |
|---|---|
| `01-first-run.png` | The opening page. The hint row now reads `TAB YOUR NOTES  F1 KEYS  F2 OPTIONS  Q LOOK AT IT`. |
| `02-the-keys.png` | `F1`. **Generated from the live binding table**, not a static array of strings — rebind anything and this page says so on the next frame. |
| `03-options.png` | `F2`. Sensitivity, invert-Y, view angle, pad deadzone, then every verb in the game with `ENTER` to rebind it. |
| `04-the-street.png` | 0.9 s of holding forward from the authored spawn. That is most of the way across the Tarwalk; the same key for the same time used to cover a third of it. |
| `05-under-the-wall.png` | Standing on the Gull's guest floor, facing the north wall. |
| `06-on-the-lead.png` | **Two seconds of holding forward later, and nothing else.** The alert says `UP AND OVER.` and the place label says `THE DOCKS - UPPER`: the body is on the Gilded Gull's roof and no climb key exists. |
| `07-on-the-lead.png` | On the lead, at the north parapet. |
| `08-the-fall.png` | `X` off it. Two storeys, 5.4 m, and the health bar is halved — the fall curve is `(v - vTolerated)^2` with `v = sqrt(2gh)`, not a table. |
| `09-mid-jump.png` | `SPACE`. Half a metre up, 0.63 s of hang, and it gets you onto nothing — which is the point. |
| `10-crouched.png` | A 200 ms **tap** on `CTRL` latches the crouch on. |
| `11-sprinted.png` | A second tap says `UPRIGHT`, then 1.3 s of `SHIFT`+forward puts the body inside the Gull. |

## Two bugs these frames caught, that a `--screenshot` capture could not have

- **The options page drew on top of the HUD.** Every other full-screen surface —
  a conversation, the casebook, the keys page — stands the HUD down; the new one
  was not on that list, so the compass strip, the case row, the health bar and
  the first-run hint all drew straight through the sliders. Nothing scripted
  opens this page, so no capture would ever have contained it.
- **A fall said how far it was and not what it cost.** `dropDown` said its line
  and *then* charged the landing, and it is `settleLanding` that appends the
  height and the injury — so the alert read `DOWN 2 LEVELS.` while the health bar
  quietly halved, with nothing on screen joining the two. It reads
  `DOWN 2 LEVELS. - 5M - 50 HURT` now.
