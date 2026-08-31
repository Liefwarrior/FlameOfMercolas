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

### The conversation again, after the panel-vocabulary conversion

The row above is the polish-1 pass. The conversation surface was rebuilt on
`panel.hpp` afterwards — bordered panes, a `subject > status` header, and a
topic list whose column count comes from its own longest label instead of a
fixed three columns of eighteen glyphs. Same scene, same ruler, measured
against the same `--nohud` capture (byte-identical between the two binaries,
which is what proves the world render was not touched):

| size | ink before | ink after | claimed before | claimed after |
|---|---|---|---|---|
| **640x360** | 72,960 (31.67%) | **47,151 (20.46%)** | 72,960 (31.67%) | **48,860 (21.21%)** |
| **960x540** | 164,160 (31.67%) | **153,436 (29.60%)** | 164,160 (31.67%) | **159,810 (30.83%)** |

Two things worth reading off that table rather than skipping:

- **The gain is a third of the surface at 640x360 and barely a twelfth at
  960x540**, because the body is drawn at `hudMinorScale`, which is 1 at
  640x360 and 2 at 960x540 — so the same rows cost twice the pixels at the
  larger size. The 960 band is already down to five rows plus its border and
  there is nothing left to cut that is not content.
- **`claimed` is now slightly LARGER than `ink`**, where before they were
  equal. That is the border motif doing its job: the `+~-~-` rules and the
  `|`/`!` edges are separate marks with gaps between them, so the closing pass
  has something to close. A solid filled band has no gaps and measures the
  same both ways.
- **The before column is not what a player lost.** Both bands used to drop
  content silently when they ran out of room — the top band cut the tail off
  any line longer than two rows, and the topic grid cut every label to
  eighteen glyphs. The after column holds MORE text in LESS space; see
  `docs/frames/conversation/before-640x360.png` beside `after-640x360.png`.

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

## The map pass — the ward map, and the number that would not move

`docs/frames/map/`, all at 960x540 with `--scale=1` at 13:00 on the authored
spawn, so before and after are the same scene at the same second.

**The `--nohud` method DOES apply here** — unlike the creation flow and the
controls page, the ward map is an overlay over a rendered first-person frame,
and `--nohud` draws that frame without it (`Session::drawFrame` gates the whole
page on `config_.hud`). `nohud-ward.png` is that control.

| scene, 960x540 | ink | claimed |
|---|---|---|
| ward map, **before** | 518,400 (100.00%) | 518,400 (100.00%) |
| ward map, **after**  | 510,720 ( 98.52%) | 510,720 ( 98.52%) |
| street HUD (untouched by this pass) | 17,709 (3.42%) | 24,831 (4.79%) |

**Both numbers are useless here and it is worth saying why rather than dressing
them up.** The old page opened by filling every pixel of the frame with its own
backdrop at 0.88 alpha, so it touched 100% of the screen by construction; the
new one is a composed panel whose grid leaves a few rows of margin, so it
touches 98.52%. Neither number is about legibility, both are the degenerate
full-frame case this document already records twice, and a two-point move in it
tells nobody anything.

### What the ruler cannot see, measured instead

The owner's complaint was not "the map is too big". It was *"Names are stacking
up on the map view. Makes it hard to figure out where the place you're looking
for is."* So the number that matters is **how much of the district the names
were covering**, and where they were sitting.

Method: `drawTextPlate` lays a hard-edged **pure-black** field under every label
it draws, and the plan's own material tones are never that dark — the `--nohud`
control reads 0.36% of the plan at the same threshold, which is the harbour and
the void showing through. So counting near-black pixels inside the plan's own
rectangle counts nameplate, and only nameplate.

| | plan rect at 960x540 | nameplate field inside the plan |
|---|---|---|
| control (`--nohud`, no interface at all) | 192,79–768,460 | 784 (0.36%) — the floor |
| **before** | 192,79–768,460 | **22,618 (10.31%)** |
| **after**  | 32,65–608,446   | **255 (0.12%)** |

**A tenth of the district was underneath a nameplate**, and the plates were
opaque: whatever ground they covered was not merely dimmed, it was gone. They
also spilled off the plan entirely — 3,997 further pixels of plate sat in the
margin around it, naming buildings by pointing at them from outside.

The new labels are set INSIDE the shape they name, over a translucent scrim
rather than an opaque plate, so they do not register at that threshold at all.
Computed analytically from the label rule instead (the box each placed name
occupies, summed):

