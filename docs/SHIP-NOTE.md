# Ship note — read this first

**Nothing is broken — but the build phase shipped nothing.** This program
dispatched three parallel builders at the last note's three next moves
(the panel measure rule, the Menu's grammar, device-aware prompts) and
**all three produced zero commits**. The integrator confirmed it: no ref, no
dangling commit, nothing to merge. The only pass that landed is the **voice
pass** — twenty functional strings tightened to city cadence, not one raw
touched. So the three next moves below are **the same three, unimplemented**,
and the first-contact numbers in the verdict are re-measured, not moved.

Gate: **green, both halves, twice over**, revision `fdbd177`.
World hash: **`0x2646C1AAA2BA38DF`** — regenerated, rebaked, twin-gated, unmoved.
Demo: **run twice, watched, exit 0 both times**, 5780 frames, body at
(150,63,z19) both runs. Capture is deterministic and **not one committed
frame moved**.

## Run the demo

```
.\dist\granadad.exe --demo
```

Two minutes, plays itself, ends on a card and closes its own window. ESC stops
it early. `dist\` already holds the gate-certified binaries — nothing to build,
and this phase proved those bytes are the exact bytes a fresh isolated
checkout goes green with.

Want the stills too? `.\dist\granadad.exe --demo-capture=DIR`
Want one section? `.\dist\granadad.exe --demo=case` (`quay saltgate case map night end`)

Play it normally with `.\dist\granadad.exe`. Character screen, fastest path:
**DOWN DOWN ENTER** (WALK YOUR OWN PATH) → **ENTER** on NAME, type a name,
**ENTER**, **UP** (the cursor wraps straight to BEGIN — you do not need the
25 DOWNs the old note counted), **ENTER**, twelve questions each answered
with **ENTER**, then — the cursor is back on NAME — **UP, ENTER** on BEGIN
again. In the world: **M** map, **TAB** casebook, **E** talk, **F1** keys,
**ESC** pause.

**On a pad**: **A** confirms, **A** on NAME raises the on-screen keyboard,
**START** commits the name, **UP** wraps to BEGIN, **A**, twelve **A**s for
the past, **UP, A** on BEGIN. In the world: **D-pad Up** casebook, **SELECT**
map, **START** pause (first press dismisses whatever is open), **B** crouch.

> Note for whoever drives this next: **the creation window does not answer
> F12**, so `drive-windowed.ps1` shot beats only work once the world window
> exists — in creation, photograph through `--creation=STEP` or
> `--padcreation`'s own `shot:` beats. And during `--demo` the game swallows
> everything but ESC. Both cost this phase several blind runs to learn.

---

## The gate, in full

Run in a **fresh isolated worktree** at `C:\repositories\granadad-ship-fdbd177`
with the four audio directories the sound bank names (`Foley Sounds` 87,
`Impact Sounds` 132, `Interface Sounds` 102, `RPG Audio` 53 — 374 files)
**real-copied** — they are gitignored, a checkout does not carry them.

| half | where | result |
|---|---|---|
| `docker compose run --rm --build build` | fresh worktree | **exit 0** |
| `scripts\verify-windows.ps1` | fresh worktree | **=== PASS ===**, exit 0 |
| `scripts\verify-windows.ps1` | main repo, re-verified | **=== PASS ===**, exit 0 |

```
gate executed:  2026-08-31T21:34:36Z UTC
revision:       fdbd177 (stamp says "docker"; the digest is the identity)
native/ digest: 6dac953634118c1ac8e51d86b3081580f6b84d18fb39b425e04937cb72161799
ctest cases:    942 (floor 537)
native/ files:  270
```

The two comparators, byte-for-byte identical linux/gcc vs mingw/windows, in
BOTH trees:

```
decoded world state   3884 bytes  sha256 97850DCB…C8B2179   IDENTICAL
world hash + sim run  1791 bytes  sha256 924F6EA6…B7B8468B  IDENTICAL
gate stamp            matches the main repo's own native/ tree
```

**`dist\` in the main repo carries the certified binaries** — `granadad.exe`,
`granadad-tests.exe`, `granadad-twin-gate.exe`, `granadad-content-tests.exe`
each hashed against the fresh worktree's published set: **byte-identical, all
four.** The thing the owner runs is the thing that went green in isolation.

`test_demo` explicitly: `dist\granadad-tests.exe -sf="*test_demo*"` →
**6 cases, 209 assertions, all passed, exit 0.**

## The world hash did not move — regenerated and rebaked, not asserted

```
1. regenerate   python tools\scripts\gen_docks_surface.py
                content/maps/src/docks_surface.tmx
                sha256 CCEDA566…237B4D1C BEFORE and AFTER — git status clean

2. rebake       gradlew :tools:run --args="import-map … --raws content/raws"
                17,954 bytes, sha256 E47DA3AE…E474C2AC
                vs content/maps/baked/docks_surface.trojsav: IDENTICAL

3. twin-gate    dist\granadad-twin-gate.exe --population --population-hour 16 --ticks 7200
                run twice: 0x2646C1AAA2BA38DF all four hashes,
                report 18,772 bytes IDENTICAL, both console outputs
                byte-identical to each other

4. no re-bless  golden_java_vectors.hpp, content/tests/fixtures.hpp,
                test_world_reader.cpp, sim/docks.hpp, docks_surface.tmx,
                docks_surface.trojsav, BASELINE-WORLD-HASH.md
                — all seven byte-identical to the program's first commit (d32942e)
```

`git diff --name-only 107b646..HEAD -- content/` is **empty**. The voice pass
touched five render .cpp/.hpp, two test files, and docs frames. Nothing else.

## The demo

Two full windowed runs, watched, exit code checked:

```
run 1   5780 frame(s), body ended at (150,63,z19)   exit 0
run 2   5780 frame(s), body ended at (150,63,z19)   exit 0
```

One `--demo-capture` run afterwards: **22 frames, every one byte-identical to
the committed set in `docs/frames/demo/`** — which is the set the voice pass
re-committed, so the five copy-moved frames (case-book, case-book-harls,
creation-name, creation-sheet, gate-10) are already in it and **nothing moved
beyond them. The street, world, night and end-card frames did not move**,
which is also the proof the prompts builder's no-op touched nothing: there was
no relabeling to touch them with.

The 23rd, `creation-origin.png`, **dropped again** (twelfth capture run,
twelfth drop — the known shutter race). Re-taken through
`--creation=origin --width=640 --height=360 --scale=1`: sha256 `5650B135…`,
**byte-identical to the committed frame.**

## Driving every input, this phase

All three devices completed creation and reached the ward on the certified
binary. All three exited 0.

**Keyboard — real SendInput scancodes through `scripts\drive-windowed.ps1`.**
Door → CLAW typed → UP to BEGIN → twelve ENTERs → BEGIN → the ward, then the
casebook, the ward map, F1 and a street shot with a serf's `E - TALK` prompt
on it: `granadad: playing as CLAW (custom)`, 1225 frames, exit 0.

**Pad — a real `SDL_AttachVirtualJoystick`, creation AND world, one process.**
Door → A on NAME → `DAD` off the 10x3 key grid → START → UP, A on BEGIN →
twelve As → BEGIN → the ward → D-pad Up casebook → START START pause:
407 frames, exit 0.

**Mouse — real relative motion and real clicks.** Hovering the ward map at
(85,260) selects THE QUAYWARD COMPOUND — footprint rect lit, detail pane
`KIND DOOR / STANDS ON SALTGATE RISE / FOOTPRINT 64X19 TILES / BAND 20 /
FROM YOU SW 90 PACES / INSIDE NOW 39 PEOPLE`, status
`SELECTED (104,137) THE QUAYWARD COMPOUND`, foot `ENTER - FACE IT (SW)` —
**the same selection ship2 recorded, reproduced by measurement.** 1047
frames, exit 0.

### And what the driving photographed, because the builders shipped nothing

* **With the pad connected and driving, the pause header still reads
  `ENTER SELECTS  ESC RESUMES`,** the casebook's foot still reads
  `ENTER GO TO IT / TAB CLOSE`, the street still says `E - TALK`
  (`docs/frames/ship3/pad-pause-640.png`, `pad-casebook-640.png`). Worse than
  the last note knew: **creation's sheet and quiz feet are keyboard-worded
  too** — `ESC BACK / ENTER OPEN`, `1-9 PICK` — on the critical path of every
  pad player (`pad-sheet-begin-640.png`, `pad-quiz-640.png`). The one
  pad-aware surface is the OSK itself (`B BACK / PAD MOVE / A TAKE / START
  DONE`). The drawing site is a fixed string (`session.cpp:4161` and friends);
  no device lookup exists.
* **THE NAME re-measured off a fresh pad capture: 259 of 360 rows carry no
  lit pixel — 71.9%, the exact number the note said to beat.** Lit band rows
  103–231 (the 129px letterbox), 114 fully dark columns. Unbeaten because
  untouched.
* **THE DOOR: 63.9% black rows** (230/360, band 87–250) — the committed frame
  re-measured, and `--creation=origin` still reproduces it byte-exactly.
* **The tiled Menu, captured fresh at 640x360 AND 320x180**
  (`docs/frames/ship3/menu-character-320.png`): still four solid hairline
  rectangles, not the `+~-~-` grammar; the CHARACTER tile still draws ten
  rows and `0 MORE (1/2)` into a pane twice that deep. `menu_view.cpp` has a
  zero-byte diff this program.

### Builders' arithmetic, verified

**There was none to verify.** All three builder reports were null; no layout
claim, no commit, no branch. Every number above is this phase's own
measurement of the unchanged surfaces.

---

## The verdict: still NOT near ready for early access

The last note said "forward half a step." This program moved it **a sliver,
and only in the prose** — the geometry a stranger meets is the same to the
pixel, because the phase that was supposed to move it silently produced
nothing. I will not dress that up: the parallel dispatch failed and the
program's three stated goals are exactly as unimplemented as they were.

**A stranger's first sixty seconds, today:** a well-set, well-seated DOOR
that is still a 639px-wide letterbox with 63.9% of the frame dark; on a pad,
THE NAME at 71.9% black with keyboard verbs on the sheet around it; the
casebook pushed at them (with its good waiting line); the genuinely excellent
ward and ward map; then TAB — **and the tiled Menu is still the worst screen
in the game**, the one surface off the terminal grammar, half its CHARACTER
tile dark for a reason that is not emptiness.

**The prose, though, got there.** The owner's note was that the writing
should read like city folk wrote it, not an assistant. Sampled on the
surfaces a stranger actually meets: the sheet's NAME detail now says `THE
WARD WILL USE IT TO YOUR FACE FROM HERE ON`; the chart tile says `THE CHART
KNOWS WHAT THE CASEBOOK KNOWS, AND NOT A STREET MORE`; the pause line says
`THE DOCKS DO NOT WAIT ON YOU. SETTINGS KEEP THEMSELVES.` The over-explaining
is gone — no sentence left does two clauses' work where one lands. And the
raws were rightly left alone: Father Maell's "the difference between an old
man's nerves and a fact with a temperature" needed no assistant's help. The
functional copy now keeps that company instead of apologizing next to it.
That is real, it is on every panel, and it is the only thing in this program
a player can feel.

The unfixable one is still unfixable by layout or prose: **one authored case,
twelve leads, five letters.**

## What this program moved, in two commits

### The voice pass — `7d4366f` (copy + test pins), `fdbd177` (frames)

Twenty functional strings across `session.cpp`, `creation.cpp`,
`controls.cpp`, `demo.cpp`, `casebook_page.hpp` — empty states, help lines,
instructions — each cut to one clause doing the work. Zero raws changed
(`git diff 107b646..HEAD -- content/` is empty). Two test pins updated in the
same commit. Nine surfaces photographed at 640x360 in `docs/frames/voice/`;
five demo frames legitimately moved by copy and were re-committed; the other
17 stayed byte-identical.

## The three things to do next — unchanged, because none of them happened

1. **Give the composed panel a MEASURE, the way the placement pass gave it a
   seat.** One `panelSeatX()`/measure rule in `render/panel.hpp` sizing the
   composition to the widest row it actually draws plus the master/detail
   gutter, seated on the same 45/55 judgement, full-frame pages falling out
   untouched by construction. **Judge it off frames at 640x360, not
   arithmetic.** THE NAME is the acid test: **71.9% black is still the number
   to beat** — re-confirmed this phase off a fresh capture.
2. **Put `menu_view.cpp`'s four tiled panels on the terminal grammar, and let
   a tile's page size follow its pane.** One job, both in `drawMenuTiles`:
   `+~-~-` rules with `+`/`◆` junctions and alternating `|`/`!` edges, and
   `topicRowsFor`'s ten-row cap becoming a function of the pane's height. The
   vocabulary exists in `panel.hpp`/`panel.cpp` — use it, never hand-roll a
   second copy.
3. **Make the prompts name the device that is holding them** — and the
   inventory grew: not just `E - TALK`, `ENTER SELECTS ESC RESUMES`,
   `ENTER GO TO IT`, `TAB CLOSE` in the world, but creation's own sheet and
   quiz feet (`ESC BACK`, `ENTER OPEN`, `1-9 PICK`) with a pad in hand. The
   binding table knows both halves of every `Action` (`controls.cpp`'s
   `set(Action, Key, Key)`); print the half belonging to the device that last
   sent input. The OSK already behaves — copy its manners, not its code.

## Still open, and why

1. **`--demo-capture` drops `creation-origin.png`** — twelve runs, twelve
   drops. The demo plays the beat every time; the substitution through
   `--creation=origin --scale=1` is byte-exact. Untouched.
2. **Seven pages swallow clicks** — the tiled Menu, the pause menu and its
   pages, a conversation, the wait page, the lockpick, the casebook page's
   tab row. Pattern to copy: the map branch in `session_pointer()` —
   `mapPageLayout`/`mapPlaceAtPixel`, `creationPageHitTest`.
3. **The input router is still in `main.cpp`'s anonymous namespace** — no
   suite can link `route_menu_key`, `session_pointer`, `creation_input`,
   `creation_pointer`.
4. **No camera motion anywhere in the demo.** It cuts, walks and turns; it
   never dollies on a still.
5. **One authored case, twelve leads, five letters.**
6. **New, procedural:** the parallel builder dispatch can silently no-op —
   three null reports and zero commits looked, from the outside, exactly like
   three busy builders. Whatever re-runs it should demand a branch name and a
   commit SHA as proof of life before the integrate phase spins up.

## The frames behind every claim above

`docs/frames/ship3/`, all 640x360 (one 320x180), all off the certified binary:

```
pad-pause-640.png        pause via START, pad connected — ENTER SELECTS ESC RESUMES
pad-casebook-640.png     casebook via D-pad Up — ENTER GO TO IT / TAB CLOSE
pad-sheet-begin-640.png  the sheet, NAME DAD, BEGIN READY — keyboard-worded foot
pad-quiz-640.png         question 1 of 12 — ESC BACK / ENTER ANSWER / 1-9 PICK
pad-osk-640.png          the OSK fresh: 71.9% black rows re-measured, pad-worded
kb-casebook-640.png      the casebook a new game opens (keyboard run)
kb-wardmap-640.png       M — the ward map, full frame, still excellent
kb-keys-640.png          F1 — the keys page, voice line in place
kb-street-serf-640.png   a serf carrying E - TALK
mouse-map-hover-640.png  real hover selecting THE QUAYWARD COMPOUND
menu-character-320.png   the tiled Menu at 320x180 — same wrong grammar
```

Plus `docs/frames/voice/` (the voice pass's nine) and `docs/frames/demo/`
(all 23, every one re-proven byte-identical this phase).
