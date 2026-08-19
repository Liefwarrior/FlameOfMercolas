# How much of the frame is interface

The owner's note was one sentence: *"I love the vibe of the UI but just be more
careful with real estate."*

"Too much of the screen" is an opinion until somebody puts a number on it, so
this is the number, how it is measured, and what it was before and after.

## The ruler

`--nohud` draws the world and nothing over it — no HUD, no conversation
surface, no capture stamp. Capture the same scene twice, and every pixel that
differs between the two PNGs is interface. No estimating, no counting glyphs.

```
dist\granadad.exe --smoke=30 --width=960 --height=540 --scale=1 \
    --screenshot=frames\after-street.png
dist\granadad.exe --smoke=30 --width=960 --height=540 --scale=1 --nohud \
    --screenshot=frames\nohud-street.png
```

The scripted-scene flags work the same way, so the same pair can be taken of a
conversation (`--skyrun=talk`) or the rooftops (`--roofs=roof`).

Two figures come out of the pair:

- **ink** — pixels the interface actually changed. It is a diff and cannot be
  argued with.
- **claimed** — the ink mask closed up by one glyph advance horizontally and
  one pixel row vertically, so the gaps inside and between letters belong to
  the row that owns them, then the bounding box of every blob unioned. That is
  the screen real estate a row *holds*, which is the thing you actually lose.

`scripts/hud-real-estate.py` does the arithmetic:

```
python scripts\hud-real-estate.py <frames-dir> 15 3
```

(15 and 3 are the closing gaps in pixels: one glyph advance and one row of
shadow at 960x540. They scale with the capture.)

## The numbers, at 960x540

| scene | ink before | ink after | claimed before | claimed after |
|---|---|---|---|---|
| street (`--smoke=30`)      | 30,063 (5.80%)   | 15,111 (2.91%)  | 42,474 (8.19%)  | 26,569 (5.13%)  |
| rooftops (`--roofs=roof`)  | 35,706 (6.89%)   | 21,324 (4.11%)  | 50,745 (9.79%)  | 31,500 (6.08%)  |
| conversation (`--skyrun=talk`) | 222,720 (42.96%) | 187,200 (36.11%) | same | same |

The conversation's two figures are equal because the surface is a filled panel:
every pixel of the band is claimed and inked at once.

The world render is bit-identical across the change — the smoke line's own
`world px / sky px / sprite px / luma / colours` fields are computed before the
HUD is drawn, and all three scenes print exactly the same values before and
after. That is what makes one `--nohud` capture a valid baseline for both eras.

## What actually changed

Nothing about the look. Same 4x6 font, same colours, same corners, same rule
about the middle of the screen (`hudCentreRect`, and the test that proves it).

1. **Two sizes, not one.** `hudScale()` is the register you read at a glance:
   compass, hour, health bar, a shout. `hudMinorScale()` is one step down and is
   what everything you read *deliberately* is drawn at: place name, purse,
   standing, heat, sack, stealth, case, rung, objective, rival, room. Ten
   sprints added ten rows and drew every one at the size of the health bar.

2. **Absence costs nothing.** `NOBODY IN PARTICULAR` is the ward having no
   opinion of you. It held twenty characters of the top right in every frame
   this game has produced. It appears when there is an opinion.

3. **Rows are allocated, not numbered.** Every bottom-band row carried a
   hand-written offset — alert 23, lock 31, case 16, guild 24 — which is how S9
   shipped the lock row printed through the guild row and S10 shipped a clue
   printed through the case row. `BottomBand` hands out slots in priority order
   and refuses one that crosses the exclusion rectangle. The top-right stack
   drops its least important row rather than running into it.

4. **The health bar loses the word HP** (a red segmented bar bottom-left is not
   ambiguous) and a quarter of its width. 48 scale units divide by sixteen
   segments exactly; 62 left ten units of dead track on every full bar.

5. **The conversation's detail row** is drawn only when the column actually cut
   the label, rather than repeating the highlighted row above it every time.

6. **The conversation's bottom band is sized from the rows it draws**, which is
   the rule its top band has always had.