| zoom | places named on the plan | doors named | label boxes as a share of the plan |
|---|---|---|---|
| **fit — the whole ward** | 30 of 55 | 24 of 40 | at most 6.40% |
| 2x | 44 of 55 | 35 of 40 | at most 2.50% |
| 3x | 53 of 55 | 39 of 40 | at most 1.37% |
| 4x | 53 of 55 | 39 of 40 | at most 0.75% |

Those percentages fall as you zoom because the plan grows faster than the type
does — and every one of those boxes is inside a building it names, so it hides
its own floor and nothing else. The two places never named on the plan face at any
zoom are The Drowned-Name Wall (a 3x3 shrine) and Wormwood Pier (3 tiles wide,
against an eight-glyph word); both keep their door dot, both are named by the
cursor and in the Index, and
`after-too-small-fallback.png` is the Wall photographed at the closest zoom with
no label and its cursor box around it. That is the fallback, deliberately, and
not a quiet return to stacking.

### What the pixels bought

| | before | after |
|---|---|---|
| plan size at 960x540 | 576x381 (42.3% of the frame) | **576x381 (42.3%)** — unchanged |
| names on the plan | one per distinct name, every one on a floating opaque plate, seated by a twenty-four-position avoidance solver | **30, set inside the shapes that own them; nothing floats** |
| district hidden under a name | 10.31% of the plan | **0.12%** |
| a name that could not find room | dropped, silently | named by the cursor, the selection line and the Index |
| what is at the place you are pointing at | nothing shown | name, flavour, kind, street, footprint, band, bearing, distance |
| who is at that place right now | nothing shown | **the People view**, off the live roster and the taproom |
| finding a place you were told the name of | read the plates | **the Index**, alphabetical, 55 entries |
| what the map costs the HUD | — | **nothing: `hud.cpp` is untouched** |

The plan is exactly the same size and carries fewer names on its face — and it
is the first version of this page you can read the district off, because the
names that remain lie inside the buildings that own them instead of on top of
the ones that do not, and the ones that are gone are a keypress away in a list
rather than dropped in silence.

### Across the window sizes

| window | grid | composition |
|---|---|---|
| 320x180   | 64x25 cells | master/detail holds; plan at 1 px/tile, pans to the cursor; `LEGEND` drops off the tab row; the nav band takes two rows |
| 960x540   | 96x38 cells | master/detail, plan at 3 px/tile, the whole ward, 30 names on its face |
| 1280x720  | 85x34 cells | master/detail, plan at **4** px/tile, 35 names on its face |
| 1920x1080 | 76x30 cells | master/detail, plan at 5 px/tile, 40 names on its face |

Two of those numbers are the composition asking a question rather than assuming
an answer, and both were worth asking. The MAP PANE`S SHARE of the body is the
first: the plan only ever grows in whole pixels per tile, so a pane one cell
short of the next rung is a pane whose extra cells do nothing for the picture
and are worth more to the facts beside it. The composition tries three shares
and takes the one that buys a whole pixel, narrowest on a tie. The NAV BAND`S
HEIGHT is the second: two rows are needed at 320x180, where the four verbs
cannot make four columns and a one-row band clips `M - CLOSE` off the page --
and two rows cost the plan a whole pixel per tile at 1280x720, where they fit on
one and the row is the twenty-one pixels between three px/tile and four. So the
band asks the list how many rows it needs. Neither responds to a player.

**The HUD is untouched.** Nothing in this pass reaches `hud.cpp`, the world
render or the street overlay — the ambient street HUD measures ink 17,709
(3.42%) / claimed 24,831 (4.79%), and the only line the page adds to `HudState`
sits inside the ward map's own branch, so a frame with the map shut is unchanged
by construction. What that branch does add is a STAND-DOWN: the compass ribbon,
the place plate, the clock and the purse are suppressed while the map is up,
because the compass prints the ward's own street name across the top centre and
the clock stack sits top right — exactly where this composition's breadcrumb row
and its right-aligned readout now live.

## The crosshair pass: moving the E prompt, and what it cost

The owner's note, verbatim:

> *"The 'E' button shouldn't have that label text be at the bottom of the
> screen. It needs to be improved to be properly contextual and when shown
> hover a bit to the top-right of the center crosshair."*

