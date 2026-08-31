# Ship note — read this first

**Nothing is broken.** Both halves of the gate are green on a fresh isolated
worktree, the world hash is byte-identical to where this program started, the
demo ran twice and exited 0 both times, all 23 committed demo frames are
byte-for-byte what the shipped binary draws today, and pad, keyboard and mouse
each drove creation end to end and out into the ward.

Gate: **green, both halves**, revision `7f69334`.
World hash: **`0x2646C1AAA2BA38DF`** — regenerated, rebaked, twin-gated, unmoved.
Demo: **run twice, watched, exit 0 both times.** Capture is deterministic and
**not one committed frame moved**.

## Run the demo

```
.\dist\granadad.exe --demo
```

Two minutes, plays itself, ends on a card and closes its own window. ESC stops
it early. `dist\` already holds the gate-certified binaries — nothing to build.

Want the stills too? `.\dist\granadad.exe --demo-capture=DIR`
Want one section? `.\dist\granadad.exe --demo=case` (`quay saltgate case map night end`)

Play it normally with `.\dist\granadad.exe`. On the character screen the
fastest way in is **DOWN DOWN ENTER** (WALK YOUR OWN PATH) → **ENTER** on the
NAME row, type a name, **ENTER**, then 25 × **DOWN** to BEGIN, **ENTER**,
twelve questions about your past, back to the sheet, BEGIN again. In the world:
**M** map, **TAB** casebook, **E** talk, **F1** keys, **ESC** pause.

**On a pad**, all of that works with no keyboard at all: **A** confirms, **A**
on the NAME row raises an on-screen keyboard, **START** commits the name,
**A** on BEGIN. In the world: **D-pad Up** casebook, **SELECT** map,
**START** pause, **A** interact, **B** crouch, **X** attack, **Y** up/down,
**LT/RT** block and cast, left stick look.

> **Correction to the last note.** It said "**B** back". B is **crouch** —
> that is the shipped default (`controls.cpp:486`) and it is what the frame
> shows (`docs/frames/ship2/pad-crouch-640.png`, the word `CROUCHED` on the
> prompt line). Backing out of an open panel is **START**, which backs out of
> whatever is open first and only opens the pause menu when nothing is. So a
> pad player reaches the pause menu with two presses of START from a new game,
> because START's first press dismisses the casebook the game opened for them.

---

## The gate, in full

| half | result |
|---|---|
| `docker compose run --rm --build build` | **exit 0.** `100% tests passed, 0 tests failed out of 75`, 134.6s of test time, 195.6s wall |
| `scripts\verify-windows.ps1` | **=== PASS ===**, exit 0, 121.1s |

```
gate executed:  2026-08-31T19:45:12Z UTC
revision:       7f69334
native/ digest: 357dad7655adeec2e6eb45cc67ce8a5593178b945b38ae22794fe0846eb4339e
ctest cases:    942 (floor 537)
native/ files:  270
```

Cases and assertions actually executed under mingw/Windows:

| suite | cases | assertions |
|---|---|---|
| sim + render (`granadad-tests.exe`) | 861 | 1,317,802 |
| content (`granadad-content-tests.exe`) | 71 | 902,135 |
| **total** | **932** | **2,219,937** |

The two comparators, both byte-for-byte identical linux/gcc vs mingw/windows:

```
decoded world state   3884 bytes  sha256 97850DCB…C1B2179   IDENTICAL
world hash + sim run  1791 bytes  sha256 924F6EA6…8B7468B   IDENTICAL
gate stamp            the published green names THIS native/ tree
```

Run in a **fresh isolated worktree** at `C:\repositories\granadad-ship-7f69334`,
with the four audio directories the sound bank actually names (`Foley Sounds`
87 files, `Impact Sounds` 132, `Interface Sounds` 102, `RPG Audio` 53 — 374
files, 4.4 MB) **real-copied**: they are gitignored, so a checkout does not
carry them, and junctions do not survive the copy the build makes.

**`dist\` in the main repo carries these binaries and was re-verified against
them.** `verify-windows.ps1` was run a second time from
`C:\repositories\FlameOfMercolas-MVP` and passed, with the gate stamp's digest
matching the main repo's own `native/` tree — so the owner runs the demo with
no build step and the thing in `dist\` is the thing that went green.

## The world hash did not move — regenerated and rebaked, not asserted

This program was render and content-text only. The proof is four numbers:

```
1. regenerate   python tools\scripts\gen_docks_surface.py
                content/maps/src/docks_surface.tmx
                sha256 cceda566…237b4d1c  BEFORE and AFTER — git status clean