7. **The capture stamp is drawn at 1:1.** It is provenance and not a HUD
   element — the windowed game has never drawn it — and it was taking 222x21
   pixels of the top-left corner of every screenshot this project has ever
   produced. The version is on the F1 keys page, where a player looks for it.

## The gap this closed

`hud.hpp` carried a `VERIFICATION GAP (S10)`: the guild and objective rows
crossed the exclusion rectangle at 320x180 and 640x360 whenever they were
non-empty, and — undocumented — the six-row top-right stack crossed it at
320x180. Both were real. Nothing went red because no case had ever lit more
than five fields of `HudState` at once, and the defect needs eleven.

*"every HUD row lit at once still leaves the centre clear"* lights all sixteen
at 320x180, 640x360 and 960x540. *"the HUD costs a fraction of the frame, and
the fraction is pinned"* holds a fully lit HUD under 6.5% of the frame; it
measures 5.78%.

## District Phase D: the threshold plate

A new drawn element, and it is in this file because a new drawn element is
exactly the thing this file exists to hold to account.

**What it is.** Crossing into a named place — out of the Quayward compound's
east gate onto Saltgate Rise, in at the Gilded Gull's door — puts the place's
name on a plate centred under the compass ribbon for two seconds, rising as it
fades. `HudState::placePlate` / `placePlateFade` / `placePlateDrift`, drawn by
`drawPlacePlate` in `hud.cpp`, driven by `Session::placePlateAnim_`.

**Why it is not just the location row.** `locationLabel` — the ribbon's own dim
sub-label — is REFERENCE: up every frame, one size down, read when you want it.
This is an EVENT. The ward's authored names (`docks.hpp` `kPlaces`) used to
announce themselves by quietly changing four small words nobody is looking at.
Both are drawn off the same `placeNameAt`; neither is derived from the other,
because one is a state and the other is an edge.

**What it costs.** Nothing at rest — `placePlateFade` defaults to 0 and an
untouched `HudState` is pixel-identical, which is proved rather than asserted
(*"a HudState that never heard of the plate is pixel-identical"*). While it is
up, at 960x540 with `hudScale` 3:

```
"SALTGATE RISE"   13 glyphs x 5 advance - 1  = 64 units
                  x scale 3                  = 192 px of text
                  + 2 x padX (2 x scale)     = 204 px wide
kGlyphH 6 x 3 + 2 x padY (1)                 =  20 px tall
                                               4,080 px = 0.79% of the frame
```

for two seconds after a crossing and for no other reason. It is the only
element on this HUD that is neither furniture nor a response to a keypress.

**Where it may not go.** The centre-clear rule is unchanged and this element is
the most obvious candidate in the game for breaking it — a place-name
announcement wants to be a big centred title card. It is not one. It sits in
the top band under the ribbon, and:

- it is **dropped** rather than drawn if the frame is too short for its whole
  travel to clear the exclusion rectangle (`BottomBand::take()`'s rule at the
  other edge);
- it is **drawn a size down and then dropped** rather than overlapping the
  top-right stack. `drawTopRight` now returns the width it actually claimed —
  every row up there is clipped to `width - 2 * margin`, so a single heat line
  at its longest reaches past the middle of the frame and a fixed reserve would
  have been a guess. At 320x180 with the corner at its widest there is no
  centre channel left at all, and the honest answer is nothing;
- it is **whole or not at all**. The alert row clips because the front of a
  bouncer's warning still carries the warning; the front of a place name is not
  a shorter place name.

**When it does not draw.** While any panel owns the screen — a conversation,
the tiled Menu, the ward map, the keys or options page, the pause menu — on the
same `conversingNow()` test `showCompass` already uses, because the plate lives
in the compass's band and obeys the compass's rule. It stands down rather than
pausing: the countdown is zeroed, so nothing pops the instant a menu closes.

**Photographing it.** `--threshold=WHERE` walks the body across one of four
measured crossings and `--threshold-end=back` turns it round to look at what it
came through:

```
dist\granadad.exe --smoke=0 --hold --threshold=saltgate --threshold-end=back \
    --time=13 --width=960 --height=720 --fov=100 \
    --screenshot=docs\frames\district-d-saltgate-gate-frame.png
```