`docs/frames/crosshair/before-street-960.png` is what he was looking at: `E
TALK`, centred along the very bottom edge, four hundred pixels away from the
thing it was talking about. What replaced it is two rows hanging off the
reticle:

```
                    OX QUERNSTONE  WATCH        <- what is under the crosshair
                 -+-E - TALK                    <- what the key does to it
```

**This is the only element in the game allowed inside `hudCentreRect`**, and it
is a clamp rather than a promise: `hudAimRect()` names the rectangle and every
pixel of the reticle and both rows is intersected with it before it is drawn.
With no verb to show, `drawHud()` puts nothing there at all — so under a
conversation, the ward map, the casebook, the keys page, options or the pause
menu the middle of the screen is exactly as clear as it was before this pass
existed, and `test_render.cpp` still proves zero trespass with all sixteen
other rows lit at once.

### The numbers, at 960x540

| scene | ink before | ink after | claimed before | claimed after |
|---|---|---|---|---|
| street (`--smoke=30`, a person in reach) | 17,781 (3.43%) | **19,427 (3.75%)** | 24,939 (4.81%) | **37,804 (7.29%)** |

Ink went up by 1,646 pixels — a third of one per cent of the frame — and
claimed by 12,865, which is two and a half. Neither is hidden and both are
decomposed below, because the second one is mostly not what it looks like.

**The ink delta is the element and nothing else.** Measured over the same
`--nohud` control, the old bottom-edge row lit 420 pixels and the new
two-row prompt plus its reticle lights 2,066: a difference of 1,646, which is
the whole-frame figure exactly. Every other row on this HUD is bit-identical.
The 1,646 is the second row — the old element was a verb, the new one names its
object, and `OX QUERNSTONE  WATCH` is twenty glyphs where `E TALK` was six.
There is no version of "name what you are looking at" that costs fewer pixels
than the name.

**The claimed delta is two thirds ruler artefact.** Listing the blobs the ruler
actually boxes:

| blob | before | after |
|---|---|---|
| the aim prompt (reticle + both rows) | — | **6,540** (474,238)-(687,277), in two boxes |
| `E TALK` on the bottom edge | 560 | — |
| the case row | 4,340 | *merged* |
| the health and wind bars | 5,850 | *merged* |
| case row **+** bars, as one blob | — | **17,215** |

Removing the interact row freed a slot in the bottom band, so the case row
dropped into it — and landed within the ruler's own three-pixel vertical
closing distance of the health bar, which merges two boxes that used to be
counted separately into one box with the gap between them inside it. That
accounts for **+6,465** of the +12,865, and it is not one new pixel of
interface: the ink in that corner did not change at all. The prompt's own
claim is **6,540 px, 1.26% of the frame**, against the 560 the old row held.

### The scrim is paid for by the pixel, not by the frame

The first cut drew a flat dark scrim behind both rows so they would survive
being drawn over anything. It measured ink 21,771 (4.20%) and claimed 40,252
(7.76%) — a three-point jump in claimed area, which is precisely the thing
*"I love the vibe of the UI but just be more careful with real estate"* was
about.

But the capture over a noon sea is just as real: the 4x6 font's own one-pixel
drop shadow carries a row over a dark street and does **not** carry a name over
a pale sky. So the scrim reads the ground it is about to sit on and charges for
exactly as much as that ground costs, on a **continuous** ramp — a threshold
would pop the plate on and off as the player turned, which is worse than either
state.

Measured under that exact rectangle in the `--nohud` control:

| ground | luma | scrim |
|---|---|---|
| the Tarwalk under lamps, 20:00 | 0.24 | **none** — the drop shadow carries it |
| the harbour from the rooftops, 12:00 | 0.67 | **full** |

`after-person-960.png` and `after-bright-960.png` are the same element on both
grounds. On the street there is no plate at all and the row still reads; over
the sea there is one, and without it the row does not. That change took 2,344
pixels of ink and 2,448 of claimed area back off the street frame.

The reticle keeps a shadow unconditionally, and that was also a capture and not
a guess: the first noon-sea frame had a warm reticle over pale water that very
nearly disappeared.

### What the pixels bought

