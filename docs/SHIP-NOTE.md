# Ship note — read this first

**The three moves landed this time.** The retry demanded proof of life and got
it: three lanes, twelve commits, one merge conflict, one one-line compile fix,
and every layout claim in the builders' reports re-verified here **by
measurement off fresh captures**, not by reading the reports back. The panel
MEASURE exists and every creation/casebook step is a card seated 45/55 on both
axes; the tiled Menu is on the terminal grammar at every size; every prompt
names the device that is holding it and re-words **live, mid-frame, no menu
visit** — photographed switching both directions in one process.

Gate: **green, both halves, twice over**, digest `6165af5d…`.
World hash: **`0x2646C1AAA2BA38DF`** — regenerated, rebaked, twin-gated, unmoved.
Demo: **run twice, watched, exit 0 both times**, 5780 frames, body at
(150,63,z19) both runs. Six committed demo frames legitimately moved (the
measure lane's creation and casebook beats) and were re-blessed; **the street,
world, night and end-card frames did not move a byte.**

## Run the demo

```
.\dist\granadad.exe --demo
```

Two minutes, plays itself, ends on a card and closes its own window. ESC stops
it early. `dist\` holds the gate-certified binaries — byte-identical, all
four, to the set a fresh isolated checkout goes green with (re-proven this
phase in `C:\repositories\granadad-ship-9d4fa02`).

Play it normally with `.\dist\granadad.exe`. Keyboard fastest path unchanged:
**DOWN DOWN ENTER**, ENTER on NAME, type, ENTER, **UP** (wraps to BEGIN),
ENTER, twelve ENTERs, UP, ENTER. On a pad the screen now tells you itself —
`B LEAVE / D-PAD MOVE / A OPEN`, `A - THAT IS WHAT HAPPENED` — because the
feet finally read the hand: **A** on NAME raises the OSK, **START** commits,
twelve **A**s, BEGIN. In the world: **M**/**SELECT** map, **TAB**/**D-pad Up**
casebook, **E**/**A** talk, **ESC**/**START** pause.

> Still true: the creation window does not answer F12 (photograph creation
> through `--creation=STEP` or `--padcreation`'s `shot:` beats), and `--demo`
> swallows everything but ESC.

---

## The gate, in full

Run in a **fresh isolated worktree** at `C:\repositories\granadad-ship-9d4fa02`
with the four gitignored audio directories real-copied (374 files).

| half | where | result |
|---|---|---|
| `docker compose run --rm --build build` | fresh worktree | **exit 0** |
| `scripts\verify-windows.ps1` | fresh worktree | **=== PASS ===**, exit 0 |
| `scripts\verify-windows.ps1` | main repo, re-verified | **=== PASS ===**, exit 0 |

```
revision:       9d4fa02 (17 commits past f4f8e19: 12 lane + 3 merges + 1 fix + 1 frames)
native/ digest: 6165af5d0ef8ebff5fcdc071913aa37d731e95561804f6df7158a815f6160759
                (stamped and here, both trees)
ctest cases:    967 (floor 537; +25 from the three lanes)
native/ files:  271
```

Comparators byte-for-byte identical linux/gcc vs mingw/windows in BOTH trees:
decoded world state 3884 bytes sha256 `97850DCB…C8B2179`; world hash + sim run
1791 bytes sha256 `924F6EA6…B7B8468B`. The four `dist\` binaries
(`granadad.exe`, `-tests`, `-twin-gate`, `-content-tests`) hashed main repo vs
fresh worktree: **byte-identical, all four.**

`test_demo` explicitly: **6 cases, 209 assertions, all passed, exit 0.**

## The world hash did not move — regenerated and rebaked, not asserted

```
1. regenerate   python tools\scripts\gen_docks_surface.py
                docks_surface.tmx sha256 CCEDA566…237B4D1C before AND after, tree clean
2. rebake       gradlew :tools:run --args="import-map … --raws content/raws"
                17,954 bytes, sha256 E47DA3AE…E474C2AC
                vs content/maps/baked/docks_surface.trojsav: IDENTICAL
3. twin-gate    dist\granadad-twin-gate.exe --population --population-hour 16 --ticks 7200
                run twice: 0x2646C1AAA2BA38DF both runs, report 18,772 bytes
                IDENTICAL, the two console outputs byte-identical to each other
4. no re-bless  golden_java_vectors.hpp, fixtures.hpp, test_world_reader.cpp,
                docks_surface.tmx, docks_surface.trojsav, BASELINE-WORLD-HASH.md
                — all byte-identical to the program's first commit (d32942e)
```

`git diff --name-only f4f8e19..HEAD -- content/` is **empty**. Three
render-only lanes; the sim never had a way to move.

## The demo

```
run 1   5780 frame(s), body ended at (150,63,z19)   exit 0
run 2   5780 frame(s), body ended at (150,63,z19)   exit 0
```

`--demo-capture`: 22 frames. **Exactly six moved** — case-book,
case-book-harls, creation-name, creation-sheet, creation-quiz,
creation-quiz-answered — the measure lane's beats, which SHOULD move, and did,
onto the new cards. **All sixteen street/world/night/end frames byte-identical**
— the proof the prompt sweep relabels nothing under keyboard drive. The six
are re-blessed in `docs/frames/demo/`. `creation-origin.png` dropped again
(thirteenth run, thirteenth drop — the known shutter race); the committed copy
is the byte-exact `--creation=origin` substitute, now showing the measured
card.

## Driving it, this phase

* **Pad, full run, one process** (`--padcreation` + `--padscript`, pristine
  bindings): door → A on NAME → `DAD` off the 10x3 grid → START → BEGIN →
  twelve As → BEGIN → the ward → casebook → pause → QUIT armed → cancelled →
  ward map → **quit through the pad's own armed-QUIT confirm**. 521 frames,
  exit 0.
* **The live switch, one process, no menu visit** — the frame the last two
  notes asked for (`docs/frames/ship4/sw-*.png`): keyboard finishes creation,
  the street serf reads `E - TALK`; **one D-pad press** → the crosshair reads
  `A - TALK`; **one arrow key** → `E - TALK` again. 2856 frames, exit 0.
* A pre-press bonus determinism point: the windowed pad run's door frame,
  taken before any pad press, is **byte-identical** to the headless
  `--creation=origin` capture.

## The measured state at 640x360 — verified against the builders' arithmetic

Threshold calibrated to reproduce the last note's 71.9% exactly (rows/cols
with no pixel above 16).

| surface | before (ship3) | now (measured fresh) | builder claim |
|---|---|---|---|
| THE DOOR | 639px full-width band, 63.9% dark rows, ~0 dark cols, 3.90:1 | card lit x[114..497], y[87..250]; dark cols **0→288 (45.0%)**; **2.34:1**; dark rows 63.9% (unchanged — rows are the height rule's axis) | 385×168 @ (114,86) — **CONFIRMED** (lit bbox 384 wide; the border's outer column sits under threshold) |
| THE NAME (OSK) | **71.9% dark rows**, 114 dark cols, 4.96:1 letterbox | **still 71.9% dark rows — the number was NOT beaten, exactly as the measure lane predicted**; the win is the other axis: dark cols **114→320 (50.0%)**, lit band a **2.98:1 card** at x[114..497], y[103..231] | 385×133 @ (114,102), 2.9:1 — **CONFIRMED** |
| casebook | 639px, 56.1% dark rows | card x[90..528] (439), y[71..269]; dark cols ~0→**246 (38.4%)**; 2.21:1; dark rows 56.1% (unchanged, same reason) | 440×203 @ (90,70) — **CONFIRMED** |
| quiz | 640-wide | 424 wide @ x=96, 2.29:1 | ~425 @ x=96 — **CONFIRMED, dead on** |
| sheet | 640-wide | 509 lit width x[58..566] (kb, typing); the pad sheet 394 | builder's ~415 estimate **wrong** (the two-column master out-votes it) — the *rule* held; the integrator's ~575 was also off at the calibrated threshold |
| tiled Menu | hairline rectangles, ten rows + `0 MORE (1/2)` in a 27-row pane | one `+~-~-`/`◆` frame, digits on exactly rows 1–9, the full list, stipple in the spare rows; at 320x180 the list paginates with a MORE foot instead of clipping | **CONFIRMED** at both sizes, byte-identical recapture |
| regressions | — | kb-wardmap, kb-keys, origin-320, sheet-1920, menu-320: fresh captures **byte-identical to the committed parallel2 set**, all 13 shots | clamp/never-calls-measure claims **hold** (the integrator's 1px-left find at 1920 stands noted) |

**Prompts, every claim photographed on pristine bindings**: pause header
`A SELECTS  START RESUMES`; armed `A QUITS  B CANCELS` + `5 QUIT -- SURE? A`;
casebook `PICK A LEAD. A SHOWS YOU WHERE.`; ward map band `D-PAD NEXT PLACE /
LB RB OVERVIEW / RT LT ZOOM / SELECT CLOSE`; door/sheet/quiz feet `B BACK /
D-PAD MOVE / A …` with the digit row gone; street `A - TALK` / `A - LOOK`.
And the fallback is honest: under this repo's own cfg (`interact E MOUSE2`,
no pad half) the crosshair keeps saying `E` on a pad — the other-hand rule,
by design, seen live.

Frames: `docs/frames/ship4/` (this phase's pad set + the switch triptych),
`docs/frames/parallel2/` (the integrator's 21, all re-reproduced
byte-identically here), `docs/frames/demo/` (re-blessed).

---

## The verdict: closer by a real step — and still not near ready

The standing verdict was "not near ready — moved a sliver, prose only." This
program moved the thing itself, and I measured it rather than took anyone's
word: **every surface a stranger meets in the first sixty seconds is now a
measured card on one grammar, seated 45/55 on both axes, with prompts that
follow the hand holding the machine.** Door, name, quiz, sheet, casebook,
pause, map, Menu — one register, no letterbox, no hairline rectangles, no
keyboard verbs under a pad player's thumbs. The Menu was the worst screen in
the game for three notes running; it now looks like it belongs to the same
program as the ward map.

**A stranger's first sixty seconds, today:** a 2.3:1 DOOR card floating on
ground-stipple; on a pad, feet that say `B LEAVE / D-PAD MOVE / A OPEN`; THE
NAME as a 2.98:1 card whose grid answers the D-pad; the casebook card with its
waiting line; the excellent ward and ward map; TAB — and the Menu holds the
register. **The worst screen in the game is now the sheet** — still the widest
(509px), still the densest, and a typed name past five glyphs visibly wiggles
its frame in 2-cell steps while you type.

But the bar is "clean and near ready for early access," and honesty about the
distance: **not near ready.** For the first time the reason is not the
screens. It is: **one authored case, twelve leads, five letters** — an evening
of content in a sandbox built for a season; seven pages that still swallow
mouse clicks; a demo that never dollies; and the new pad seams below. The
presentation debt is nearly paid. The content debt is untouched and it is the
whole remaining distance.

## Seams found by driving it (new this phase)

1. **The B that closes the casebook also toggles crouch** — the street after
   closing the book carries a `CROUCHED` banner nobody asked for. One press,
   two routes.
2. **A pad player's first world screen is keyboard-worded**: the auto-opened
   new-game casebook says `ENTER SHOWS YOU WHERE` until their first world
   press, because creation's device note dies with `CreationFlow` instead of
   seeding the Session it births.
3. The casebook detail's `ENTER - SHOW ME WHERE` / foot `ENTER GO TO IT`
   stay keyboard-worded on a pad — the known measure-lane literals, one-line
   fixes each via `promptMoveKeys`-style state fields.
4. The sheet's NAME-row width wiggle while typing (measure lane's own flag;
   fix is padding the value to `kMaxNameLength` in creation.cpp).

## The three things to do next

1. **The pointer pass: make the seven silent pages answer the mouse.** The
   tiled Menu, pause and its pages, conversation, wait, lockpick, and the
   casebook's tab row. The lanes left it cheap on purpose: `menuTileLayout` +
   `optionListAt` for the tiles, a `casebookTabAtPixel` sibling beside
   `casebookLeadAtPixel`, all on the pattern `session_pointer()`'s map branch
   already proves. `main.cpp:1881`'s catch-all return is where the tab hit
   slots in.
2. **Close the pad seams in one sweep** — the B-close crouch fall-through,
   the device note carried from creation into its Session, and the casebook's
   three remaining keyboard literals moved onto state fields the way
   `closeKey` already flows. All small, all found by playing, all on the pad
   player's first five minutes.
3. **A second authored case.** The screens no longer excuse the sandbox.
   Twelve leads and five letters is the whole game a stranger can drain in an
   evening, and no measure rule fixes that. This is the verdict's blocker and
   it should be the next program's headline, not its leftovers.

## Still open, unchanged

`--demo-capture` drops creation-origin (13/13); the input router lives in
`main.cpp`'s anonymous namespace, unlinkable by any suite; no camera motion in
the demo; the 1920 sheet sits 1px left of its old centring (invisible,
integrator-found, noted so nobody hunts it as drift).