2. rebake       ToolsLauncher import-map docks_surface.tmx <scratch> --raws content\raws
                17,954 bytes, sha256 e47da3ae…9e474c2ac
                cmp against content/maps/baked/docks_surface.trojsav: IDENTICAL

3. twin-gate    dist\granadad-twin-gate.exe --population --population-hour 16 --ticks 7200
                run A  0x2646C1AAA2BA38DF
                run B  0x2646C1AAA2BA38DF     report text 18,772 bytes, IDENTICAL

4. no re-bless  golden_java_vectors.hpp, content/tests/fixtures.hpp,
                test_world_reader.cpp, sim/docks.hpp, docks_surface.tmx,
                docks_surface.trojsav, BASELINE-WORLD-HASH.md
                — all seven byte-identical to this program's first commit (d32942e)
```

`0x2646C1AAA2BA38DF` at 18,772 bytes is exactly what `docs/BASELINE-WORLD-HASH.md`
records for District Phase D. `git diff --name-only d32942e..HEAD` reaches only
`docs/` and `native/` — **no `content/`, no map, no `content/art`.**

## The demo

Two full windowed runs, watched start to finish:

```
run 1   5780 frame(s), body ended at (150,63,z19)   exit 0   117.6s
run 2   5780 frame(s), body ended at (150,63,z19)   exit 0   117.5s
```

No stall, no crash, ends on the card by its own hand and closes its own window.
**Exit code checked explicitly** — the heap-corruption-on-exit the earlier
program found is still fixed.

Two `--demo-capture` runs afterwards: **22 frames each, byte-identical to each
other and byte-identical to all 22 committed frames in `docs/frames/demo/`.**
**Nothing moved.** Which is also the proof that the street HUD, the ruler, the
ward, the night and the end card are untouched by the last two passes.

The 23rd, `creation-origin.png`, **dropped again** — the shutter race below,
eleven capture runs across four programs. Re-taken through
`--creation=origin --width=640 --height=360 --scale=1`: sha256 `5650b135…`,
**byte-identical to the committed frame**. (Note for whoever takes it next:
`--creation` upscales by `--windowScale`, which defaults to **2**. Without
`--scale=1` you get a 1280x720 PNG and a false mismatch. I made that mistake
first.)

## Driving every input, this phase

All three devices completed creation and reached the ward. All three exited 0.

**Keyboard — real SendInput scancodes through `scripts\drive-windowed.ps1`.**
DOWN DOWN ENTER at the door, ENTER on NAME, `C L A W` typed, ENTER, 25 DOWN to
BEGIN, ENTER, twelve past questions, back to the sheet, BEGIN — then TAB, W,
M, F1 in the world:

```
granadad: playing as CLAW (custom)
granadad: 1529 frame(s), body ended at (150,63,z19)   exit 0
```

**Mouse — real relative motion and real clicks, same harness.** Hovering the
ward map at (85,260) selects THE QUAYWARD COMPOUND: the footprint rect lights
in the accent, the detail pane fills (`KIND DOOR / STANDS ON SALTGATE RISE /
FOOTPRINT 64X19 TILES / BAND 20 / FROM YOU 90 PACES / INSIDE NOW 39 PEOPLE`),
the status line reads `SELECTED (104,137) THE QUAYWARD COMPOUND` and the foot
restates the cost as `ENTER — FACE IT (SW)`. Clicking a second place faces it
and closes the map. `docs/frames/ship2/mouse-map-hover-640.png`.

**Pad — a real `SDL_AttachVirtualJoystick`, creation AND the world, one
process.** The OSK survived its regrid: from the sheet, A on NAME raises the
10x3 grid, one D-pad move and one A per letter spells `PAD`, the detail pane
names `THE LETTER D` as it goes, START commits, and the sheet comes back
reading `NAME PAD`:

```
granadad: playing as PAD (custom)
granadad: 1108 frame(s), body ended at (136,65,z19)   exit 0
```

Then in the ward, on **shipped default bindings** (the repo-root
`granadad-controls.cfg` is the player's and was moved aside for this, then put
back): **D-pad Up** casebook, **SELECT** ward map, **START** pause menu,
**B** crouch, **A** a conversation with Petra Barnacre, **LT/RT** block and
cast, left stick look and walk. Frames for every one in `docs/frames/ship2/`.

`--creation=input-keyboard`, `-pad` and `-mouse` are still byte-identical to
each other and to the number the last program recorded: `0FE0753EAD693D20…`.

---

## The verdict: still NOT near ready for early access

The standing sentence was "a very good vertical slice with a demo that punches
above the build," and the last program's two passes moved it laterally. **This
program moved it forward — half a step, not a step.** I will not round that up.

What actually changed, measured on the shipped binary at 640x360:

| surface | before the placement pass | now |
|---|---|---|
| THE DOOR (164px panel) | 2px above / 194 below | **87 / 109** |
| THE SHEET, named, BEGIN ready (171px) | 2 / 187 | **84 / 105** |
| THE NAME (OSK, 129px) | 2 / 229 | **103 / 128** |
| casebook, new game (199px) | 2 / 159 | **71 / 90** |

Those are re-measured off this phase's own frames, not copied forward. THE DOOR,
THE NAME and the casebook reproduce the placement pass's table exactly; the
sheet row is a taller state of that page (a name typed and the past answered, so
one more row than the frame the placement table measured) and it seats on the
same 45/55 rule. The panels **are** seated now. Four
panes that shipped blank now say what they are waiting for and the sentences
are true. Both are real, both are correct, and neither is cosmetic.

**And here is what a stranger still sees in the first sixty seconds.**

1. **THE DOOR.** Well set and now well placed — but it is a **639 × 164 px
   letterbox in a 640 × 360 frame**, and **230 of the frame's 360 rows (63.9%)
   contain no lit pixel at all**. Inside the panel, ink is 8.84%. The vertical
   axis learned "size to content" and the horizontal axis did not: the panel is
   as wide as the window no matter how narrow its content is. The longest row
   in the master pane is `3 WALK YOUR OWN PATH  BUILD` — 27 glyphs, about
   135px, in a 310px pane. 83 of the panel's 639 columns carry three lit pixels
   or fewer.
2. **THE NAME**, for anyone on a pad, is the **emptiest screen in the build**:
   **71.9% of its rows are entirely black.** The regrid is a genuine
   improvement — it reads as a keyboard block instead of five sparse vertical
   strings — but it is now a 95 × 21px keyboard in the top-left corner of a
   310px-wide pane, inside a 129px letterbox, inside a mostly black frame. This
   is on the critical path for every pad player who names a character.
3. **The casebook, pushed at them** 3.5 seconds after the world opens, with no
   input asked for. It is the best of the three: 56.1% black rows, and the new
   sentence — `THE REST OF THE BOOK IS STILL BLANK. STAND OVER A LEAD AND LOOK.
   WHAT IT OPENS IS WRITTEN IN HERE.` — makes the empty half read as waiting
   rather than as a list that failed to load. That change is worth what it cost.
4. **Then the ward, and the ward is genuinely good.** Chunky voxel city, a
   compass ribbon, named buildings, a passing serf carrying her own prompt, a
   3.5%-ink HUD. **The ward map is excellent** and fills its frame.
5. **And then TAB, and the tiled Menu, which is the worst screen in the game.**
   It fills the frame (18.3% ink) — and it is drawn in **the wrong grammar**:
   four solid hairline rectangles, not `+~-~-` rules with `+`/`◆` junctions and
   alternating `|`/`!` edges. Every other surface in the build is on the
   terminal grammar; this one looks like a different program's UI pasted into
   it. On top of that the **CHARACTER tile draws ten rows and a `0 MORE (1/2)`
   into a pane forty rows deep**, so half of it is black for a reason that is
   not emptiness at all.

Two more things a pad player hits in that same minute, both found this phase:

* **Every prompt in the world names a keyboard key even when a controller is
  connected.** `E - TALK` over a serf, `E - LOOK` over a lead, `ENTER SELECTS
  ESC RESUMES` across the pause menu's header, `ENTER GO TO IT` and `TAB CLOSE`
  along the casebook's foot. The whole point of the pad work was that a pad
  player never has to touch a keyboard, and then the game tells them to press E.
* **The casebook page's tab row swallows clicks.** Keyboard LEFT/RIGHT moves
  LEADS ↔ THE CASE; two clicks on `THE CASE` at (125,86) and (125,83) did
  nothing. That is a seventh page for the "swallows clicks" list below, and it
  is on a page a new game opens unasked.

So: the world holds up, the map holds up, the conversation holds up, the demo
holds up, and the **framing** of the full-screen pages holds up. What is left on
first contact is no longer "the panel hangs off the top of the frame" and no
longer "the pane is blank and says nothing about it". It is **measure** — a
window-wide bar holding a third of its own area in text — and **grammar**, on
the one screen that ignores the house style. Plus the unfixable one: **one
authored case, twelve leads, five letters.** No layout rule and no sentence
touches that.

---

## What this program moved, in three commits

### 1. The composed page is placed, not pinned — `24fa403`

`panelSeatY()` in `render/panel.hpp` is the one place that decides, shared by
`creation_page.cpp` and `casebook_page.cpp`. **Not a true half**: `kPanelSeatAbove`
is 45, because a box of type centred by arithmetic reads low. All three splits
were looked at as frames (`docs/frames/placement/seat-*.png`). The two
full-frame pages (ward map, controls) are untouched **by construction, not by a
special case** — they compose at full height, so their spare is the two or three
pixels that do not divide into whole cells. **None of the panels got smaller.**
The dead space is the same pixel count; it is margin on both sides now instead
of a hole underneath.

### 2. The on-screen keyboard gets its own grid — `c85622c`

`planKeyGrid` / `drawKeyGrid` / `keyGridAt`, the same three-function shape the
option list and block list have, with one rule different: **the advance is the
widest a key can afford, not the pane divided by the columns.** 6x5 on a
ten-cell stride became 10x3 on a two-cell stride; the block went 195x21 to
95x21; the master pane went 42% of the body to 28%. Padding is zero and that
was measured. **The transpose is gone** — page, cursor and `main.cpp`'s mouse
inverse, three expressions that had to stay identical, down to none. The page
did **not** get shorter: its height is set by the detail pane, not the grid.

### 3. The panes that ship empty say what they are waiting for — `d168460`

Four panes, not the three the note asked for: **the casebook is TWO surfaces
showing one list** — the full-screen page behind TAB and the Menu's own casebook
tile — and both were blank under their single lead. They share one string.

| pane | copy |
|---|---|
| `THE LETTERS`, empty | `YOU HAVE READ NOBODY'S POST YET. STAND OVER A LEAD AND WHATEVER PAPER IT KEEPS TURNS UP HERE, IN THE HAND THAT WROTE IT.` |
| `THE LETTERS`, holding one | `STAND OVER MORE LEADS. WHATEVER PAPER THEY KEEP TURNS UP HERE.` |
| `THE CHART` | `EVERY LINE HERE IS DRAWN OUT OF THE CASEBOOK. HEAR A LEAD AND ITS GROUND, ITS BEARING AND ITS NAME COME WITH IT.` |
| the casebook, **both** surfaces | `THE REST OF THE BOOK IS STILL BLANK. STAND OVER A LEAD AND LOOK. WHAT IT OPENS IS WRITTEN IN HERE.` |