| | before | after |
|---|---|---|
| where it is | bottom edge, centre, a slot in the bottom band | **up and right of the reticle** |
| what it says | the verb | **the verb, and what the verb will act on** |
| a person | `E TALK` | `GERTA SALTCOTTE  BARTENDER` / `E - PICKPOCKET` |
| a door | `E LOOK` | `THE WEIGHHOUSE` / `E - LOOK` |
| a lead the case is about | `E LOOK` | `THE BODY, AND WHOEVER FOUND IT` / `E - LOOK`, in its own colour |
| a stranger's box | `E PICK LOCK` | `THE STRONGBOX  ROOM 2  LOCKED` / `E - PICK LOCK` |
| a reticle | none at all | four ticks around an open centre, in the subject's accent |
| slots taken in the bottom band | one | **none** |

The bottom band got a slot back, which is not nothing: the alert, the lock, the
guard, the case, the rival, the rung and the errand all queue for that space and
at 320x180 the band is three rows deep.

### Across the window sizes

Everything is in units of `hudMinorScale` — the register this HUD draws things
you read *deliberately* at, which is what a crosshair prompt is. Drawing it at
`hudScale` would put the loudest type in the game in the middle of the play
space.

| window | unit | reticle | room for the subject row |
|---|---|---|---|
| 320x180 | 1 | 6x6 px, ticks 2x1 | 29 glyphs |
| 960x540 | 2 | 12x12 px, ticks 4x2 | 44 glyphs |
| 1920x1080 | 5 | 30x30 px, ticks 10x5 | 35 glyphs |

The biggest window has the least room, and that is the same inversion the panel
pass documented: `hudMinorScale` steps up with height, so the glyphs grow faster
than the frame does. A name too long for the room is clipped, and the qualifier
beside it is **dropped whole** rather than cut to a stub — `ALREADY R..`
qualifies nothing.

## The casebook pass — the book, and the moment leads open

The complaint this answers is not about pixels at all. The owner followed the
Bloodletter case to Crell at the Weighhouse and it *"seemed to stop there."* It
did not: `weighhouse-ledger` opens three leads and `sim/casebook.cpp` opened all
three, correctly, every time. **`docs/frames/casebook/before-weighhouse-960.png`
is the frame he was looking at when it happened** — and the only thing on it
that says anything about three leads is the bottom-left row reading `CASE 4/9`
in dim grey, which a moment earlier read `CASE 4/6`.

So there are two measurements here, and they pull in opposite directions.

### The street, where the notice fires

| scene, 960x540 | ink | claimed |
|---|---|---|
| street, no notice up (`--smoke=30`) | 19,427 (3.75%) | 37,804 (7.29%) |
| the Weighhouse, the instant the ledger is read, **before** | 31,599 (6.10%) | 83,692 (16.14%) |
| the Weighhouse, the instant the ledger is read, **after** | 41,139 (7.94%) | 93,790 (18.09%) |

**The HUD is bit-identical when the notice is down.** The first row is the
crosshair pass's own settled figure to the pixel — 19,427 and 37,804 — which is
what "this pass adds nothing to the ordinary frame" looks like when it is
measured rather than asserted.

What the notice costs is **+9,540 ink and +10,098 claimed, for three seconds**,
once, on the rising edge of a look that opened something. It cannot repeat by
standing still: `LookResult::opened` counts leads that were *not already known*,
so a lead the trail converges on opens nothing new the second time and fires
nothing. Nothing queues it, and it stands down whole under a page or a
conversation rather than popping when one closes.

It is the same slot the threshold plate uses and it **outranks** it: two
announcements stacked in one band would be two notices fighting.

### The book itself

The ruler's `--nohud` method applies (the Menu is an overlay over a rendered
frame), and it says the number went the wrong way:

| scene, 960x540 | ink | claimed |
|---|---|---|
| the casebook, **before** (the tiled Menu, Journal focused) | 468,819 (90.44%) | 477,360 (92.08%) |
| the casebook, **after** (the composed page) | 510,720 (98.52%) | 510,720 (98.52%) |

Both figures are equal in the "after" row because it is a filled panel — the
same degenerate case the conversation surface and the controls page already
record.

**+6.44 points of claimed area, and the defence is what is in it.** The old
frame gave the casebook the bottom third and spent the other 56% on three tiles
the player did not open the book to read:

| | before | after |
|---|---|---|
| lead rows on screen | 7 of 12, with `0 MORE (1/2)` | **all 12, no page to turn** |
| what the highlighted lead says | its short name, and nothing else | **place, witness, what they are, dateline, bearing, the clue, the paragraph behind it, and every lead it opened, by name** |
| where a lead is | nowhere on the frame | **a bearing and a distance, and `ENTER` puts the ward map's cursor on it** |
| state of a lead | `?` / `X` / `*` in front of the name | **the row's colour, its glyphless value column, the badge in the detail pane, and the verb at the foot of it** |
| the case itself | one line of dread band | **its own view: the hook, the count, the dead ends, the ward's nerve as a bar** |
| screen spent on the casebook | about a third | all of it |

