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