`DialogueViewState::emptyLine` sits beside `line`: `line` says what the panel
**is**, `emptyLine` says what would be **in** it and how you put it there, and
it is drawn **only into room the rows did not want**. Every sentence is
suppressed the moment it would be a lie — the book's when the trail is closed or
every lead is already in it, the Letters' the instant one letter unlocks. All
of it is visible on the shipped binary in `docs/frames/ship2/menu-*.png`.

---

## The three things I would do next, in order

1. **Give the composed panel a MEASURE, the way the placement pass gave it a
   seat.** `panelSeatY()` decides where a panel sits vertically; nothing decides
   how wide it is, so every full-screen page is 639px wide at 640x360 whatever
   it holds, and 63.9% of the frame goes dark under THE DOOR while 83 of its
   own columns carry three lit pixels or fewer. The move is the mirror of
   `panelSeatY`: one `panelSeatX()`/measure rule in `render/panel.hpp` that
   sizes the composition to the widest row it actually draws (plus the gutter
   the master/detail split needs) and seats it horizontally on the same 45/55
   judgement, with the two full-frame pages falling out untouched by
   construction exactly as they did last time. **Judge it off frames at
   640x360, not arithmetic** — the last pass proved that is the only way this
   call gets made right. THE NAME is the acid test: 71.9% black is the number
   to beat.