Per unit of information the page is far cheaper than the strip it replaces; per
unit of *screen* it is a full takeover, exactly like every one of the owner's
own reference frames and like the controls page and the ward map before it.

### Where the composition gives way

| window | interior cells | the split (master / detail) | the nav band | the list |
|---|---|---|---|---|
| 320x180 | 62 | **collapses** — the list takes the whole body | two rows | all 12 leads, no detail pane |
| 640x360 | 126 | 63 / 61 | one row | all 12 |
| 960x540 | 94 | 47 / 45 | one row | all 12 |
| 1280x720 | 83 | 41 / 40 | **two rows** | all 12 |
| 1920x1080 | 74 | 37 / 35 | **two rows** | all 12 |

The biggest window is the narrowest in cells — `hudMinorScale` steps up with
height, so 1920x1080 has 74 interior cells where 960x540 has 94 — and both of
the adaptive decisions above are that inversion being paid for. A case pins
every one of them, including that the nav band still names the key that closes
the book after it re-columns, because the one-row band silently dropped that
entry at 1920x1080 and a screenshot is what found it.

---

## The polish pass: the stipple, the ground, and one sign that stopped being cut in half

Everything below was measured at **640x360**, the default render size, and not
sampled at 960x540 and assumed down.

### The street, where the ruler means what it says

There is a world behind this scene, so `--nohud` is a real control and INK and
CLAIMED are the honest numbers:

| scene, 640x360 | ink | claimed |
|---|---|---|
| street, **before** | 7,917 (3.44%) | 13,231 (5.74%) |
| street, **after** | 7,587 (3.29%) | 12,431 (5.40%) |

**Both went down.** The change on this scene removes a label rather than adding
one: the ward's own hanging signage is drawn before the HUD and could not see
it, so on the Tarwalk "THE BILGE" and the crosshair's own "HEMP CAT" row landed
on the same pixels and the prompt won by being second, cutting a street name
through the middle. `SignageSettings::exclusion` hands the sign pass the
prompt's own footprint and the existing nearest-first rule does the rest.

### The pages, where it does not

Four of the five scenes this pass touched are **full-screen surfaces over no
world**, which is this document's own documented degenerate case: the ink mask
against a `--nohud` control is the whole page, CLAIMED reads ~100%, and the
number means nothing. So what is reported here instead is **ink density** — the
fraction of the frame carrying anything above the panel ground — measured on
the shipped binary before and after:

| page, 640x360 | ink density before | after |
|---|---|---|
| creation door | 4.10% | **5.01%** |
| the quiz | 6.53% | **7.45%** |
| controls | 5.92% | **6.71%** |
| casebook | 5.04% | **5.77%** |
| ward map | 45.54% | **45.76%** |

Roughly nine tenths of a point each, and all of it is the stipple.

### What the stipple actually was, and what it is now

Measured inside the casebook's empty lead pane and the quiz's empty answer
pane, at 640x360, on the ground the marks sit on (luma 8):

| | coverage | peak luma | mean lit luma |
|---|---|---|---|
| casebook pane, before | 0.76% | 29 | 29 |
| casebook pane, after | **1.92%** | **60** | **44** |
| quiz pane, before | **0.00%** | — | — |
| quiz pane, after | **1.83%** | **60** | **43** |

The quiz row is the finding. Its master pane had no stipple at all: three
answers spend about eight rows of the block list, so roughly two thirds of the
pane — about 104,000 pixels — was genuinely flat black. Only the *detail* panes
ever carried the field, and the master pane is the one that empties out.

Coverage 2.5x and delta 3.9x, so the signal is about ten times what it was. The
field is now a bit-mixer rather than `(cell*7 + row*13) % 11`, which is linear
and lays its marks on parallel diagonals — invisible at 0.77% and visible
corduroy at the density this needed.

### One ground alpha, not four

0.995 on the ward map, 0.99 in the casebook, 0.98 in creation, 0.97 on the
controls page. Three of the four comments defending those numbers say the same
thing in different words. `kPageGroundAlpha` is 0.995 for all four; the
conversation band keeps 0.86 as `kBandGroundAlpha`, which is a different job and
is named so it reads as a decision.

