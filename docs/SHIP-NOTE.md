# Ship note — read this first

**The placement pass (this program) is NOT ship-gated yet.** Two moves landed
against a green docker half (`100% tests passed, 0 tests failed out of 75`, run
per item) and the demo was re-captured twice, byte-identical; the Windows half
(`scripts\verify-windows.ps1`) has not been run since. Everything below the
"What the placement pass moved" section is the previous program's ship note and
its numbers still stand except where that section corrects them.

Gate: **green, both halves**, revision `581d8fe`.
World hash: **byte-identical** to where this program started.
Demo: **run twice, watched, exit 0 both times.** Capture is deterministic.

## Run the demo

```
.\dist\granadad.exe --demo
```

Three minutes, plays itself, ends on a card and closes its own window. ESC stops
it early. `dist\` already holds the gate-certified binary — nothing to build.

Want the stills too? `.\dist\granadad.exe --demo-capture=DIR`
Want one section? `.\dist\granadad.exe --demo=case` (`quay saltgate case map night end`)

Play it normally with `.\dist\granadad.exe`. On the character screen the
fastest way in is **2 (ANSWER FOR YOURSELF) → ENTER**, answer ten questions,
type a name, ENTER, then BEGIN. In the world: **M** map, **TAB** casebook,
**E** talk, **F1** keys.

**On a pad**, all of that now works with no keyboard at all: **A** confirms,
**A** on the NAME row raises an on-screen keyboard, **START** commits the name,
**A** on BEGIN starts the game. In the world: **D-pad Up** casebook,
**SELECT** map, **START** pause, **B** back, **A** interact, **LT/RT** block
and cast.

---

## The gate, in full

| half | result |
|---|---|
| `docker compose run --rm --build build` | **exit 0.** 169 objects, `100% tests passed, 0 tests failed out of 75` |
| `scripts\verify-windows.ps1` | **=== PASS ===**, exit 0 |

```
gate executed:  2026-08-29T20:03:54Z UTC
revision:       581d8fe
native/ digest: 1d1fa8082adedfe3787b83cce569e85e67bc274b03f8ab20236a026f90a7bb31
ctest cases:    935 (floor 537)
native/ files:  270
```

Cases and assertions actually executed under mingw/Windows:

| suite | cases | assertions |
|---|---|---|
| sim + render (`granadad-tests.exe`) | 854 | 1,317,460 |
| content (`granadad-content-tests.exe`) | 71 | 902,135 |
| **total** | **925** | **2,219,595** |

The two comparators, both byte-for-byte identical linux/gcc vs mingw/windows:

```
decoded world state   3884 bytes  sha256 97850DCB…C1B2179   IDENTICAL
world hash + sim run  1791 bytes  sha256 924F6EA6…8B7468B   IDENTICAL
gate stamp            the published green names THIS native/ tree
```

Run in a **fresh isolated worktree** at `C:\repositories\granadad-ship-581d8fe`,
with the four audio directories the sound bank actually names (`Foley Sounds`,
`Impact Sounds`, `Interface Sounds`, `RPG Audio`, 4.3 MB) **real-copied** — they
are gitignored, so a checkout does not carry them, and junctions do not survive
the copy the build makes.

## The world hash did not move

Two render-only sprints. The proof is a number, not a claim:

```
granadad-twin-gate --population --population-hour 16 --ticks 7200, 96 walkers
run A  0x2646C1AAA2BA38DF
run B  0x2646C1AAA2BA38DF     report text 18,772 bytes, IDENTICAL
```

That is the exact hash and the exact byte count `docs/BASELINE-WORLD-HASH.md`
records for District Phase D. **No golden was re-blessed** —
`native/tests/golden_java_vectors.hpp`, `native/content/tests/fixtures.hpp`,
`native/content/tests/test_world_reader.cpp`,
`native/include/granadad/sim/docks.hpp`, `content/maps/src/docks_surface.tmx`,
`content/maps/baked/docks_surface.trojsav` and `docs/BASELINE-WORLD-HASH.md`
are all byte-identical to the program's first commit. `git diff --name-only`
across both sprints touches only `docs/` and `native/`. No map, no
`content/art`.

## The demo

Two full windowed runs, watched start to finish:

```
run 1   5780 frame(s), body ended at (150,63,z19)   exit 0   176.9s
run 2   5780 frame(s), body ended at (150,63,z19)   exit 0   179.5s
```

No stall, no crash, ends on the card by its own hand and closes its own window.
**Exit code checked explicitly** — the heap-corruption-on-exit the previous
program found is still fixed.

Two `--demo-capture` runs afterwards: **22 frames each, byte-identical to each
other and byte-identical to all 22 committed frames in `docs/frames/demo/`.**
Nothing needed re-capturing. The route is deterministic and the committed
frames are honest.

The 23rd, `creation-origin.png`, **dropped on both runs** — the shutter race
below. Eight capture runs across three programs have now lost it.

## What moved in these two sprints

| | |
|---|---|
| **The character screen ends after what is on it** | Every step of creation now measures its own composition and recomposes over a grid that many rows shorter. Ink inside the panel roughly doubled — the quiz went 8.0% → 13.7%. |
| **The casebook ends after what is in it** | Same mechanism, opposite judgement: the lead *list* is static under the cursor so it sizes; the detail pane is what the cursor swaps, so it is held. One floor for both tabs, because LEFT/RIGHT is itself a cursor. |
| **The end card owns the frame** | `Session::setHudStandDown()`, asked before `drawFrame`. The card is now the ward at night and the card — no compass, no clock, no case bar, no street sign. |
| **A pad can start the game** | The single biggest gap in the previous ship note, closed. An on-screen keyboard, built as a `CreationPage` so layout, drawing and the mouse hit-test all come from one composition. **Verified this phase with a real SDL virtual gamepad** — see below. |
| **The last arrow became a fill** | `menu_view.cpp`'s four tiled panels no longer draw `>` plus a breathing hairline. |
| **F1 and F2 do what `--help` promised** | Both were dead since #85 retired the `Action` that carried them. Wired beside F3, with F3's own yield rule. |
| **The ruler stopped moving** | `signage.exclusion` was computed even under `--nohud`, so the baseline stepped around a prompt that was never drawn. Every `CLAIMED` figure in `docs/HUD-REAL-ESTATE.md` older than sprint 1 is overstated. |

## Driving every input, this phase

**Keyboard and mouse**: the gate's own windowed smoke boots and photographs the
world (`dist\verify-smoke.png`, 85,551 bytes, `steps=40 at (152,63,z19)`).

**Pad — creation, end to end, no keystroke.** `--padcreation` attaches a real
`SDL_AttachVirtualJoystick`, so `route_menu_key`, `key_of_pad_button`, the stick
latch and the trigger edges are the shipped paths, exercised whole:

```
granadad: playing as ARK (calling)
granadad: custom sheet set 12 skill(s)
granadad: docks_surface loaded, 28 lamp(s)
```

`ARK` was spelled **on the on-screen keyboard, one D-pad move and one A press
per letter**, then committed with START, then BEGIN was reached with 25 D-pad
downs and pressed with A. Sprint 2's claim is true and is now photographed.

**Pad — the world surfaces, which nobody had managed before.** One process
drove creation and then the world, eleven surfaces, and exited 0 of its own
accord. All work: **D-pad Up** casebook, **SELECT** ward map, **START** pause,
**B** back out of pause, **A** interact, left stick look, **RT** cast. Frames in
`docs/frames/ship/`.

Two things I got wrong on the way and am recording so the next person does not
repeat them: a D-pad `down` that appeared to drop presses was the 26-row
customise list **wrapping** (34 presses mod 26 = the row it landed on — the
input is exactly 1:1); and a pause-menu foot that looked clipped at 640x360 is
**not** clipped — magnified 4x it closes on its own `+~-~-` rule.

## The ruler, corrected, at 640x360

First correct `CLAIMED` figure since the `--nohud` exclusion bug was fixed:

| scene | INK | CLAIMED |
|---|---|---|
| street (`--smoke=30`), 640x360 | 8,072 (**3.50%**) | 13,971 (**6.06%**) |

The full-screen surfaces are degenerate for the diff ruler (they cover the
world entirely), so what follows is **ink density of content inside the composed
panel**, plus the number that actually matters now — how much of the frame is
left dead beneath it:

| surface at 640x360 | panel | dead black below | ink in panel |
|---|---|---|---|
| creation — THE DOOR | 164px (45.6%) | **194px (53.9%)** | 8.06% |
| creation — YOUR PAST | 185px (51.4%) | 173px (48.1%) | 8.61% |
| creation — THE SHEET | 164px (45.6%) | **194px (53.9%)** | 9.55% |
| creation — THE NAME (on-screen keyboard) | 129px (35.8%) | **229px (63.6%)** | 7.72% |
| casebook, new game | 199px (55.3%) | 159px (44.2%) | 6.80% |
| **the ward map** | **360px (100%)** | **0px** | **23.67%** |

Read the last row against the rest. The map is what a finished surface in this
game looks like, drawn with the same vocabulary.

---

## The verdict: still NOT near ready for early access

The previous program said "a very good vertical slice with a demo that punches
above the build." **That is still the right sentence.** Two sprints went in and
the honest change is *from* "the first screen is two-thirds empty" *to* "the
first screen is a dense panel with a dead black half under it." That is a real
improvement in legibility and a lateral move in whether it looks finished.

What a stranger sees in the first sixty seconds, **after the placement pass**:

1. **THE DOOR.** A tight, well-set terminal panel — breadcrumb, five numbered
   doors, an inverted fill, a detail pane, a nav row — **and it now sits in the
   frame rather than hanging off the top of it**: 87px above, 109px below,
   which is the seat and not an accident. It reads as a window that was placed.
   ~~2px of margin above and 194px below.~~
2. **THE NAME**, if they are on a pad. **Now a keyboard block**: ten across by
   three down, keys one cell apart, the rub-out at the bottom right. It was
   six one-glyph columns on a ten-cell stride, which read as five sparse
   vertical strings. It is no longer the worst-composed screen in the build.
3. **The casebook, pushed at them.** A new game opens the world **with the
   casebook already up** — verified, no input, 3.5s after the world window
   opens. It holds **one lead** in a pane whose floor is 20 rows, so the master
   half is mostly stipple, and 44.2% of the frame is black. It is the second
   screen of the game, and it is not something you have to go looking for.
4. **Then the ward**, and the ward is genuinely good. Chunky voxel city, a
   compass ribbon, named buildings, `E - TALK` over a passing serf, a 3.5%-ink
   HUD. **The ward map is excellent** and fills its frame.

So: the world holds up, the map holds up, the demo holds up, **and the framing
of the full-screen pages now holds up too** — that was the standing complaint
against the first two things anyone touches, and it is answered. What is left on
first contact is not layout at all. It is **emptiness**: one lead in the
casebook's master pane, `THE LETTERS` a quarter-screen panel with a header and
nothing in it, `THE CHART` with three rows. A well-placed frame around an empty
pane still reads as unfinished, and no further layout work will change that —
which is exactly what item 3 below is for, and why it is now the first thing.

Still not near ready. Closer than it was, and the reason has moved from how the
pages are composed to what is written in them.

## What the placement pass moved

Two of the three items below, in the order they are listed. Each gated on its
own (`docker compose run --rm --build build`, ~150s of test time each) and
committed as it went green.

### 1. The composed page is placed, not pinned — `24fa403`

Sprint 1 of the last program taught these pages to STOP at their content and
never told them where to SIT, so they hung off the top edge with 2px above and a
hole underneath. `panelSeatY()` in `render/panel.hpp` is now the one place that
decides, shared by `creation_page.cpp` and `casebook_page.cpp`.

**Not a true half.** `kPanelSeatAbove` is 45 — a box of type centred by
arithmetic reads LOW. All three splits were looked at as frames at 640x360
(`docs/frames/placement/seat-*.png`): 50/50 sits visibly low, 40/60 reads
top-heavy with the foot floating, 45/55 reads placed.

| surface at 640x360 | above / below, before | after |
|---|---|---|
| THE DOOR / THE VERDICT / the review / THE SHEET | 2 / 194 | **87 / 109** |
| the quiz | 2 / 180 | **81 / 101** |
| THE CALLING | 2 / 166 | **74 / 94** |
| YOUR PAST | 2 / 173 | **77 / 98** |
| GABRI's sheet | 2 / 222 | **100 / 124** |
| THE NAME (on-screen keyboard) | 2 / 229 | **103 / 128** |
| the casebook, new game | 2 / 159 | **71 / 90** |
| the ward map, the controls page | 0 / 0 | **0 / 0 — untouched** |

The two full-frame pages are untouched **by construction and not by a special
case**: they compose at full height, so their spare is the two or three pixels
that do not divide into whole cells and `panelSeatY()` returns what the old
expression did. Both are byte-identical across the change. Nothing on these
pages anchors to the frame's edges — the ground is filled whole-frame and the
border encloses only the composition — so there was nothing for the seat to
collide with, and the mouse moved with the drawing because both read the one
layout.

**Say the honest thing about the panels themselves: none of them got smaller.**
The dead space is the same number of pixels. What changed is that it is margin
on both sides instead of a hole underneath.

### 2. The on-screen keyboard gets its own grid — `c85622c`

It was thirty rows of an option list capped at six columns, so it inherited
`planOptionList`'s **spread** rule and came out at 640x360 as six one-glyph keys
on a **ten-cell stride** — five sparse vertical strings.

`render/panel.hpp` gains a **key grid** beside the option list and the block
list: `planKeyGrid` / `drawKeyGrid` / `keyGridAt`, the same three-function shape
the other two have. The rule that differs is one line — **the advance is the
widest a key can afford, not the pane divided by the columns.**

| at 640x360 | before | after |
|---|---|---|
| grid | 6 x 5 | **10 x 3** |
| column stride | 10 cells (50px), against 7px rows | **2 cells (10px)** |
| the block | 195 x 21 px | **95 x 21 px** |
| the master pane | 42% of the body | **28%** |

Padding is **zero** and that was measured: a padded cap doubles the advance on a
one-glyph grid and puts the columns twenty pixels apart, which is the same
defect one notch milder.

**And the transpose is gone.** A key grid reads across by construction, so
`pageForOsk()` no longer reorders its cells into column-major draw order, the
cursor index IS the grid position, and `main.cpp`'s inverse transpose for the
mouse went with them — three expressions that had to stay identical, down to
none.

**Every input path driven, not asserted.** A real `SDL_AttachVirtualJoystick`
through `--padcreation` spelled a name on the new grid one D-pad move and one A
press per letter, committed with START, walked the twelve questions and reached
`granadad: playing as ABC (custom)`. The mouse hit-test is walked **cell by
cell** at three window sizes, because one that still thought the grid was
column-major would answer correctly for the first cell and wrongly for the other
twenty-nine. A real keystroke still closes the grid on its way to the name, so
somebody typing is never routed through it. `--creation=input-keyboard`, `-pad`
and `-mouse` are still byte-identical to each other.

**What it did NOT buy, against the note below's guess:** the frame is not
shorter. The NAME page's height is set by its DETAIL pane (about nine rows), not
by the grid, so three rows instead of five bought nothing. 129px before and
after.

### The demo

Two `--demo-capture` runs, **22 frames each, byte-identical to each other**.
`5780 frame(s), body ended at (150,63,z19)`, exit 0.

Six committed frames moved and every one of them is a page this pass re-seated:
`case-book.png`, `case-book-harls.png`, `creation-name.png`, `creation-quiz.png`,
`creation-quiz-answered.png`, `creation-sheet.png`. The other sixteen are
byte-identical — the world, the map, the night, the end card, all untouched.

`creation-origin.png` **dropped for the ninth run in a row** (the shutter race
below), so it was re-taken through `--creation=origin` at 640x360 instead. That
substitution is exact and now proven rather than assumed: the stale committed
frame is byte-for-byte the frame `--creation=origin` produced on the OLD binary
(sha256 `b6a7516a...`), so the two paths photograph the same composition.

### The world hash

Not touched, and not touchable by this pass: `git diff --name-only` across both
commits reaches only `native/` and `docs/`. No map, no `content/art`, no golden
re-blessed.

## The three things I would do next, in order

1. ~~**Centre the composed panel in the frame.**~~ **DONE** — `24fa403`, seated
   45/55 rather than the 97/97 this line guessed at, because 50/50 reads low.
2. ~~**Give the on-screen keyboard its own tight grid.**~~ **DONE** — `c85622c`.
   The "lets the frame be shorter" half of this line was wrong: the detail pane
   sets that page's height, not the grid.
3. **Write empty states for the three panes that ship empty** — the casebook's
   lead list on a new game, the tiled Menu's `THE LETTERS`, and `THE CHART`
   with three rows. This is content, not layout, and the previous program was
   right that layout cannot fix it. One written line each ("NOTHING HAS COME
   FOR YOU YET") costs nothing and removes the last "unfinished" tell.
   **Still open, and now the first thing a stranger meets that is wrong** — the
   framing complaint above it is answered.

## Still open, and why

1. **`--demo-capture` drops `creation-origin.png`.** A shutter race on the
   character screen; eight runs, eight drops. The demo itself plays the beat
   every time — only the capture misses it. Not touched this phase because the
   deliverable (`--demo`) is unaffected.
2. **Six pages still swallow clicks** — the tiled Menu, the pause menu and the
   pages behind its rows, a conversation, the wait page and the lockpick. The
   pattern to copy is the map branch in `session_pointer()`: ask the page's own
   `*Layout` for the composition the drawing read, call its `*AtPixel` inverse.
   `mapPageLayout`/`mapPlaceAtPixel` and `creationPageHitTest` are the worked
   examples. The on-screen keyboard gained clicks for free by being a
   `CreationPage`, which is the argument for the pattern.
3. **The input router is still in `main.cpp`'s anonymous namespace.**
   `route_menu_key`, `session_pointer`, `creation_input` and `creation_pointer`
   cannot be linked by a suite, so no test touches a line of them — including
   the on-screen keyboard's own routing branch, though the flow underneath it
   has five new cases. This was the previous note's #3 and it is still #3 in
   engineering terms; it lost to the three above only because none of them are
   visible to a player.
4. **`menu_view.cpp`'s four tiled panels are still not on the terminal
   grammar.** Sprint 2 converted the selection highlight to an inverted fill;
   the frames are still solid rectangles rather than `+~-~-` rules with
   alternating `|`/`!` edges.
5. **No camera motion anywhere in the demo.** It cuts, walks and turns; it
   never dollies on a still.

## The shutdown crash, in one paragraph

`run_client()` held the audio engine in a function-scope `unique_ptr`, so it was
destroyed on `return` — *after* `SDL_Quit()` had already torn down the audio
subsystem and the callback thread the engine's stream belonged to. Closing a
stream SDL has freed is a use-after-free. Fixed in `efe9463`: detach, drop the
engine, then let SDL go. **Re-checked this phase**: two full `--demo` runs and
five windowed pad runs, every one exit 0.