2. **Put `menu_view.cpp`'s four tiled panels on the terminal grammar, and let a
   tile's page size follow its pane.** These are one job, not two, because both
   are `drawMenuTiles`. The frames must become `+~-~-` rules with `+`/`◆`
   junctions and alternating `|`/`!` edges — it is the only surface in the build
   that is not, and it is the fifth screen a stranger meets. In the same pass,
   `topicRowsFor`'s hard cap of ten rows plus `0 MORE (1/2)` has to become a
   function of the pane's actual height, or the CHARACTER tile keeps drawing ten
   rows into forty rows of room. The implementing vocabulary for the rules
   already exists — use it, do not hand-roll a second copy.
3. **Make the prompts name the device that is holding them.** With a pad
   connected the world still says `E - TALK`, `E - LOOK`, `ENTER SELECTS ESC
   RESUMES`, `ENTER GO TO IT`, `TAB CLOSE`. The binding table already knows
   both keys for every `Action` (`controls.cpp`'s `set(Action, Key, Key)`) and
   already prints pad names on the keys page, so this is a lookup at the point
   of drawing rather than new state: ask which device last sent input, print
   that half of the binding. Until it is done, the pad path is complete in
   mechanism and incomplete in what it tells you.

## Still open, and why

1. **`--demo-capture` drops `creation-origin.png`.** A shutter race on the
   character screen; eleven runs, eleven drops. The demo itself plays the beat
   every time — only the capture misses it. Not touched, because the
   deliverable (`--demo`) is unaffected and the substitution through
   `--creation=origin --scale=1` is byte-exact.
2. **Seven pages swallow clicks** — the tiled Menu, the pause menu and the pages
   behind its rows, a conversation, the wait page, the lockpick, and (new this
   phase) **the casebook page's tab row**. The pattern to copy is the map branch
   in `session_pointer()`: ask the page's own `*Layout` for the composition the
   drawing read, call its `*AtPixel` inverse. `mapPageLayout`/`mapPlaceAtPixel`
   and `creationPageHitTest` are the worked examples.
3. **The input router is still in `main.cpp`'s anonymous namespace.**
   `route_menu_key`, `session_pointer`, `creation_input` and `creation_pointer`
   cannot be linked by a suite, so no test touches a line of them — including
   the on-screen keyboard's own routing branch.
4. **No camera motion anywhere in the demo.** It cuts, walks and turns; it
   never dollies on a still.
5. **One authored case, twelve leads, five letters.** The empty states make the
   blank halves read as waiting. They do not make the ward fuller.

## The shutdown crash, in one paragraph

`run_client()` held the audio engine in a function-scope `unique_ptr`, so it was
destroyed on `return` — *after* `SDL_Quit()` had already torn down the audio
subsystem and the callback thread the engine's stream belonged to. Closing a
stream SDL has freed is a use-after-free. Fixed in `efe9463`: detach, drop the
engine, then let SDL go. **Re-checked this phase**: two full `--demo` runs and
eight windowed pad/keyboard/mouse runs, every one exit 0.

## The frames behind every claim above

`docs/frames/ship2/`, all 640x360, all off the certified binary:

```
door-640.png                  THE DOOR, seated 87/109
pad-osk-open-640.png          the 10x3 key grid, nothing typed
pad-osk-spelled-640.png       PAD spelled on it, detail pane naming THE LETTER D
pad-sheet-begin-640.png       the sheet afterwards, NAME PAD, BEGIN gone READY
kb-casebook-newgame-640.png   the casebook a new game opens, empty state drawn
casebook-case-tab-640.png     THE CASE tab, reached by keyboard after clicks failed
menu-character-640.png        the tiled Menu — the wrong grammar, and the ten-row cap
menu-letters-640.png          the same with THE LETTERS focused, its sentence drawn
kb-keys-640.png               F1, the keys page, full height
mouse-map-hover-640.png       a real mouse hover selecting THE QUAYWARD COMPOUND
pad-wardmap-640.png           the ward map opened with SELECT
pad-pause-640.png             the pause menu opened with START
pad-street-640.png            the ward, walked with the left stick
pad-crouch-640.png            B is CROUCH, not back
pad-conversation-640.png      A opens a conversation with Petra Barnacre
```