0.97 was measurably not enough: the controls page's own comment records the
author catching "THE GILDED GULL" through the bindings list and answering it by
moving to 0.97, and frames committed *after* that still have the sign legible
across the middle of the list.

---

## Every CLAIMED figure above is overstated (2026-08-29, ship phase)

`signage.exclusion` — the box the world's street signs step around so they do
not collide with the aim prompt — was computed **unconditionally**, including
under `--nohud`. So the `--nohud` baseline had its signs shoved aside by a
prompt that was never drawn, and every pixel they moved counted as interface in
the diff. Fixed in `bb223cb` (sprint 1, item 5) by gating it on `config_.hud`.

**The `INK` figures are unaffected in kind but the `CLAIMED` figures above were
inflated by the 368–656 px this accounted for.** Do not re-derive anything from
them; re-measure instead.

### The first correct measurement, at the default render size

640x360, the size the game actually opens at and the size every frame in this
document should have been judged at:

```
dist\granadad.exe --smoke=30 --width=640 --height=360 --scale=1 --screenshot=DIR\after-street.png
dist\granadad.exe --smoke=30 --width=640 --height=360 --scale=1 --nohud --screenshot=DIR\nohud-street.png
python scripts\hud-real-estate.py DIR 10 2
```

(10 and 2, not 15 and 3: the closing gaps are one glyph advance and one shadow
row, and they scale with the capture.)

| scene | INK | CLAIMED |
|---|---|---|
| street (`--smoke=30`), 640x360, revision `581d8fe` | 8,072 (**3.50%**) | 13,971 (**6.06%**) |

Frames: `docs/frames/ship/ship-ruler-after-street-640.png` and
`docs/frames/ship/ship-ruler-nohud-street-640.png`.

### The ruler is degenerate over a full-screen page

A creation step or the casebook covers the world entirely, so the diff returns
the whole frame and says nothing. For those, measure **ink density of content
inside the composed panel**, and — since sprint 1 taught the pages to stop at
their content — **how much of the frame is left dead beneath it**, which is now
the number that decides whether a page looks placed or looks like it ran out:

| surface at 640x360 | panel | dead below | ink in panel |
|---|---|---|---|
| creation — THE DOOR | 164px (45.6%) | 194px (53.9%) | 8.06% |
| creation — YOUR PAST | 185px (51.4%) | 173px (48.1%) | 8.61% |
| creation — THE SHEET | 164px (45.6%) | 194px (53.9%) | 9.55% |
| creation — THE NAME (on-screen keyboard) | 129px (35.8%) | 229px (63.6%) | 7.72% |
| casebook, new game | 199px (55.3%) | 159px (44.2%) | 6.80% |
| the ward map | 360px (100%) | 0px | 23.67% |

The map row is the benchmark: same vocabulary, same size, fills its frame at
nearly three times the ink of any page above it.

## The placement pass: where the composed page SITS

Sprint 1 of the last program taught these pages to STOP at their content and
never told them where to START, so every one of them hung off the top edge with
two pixels of margin above it. The ship note's own words for it were that THE
DOOR "reads as a panel that *ran out*, not one that was *placed*."

`panelSeatY()` in `render/panel.hpp` is now the one place that decides, read by
`creation_page.cpp` and `casebook_page.cpp` alike. It is **not** a true half:
`kPanelSeatAbove` is 45, so the spare goes 45 above and 55 below.

**That number came off frames, not off arithmetic.** The ground outside these
panels is flat, so the same captured panel re-seated at another ratio is an
exact mock of what the binary would draw, and all three were looked at at
640x360:

| split | above / below on THE DOOR | how it reads |
|---|---|---|
| 50 / 50 | 97 / 97 | sits visibly LOW — the optical centre of a box of type is above its geometric one |
| **45 / 55** | **87 / 109** | **placed** |
| 40 / 60 | 77 / 117 | top-heavy; the foot floats and the gap under it reads as a page that stopped |

Measured on the shipped binary, at 640x360, above/below in pixels:

| surface at 640x360 | panel | above | below | ink in panel |
|---|---|---|---|---|
| creation — THE DOOR | 164px (45.6%) | **87px** | **109px (30.3%)** | 8.85% |
| creation — THE CALLING | 192px (53.3%) | 74px | 94px (26.1%) | 9.19% |
| creation — the quiz | 178px (49.4%) | 81px | 101px (28.1%) | 13.69% |
| creation — THE VERDICT | 164px (45.6%) | 87px | 109px (30.3%) | 9.18% |
| creation — YOUR PAST | 185px (51.4%) | 77px | 98px (27.2%) | 10.72% |
| creation — the review | 164px (45.6%) | 87px | 109px (30.3%) | 10.04% |
| creation — THE SHEET | 164px (45.6%) | 87px | 109px (30.3%) | 10.94% |
| creation — GABRI's sheet | 136px (37.8%) | 100px | 124px (34.4%) | 10.16% |
| creation — THE NAME (on-screen keyboard) | 129px (35.8%) | 103px | 128px (35.6%) | 8.13% |
| casebook, new game | 199px (55.3%) | 71px | 90px (25.0%) | 10.20% |
| the ward map | 360px (100%) | 0px | 0px | 44.17% |
| the controls page | 360px (100%) | 0px | 0px | 6.97% |

**The pages that already fill the frame are untouched, and by construction
rather than by a special case**: the map and the controls page compose at full
height, so their spare is the two or three pixels that do not divide into whole
cells and `panelSeatY()` returns exactly what the old expression did. Both are
byte-identical across the change.

**Read the "below" column against the old one.** Nothing here got smaller — the
panels are the same heights they were. What moved is that the dead space is now
*margin on both sides* instead of *a hole underneath*, and that is the whole
difference between a window and a page that ran out.

## The keyboard's own grid

`THE NAME` was the worst-composed screen in the build and it is on the critical
path for every controller player. The grid was thirty rows of an option list
capped at six columns, so it inherited `planOptionList`'s **spread** rule — the
one that makes the last column of a pane end at its right edge, which is right
for four callings and wrong for thirty letters.

| at 640x360 | before | after |
|---|---|---|
| grid | 6 columns x 5 rows | **10 x 3** |
| column stride | **10 cells (50px)**, against 7px rows | **2 cells (10px)** |
| the block | 195 x 21 px | **95 x 21 px** |
| the master pane's share | 42% of the body | **28%** |
| what it reads as | five sparse vertical strings | a keyboard block |

`render/panel.hpp` gains a **key grid** beside the option list and the block
list — same three-function shape (`planKeyGrid` / `drawKeyGrid` / `keyGridAt`),
so the mouse stays the inverse of the drawing rather than a second description
of it. The one rule that differs is the advance: **the widest a key can afford,
not the pane divided by the columns.**

Padding is zero, and that was measured too: a padded cap (` A `) doubles the
advance on a one-glyph grid and puts the columns twenty pixels apart against
seven-pixel rows, which is the same defect one notch milder. Frames of both said
so.

**The frame did not get shorter and the ship note guessed that it would.** The
NAME page's height is set by its DETAIL pane — badge, two facts, a paragraph and
the commit verb, about nine rows — not by the grid, so dropping the grid from
five rows to three bought nothing in height. The panel is 129px before and
after. What the tight grid bought is the master pane's width (42% -> 28%) and a
block that reads as a keyboard.

**320x180 is the one window that pays.** `splitMasterDetail` floors the master
pane at eighteen cells and ten keys with a gap between them want nineteen, so
the smallest window the game runs at spends the gap and sets the alphabet
shoulder to shoulder. That is the honest answer there: the alternative is
drawing fewer columns than the cursor walks, and then the inverted fill names a
different letter than the detail pane does.

### The frames

`docs/frames/placement/`, all at 640x360 with `--scale=1` except where the name
says otherwise:

```
before-door-640.png      after-door-640.png
before-name-640.png      after-name-640.png
before-casebook-640.png  after-casebook-640.png
seat-50-50-door-640.png  seat-40-60-door-640.png   the two seats not taken
after-name-320.png       after-name-1920.png       the grid at both ends
pad-spelling-640.png     pad-named-begin-640.png   a virtual pad, mid-name and done
```

The two pad frames are a real `SDL_AttachVirtualJoystick` through
`--padcreation`, not a mock: `pad-spelling-640.png` is `ABC` being taken off the
new grid one D-pad move and one A press per letter with the detail pane naming
`THE LETTER C`, and `pad-named-begin-640.png` is the sheet afterwards with
`NAME ABC` and `BEGIN` gone `READY`. The run that follows them reaches
`granadad: playing as ABC (custom)`.