The run prints `threshold saltgate from="-" to="SALTGATE RISE" crossed=yes
plate="SALTGATE RISE" up=yes` beside the frame, because a picture of a plate
that never fired looks exactly like a picture of a street.

## Panes pass: the controls page, and the honest number

A new drawn surface, so it is in this file, and the number went the wrong way.
Say it first and argue afterwards.

| scene (960x540) | ink | claimed |
|---|---|---|
| controls page, before (`--pause=controls`) | 187,200 (36.11%) | 187,200 (36.11%) |
| controls page, after                       | 510,720 (98.52%) | 510,720 (98.52%) |

The old page was a conversation panel: a top band, a 3x4 topic grid in the
bottom band, and a strip of street between them. The new one is a full-page
composed frame and it takes the frame.

**Why that is the right trade here, and where the line is.**

The ruler exists because of one sentence — *"I love the vibe of the UI but just
be more careful with real estate"* — and that sentence is about the HUD: the
thing that is on screen while you are playing, that you did not ask for, and
that you cannot put down. **The HUD is untouched by this pass.** The only line
this pass adds to `HudState` construction is inside `if (keysOpen_)`, so any
frame in which the controls page is not open is bit-identical. The ambient
street HUD measures ink 17,781 (3.43%) / claimed 24,939 (4.81%) at 960x540 with
this build, which is where the previous pass left it.

The controls page is the other kind of surface: one you open on purpose, read,
and put down with the same key. The tiled Menu is already exempt from the
centre-clear rule for exactly this reason (`menu_view.hpp`: "there is nobody TO
look at while it is up"), and the owner's own reference frames are all
full-screen compositions.

**What the extra pixels bought**, which is the number that actually answers
*"use screen real estate more efficiently"*:

| | before | after |
|---|---|---|
| binding rows visible at 960x540 | 9 of 29 | **29 of 29** |
| pages to see the whole list | 4 | **1** |
| explanation of the highlighted row | none | a full paragraph, beside the list |
| claimed pixels per row shown | 20,800 | **17,611** |

Per row of information delivered the page is *cheaper* than it was, and it
stopped hiding three quarters of itself behind a MORE key. A player looking for
the map key no longer has to turn three pages to find out this game has one.

**At every other window size**, from `docs/frames/panes/`:

| window | grid | list layout | pages |
|---|---|---|---|
| 320x180   | 64x25 cells  | detail pane collapses, list takes the body, 2 columns | 1 |
| 640x360   | 128x51 cells | master/detail, 2 columns | 1 |
| 960x540   | 96x38 cells  | master/detail, 2 columns | 1 |
| 1280x720  | 85x34 cells  | master/detail, 2 columns | 1 |
| 1920x1080 | 76x30 cells  | master/detail, 1 column  | 2 |

The biggest window is the narrowest in CELLS, because `hudMinorScale` steps up
with the frame height — 1920x1080 has 76 cells across where 960x540 has 96. That
inversion is why the composition's minimum detail width is 26 cells and not 30,
and it is why 1920 is the one size that still pages.

**Stable geometry, measured rather than asserted.** `keys-960x540.png` and
`keys-960-cursor-lock.png` are the same page with the cursor eighteen rows
apart. Every pixel outside `x 10..950, y 88..466` — which is to say the border,
all three rules, the column divider, the tab row, the instruction header and the
global nav row — is IDENTICAL between them. Only the inside of the body pane
changes when the cursor moves.

---

## The creation flow (#93) — a full-screen surface measured against itself

Seven steps, `docs/frames/chargen/`, all at 960x540 with `--scale=1`.

**The `--nohud` method does not apply here and the section above already says
why**: there is no world underneath this screen, so a diff against the same
scene without the interface is a diff against nothing and both numbers come out
at 100%. The base used instead is the screen's own **clear colour**, so INK is
every pixel the composition put on top of the backdrop and CLAIMED is the
ruler's own closing (15, 3) and bounding-box pass over that mask, **unioned**
rather than summed — a full-screen composition has overlapping blobs, and
summing their areas reports more than a hundred per cent of a screen, which is
not a number anybody can act on.

| step | ink before | ink after | claimed before | claimed after |
|---|---|---|---|---|
| door (origin)  | 146,062 (28.18%) | **54,892 (10.59%)** | 509,760 (98.33%) | **489,600 (94.44%)** |
| calling roster | 153,767 (29.66%) | **69,060 (13.32%)** | 509,760 (98.33%) | **489,600 (94.44%)** |
| quiz           | 159,434 (30.76%) | **79,220 (15.28%)** | 512,640 (98.89%) | **489,600 (94.44%)** |
| verdict        | 161,388 (31.13%) | **60,172 (11.61%)** | 512,640 (98.89%) | **489,600 (94.44%)** |
| your past      | 140,699 (27.14%) | **68,352 (13.19%)** | 509,760 (98.33%) | **489,600 (94.44%)** |
| review         | 142,954 (27.58%) | **67,776 (13.07%)** | 509,760 (98.33%) | **489,600 (94.44%)** |
| custom sheet   | 142,375 (27.46%) | **73,476 (14.17%)** | 512,640 (98.89%) | **489,600 (94.44%)** |
| Gabri's sheet  | 137,335 (26.49%) | **57,696 (11.13%)** | 509,760 (98.33%) | **489,600 (94.44%)** |

Both numbers went DOWN on every step, and the second one is the interesting one.
The old screen **already claimed 98% of the frame** — a hand-painted warm light
pool tinted every pixel of it — while only a quarter of that carried any ink at
all. It was claiming the whole screen to show almost nothing on it. The composed
frame claims 94% (the grid, less the margin the window's leftover pixels take)
and puts two to three times more information inside the same box while lighting
half as many pixels.

**What the pixels bought.** Ink is only half the ruler; what the interface
delivers per pixel is the other half.

| | before | after |
|---|---|---|
| sheet rows visible at 960x540 | 9 of 25, then `0 MORE (1/3)` | **25 of 25** |
| pages to see the whole sheet | 3 | **1** |
| longest quiz answer as drawn | clipped at 18 glyphs *including its row number* | **whole, wrapped** |
| copies of the hovered answer per frame | 3 (detail row, centre line, grid row) | **1** |
| what an answer costs | nothing shown | the named effects and their numbers, beside the choice |
| where the quiz is heading | nothing shown | `HEADING FOR  NETTER`, off the real tally rules |

**Across the window sizes**, from the same directory:

| window | grid | quiz layout | sheet layout |
|---|---|---|---|
| 320x180   | 64x25 cells  | detail pane collapses, answers take the body | list takes the body |
| 640x360   | 128x51 cells | master/detail | master/detail, 2 columns |
| 960x540   | 96x38 cells  | master/detail | master/detail, 2 columns |
| 1280x720  | 85x34 cells  | master/detail | master/detail, 2 columns |
| 1920x1080 | 76x30 cells  | master/detail | master/detail, 2 columns |

At 320x180 the split collapses and the consequence pane is not shown. That is
the honest limit of this composition rather than two panes too thin to read, and
it is what `MasterDetail::split` exists to let a screen say out loud.

**The HUD is untouched.** Nothing in this pass reaches `hud.cpp`, the world
render or the street overlay; the ambient street HUD is unchanged at ink 17,781
(3.43%) / claimed 24,939 (4.81%). The one sibling surface that moved is the
CONTROLS page, and only because the shared `drawTabRow` now keeps a cell of air
between its right-aligned readout and the frame's own edge — flush against it,
`GRANADAD 0.10.0` and `NAMELESS` both read as if the border were punctuation.

**Input parity, as a comparison rather than a sentence.**
`parity-keyboard-960.png`, `parity-pad-960.png` and `parity-mouse-960.png` are
the same journey — open the quiz door, answer four questions, hover the second
answer — driven by three different devices through the one dispatch the SDL loop
uses. The mouse walk finds its own pixels by asking the screen's own hit-test
where a row is. All three files have SHA-256
`7eea92f398c303bc0f43fe8c489ca4b42c4ed9827d94084b1c42e74a9afc0f49`.
