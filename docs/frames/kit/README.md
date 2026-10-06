# The Kit, photographed

Every frame here is `dist\granadad.exe` off the gated Windows build playing the Kit through the
real verbs. No harness, nothing staged. The `--kit=WHERE` line is `Session::runKitLine`, ten
beats, every ending a stop on the way, and each run prints its own summary beside the frame
(`kit beats=n/N take=... theirs=... load=238/240 legs=131/256`). The exact line that shot each
frame is the one in the table, run by `scripts\shoot-kit.ps1`. A frame that fell short cannot
pass as one that did, the script counts them.

Eli, 2026-09-11, through the wizard. THE KIT IS A GO, per D10.

Reshoot the whole set with `powershell -ExecutionPolicy Bypass -File .\scripts\shoot-kit.ps1`.
Every line below is `granadad.exe --smoke=0 --hold --width=960 --height=540 --scale=1 <the line>
--screenshot=docs\frames\kit\<frame>` (the drop is 1280x720). The script prints each run's summary
and writes the same to `shoot-kit.log` beside the frames.

## Fidelity, frame by frame

Anchor. Oblivion's inventory in this game's own idiom. One press takes, one press wears, one
number is the budget, the hand is mirrored on the HUD, and the world draws the thing you dropped.

| frame | line | what it shows | Oblivion beat | how close |
| --- | --- | --- | --- | --- |
| `kit-take-960x540.png` | `--kit=take --settle-steps=0` | Two in the afternoon on the Tarwalk. The coil of rope on the boards at (151,63), nobody's. The crosshair reads `ROPE  48DR` over `E - TAKE` in the Thing accent. The press takes it and the row says `TAKEN - ROPE. 48 DRAMS. 53/240.` | Take on the thing, the weight beside it | Same shape. The name and the number before the press, one press, the running budget on the row. |
| `kit-theirs-960x540.png` | `--kit=theirs --settle-steps=0` | Four in the morning in the shut Gull, from Father Maell's empty chair. The lantern on Hobbin's table, the house's own. `LANTERN  THEIRS` in the Owned accent (the warm red nothing else on the frame wears) over `E - TAKE`. The press is a lift under the witness rule. Nobody saw. | The red hand, before you press | Same shape. THEIRS is a word where Oblivion has a colour, and the accent carries the colour too. |
| `kit-sheet-960x540.png` | `--kit=sheet` | The Character tile on the carried rows. `IN HAND  FISTS 3-5 IMPACT` ends page one. `ON THE BACK  NOTHING`, `ON THE HEAD`, `ON THE FEET`, `AT THE BELT`, `LOAD  151 / 240 DRAMS`, then `KNIFE  8DR  4C`, `COAT  60DR  12C`, `LANTERN  30DR  9C`, `ROPE  48DR  6C`, `5 PICKS  5DR  10C`. The epithet is the verbs in the keyboard's own words. | The Inventory tab, weight and value per row, one budget | Same numbers, words instead of icons. No doll, by ruling. The equipment block is worded rows and only the slots the raws fill. |
| `kit-equip-960x540.png` | `--kit=equip` | ENTER on the coat, ENTER on the knife. `ON THE BACK  COAT  DR 2`, `KNIFE  8DR  4C  HELD`, `COAT  60DR  12C  WORN`. `IN HAND  KNIFE 11-13 EDGE` on page one. | Equip on a row, the doll changes | Same press. The consequence is the row's own mark and the DR beside the slot, not a picture. |
| `kit-slot-960x540.png` | `--kit=slot --settle-steps=0` | The knife walked onto slot 3 with RIGHT, the 3 key pressed. The strip at the foot, `KNIFE` in the third cell, the cell inverted, `SLOT 3 - KNIFE.` on the row. The hands row reads `KNIFE UP` when the hands come up. | The hotkey wheel readies a weapon without the menu | Same shape. A slot is a spell or an item, the number row and the D-pad step read either. |
| `kit-dr-960x540.png` | `--kit=dr --settle-steps=12` | The clock at two. Fists on Ox Gullbane at the door, the coat on. His blow lands and `COAT TURNS 2` comes up in the centre stack on the step it did, its own row in the blocked wash's steel ink, held a plate's length. The body lost the rest. Ox's warning keeps the alert row, the house minds you. | Armor rating softening a hit | Flat DR per worn piece, floored at one, said once per blow. Coverage and wear are the v2 ceiling. |
| `kit-search-960x540.png` | `--kit=search` | The knife back in hand off slot 3, Ox put down for good. Over him the crosshair reads `SEARCH  OX GULLBANE  DEAD`. The press opens his kit on the list widget, `1 CUDGEL  40DR  6C`, `2 BOOTS  30DR  8C`, `3 TAKE ALL`. | Search on a body, the transfer list, Take All | Same list, one column. What he carried is his role's authored kit, the roster only. The street's people carry nothing, by ruling. |
| `kit-load-960x540.png` | `--kit=load` | Everything off him, the bottle and the hood off the tables. `LOAD  238 / 240 DRAMS` on the tile. The summary reads `legs=131/256`, half pace, and the next take would refuse with the numbers on the row. | Encumbrance, the slowdown, the hard stop | Same budget shape. 256 to half, down to 128 at the budget, TAKE refuses past it. |
| `kit-drop-1280x720.png` | `--kit=drop --settle-steps=0` | Out on the Tarwalk in daylight. The coil put down through the tile's own X, `DROPPED - ROPE.` on the row, the body a tile back facing it. The coil is the Synty rope knot on the boards, the crosshair names it again. | Drop, and the thing is in the world | Same shape. The renderer draws the room's own list and never removes a thing, TAKE does. |
| `kit-full-960x540.png` | `--kit` | The whole line, the tile again on what is left. | | The default ending. |

## The item table

`content/raws/items/items.json`. Weight in drams, worth in Royals. The base sheet carries 240 drams,
the sack's own number. A hand item's span is its class's, checked at load.

| id | name | drams | royals | slot | class | span | DR | heat | where |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cudgel | CUDGEL | 40 | 6 | hand | blunt | 7-9 IMPACT | | | under the bar at Gerta's feet, the house's; a bouncer carries one |
| the_evictor | THE EVICTOR | 44 | 30 | hand | evictor | 7-9 IMPACT | | | the eviction case's reward, through the Kit now |
| mace | MACE | 48 | 22 | hand | blunt | 7-9 IMPACT | | | Watchman Cull carries one |
| bottle | BOTTLE | 5 | 1 | hand | improvised | 5-7 IMPACT | | | Wick's table, the house's |
| knife | KNIFE | 8 | 4 | hand | edged | 11-13 EDGE | | | Edda's chair, the house's; every patron and the bartender carry one |
| dagger | DAGGER | 12 | 14 | hand | edged | 11-13 EDGE | | | Finch carries one |
| cutlass | CUTLASS | 36 | 40 | hand | edged | 11-13 EDGE | | | Captain Ivo Wake carries one |
| boat_hook | BOAT-HOOK | 30 | 5 | hand | edged | 11-13 EDGE | | | a row for the quay's own tool, no stand yet |
| coat | COAT | 60 | 12 | body | | | 2 | | Colm's chair, the house's; the captain wears one |
| hood | HOOD | 12 | 3 | head | | | 1 | | Bram's chair, the house's; the priest wears one |
| boots | BOOTS | 30 | 8 | feet | | | 1 | | drying by a bollard on the quay, nobody's; a bouncer wears them |
| purse | PURSE | 2 | 2 | trinket | | | | | on most of the roster |
| lantern | LANTERN | 30 | 9 | | | | | | Hobbin's table, the house's; Venn carries one |
| rope | ROPE | 48 | 6 | | | | | | the coil on the Tarwalk, nobody's |
| picks | PICKS | 1 | 2 | | | | | | the picks counter, five to start, Finch sells them |
| letter | LETTER | 1 | 0 | | | | | | Father Maell carries one |
| bale | BALE | its contents | | | | | | | the snug, the ledger's own bale |
| strongbox | STRONGBOX | 400 | | | | | | | the four bed feet; fixed, it does not travel |
| scalp | SCALP | 2 | 4 | | | | | 0 | the sack's row |
| dust | DUST | 3 | 16 | | | | | 9 | the sack's row |
| moonshine | QUAYFIRE | 24 | 7 | | | | | 4 | the sack's row |
| flower | FLOWER | 8 | 10 | | | | | 6 | the sack's row |
| artifact | PIECE | 10 | 24 | | | | | 12 | the sack's row |

## What the world draws

`content/raws/world3d/docks-pieces.json`, the `items` table. One Synty static per item id, a point
up weapon pitched flat on the boards. Weapons are the real `SM_Wep_*` files. A coat, a hood, a
pair of boots and a purse are sacks in three sizes and the cloth's own tints, the pack has no
folded clothes and this build ships no new art. The strongbox is the Generic chest at the four
bed feet, the bale is the sack stack in the snug while a bale stands. No catalogue, no file, no
draw, and the sim never notices.

## The prompts the controls lane must re-key

The Kit spent no new key. Three presses on the Character tile over a carried row ride existing
Menu verbs. ENTER wears, LEFT and RIGHT walk the quick slot, X drops. On a pad the X is the Attack
binding wearing the tile's clothes, the same way PageNext wears the haggle's. If the pad's X leaves
Attack, re-key the tile's `X DROP` epithet and the `action == Action::Attack` branch in
`route_menu_key`, both marked KIT BUILD.

## Not here, said plainly

Attributes deriving from skills. D10's last clause. Every runtime reader of MGT, AGI, VIG and WIT
and every pinned pool, gait and damage number moves with it, so it is its own gated pass. The
budget already reads the effective sheet and follows the day it lands. The Watch does not search
the Kit for a stolen coat, heat rides the sack as before. A stolen thing dropped becomes nobody's.
Corpse kits are fixed per role.

## The kitfix, 2026-09-25

The critic's line: 5/10. Four findings off B01/B03/B05/B06 -- the kit sheet, a bare slot press, the
`--quickbar` shutter, the same sheet at 2560x1440.

1. "Numeric columns are ragged: in B06 the DR values start at four different x, the C values at
   four more." KNIFE's `8DR` sat three cells left of LANTERN's `30DR` because each row's number
   followed straight off ITS OWN name and a name is not a column.
2. "Higher resolution shows LESS text ... the epithet row butts the divider with no gutter ...
   'MORE (3/3)' overhangs the tile border."
3. "The quick bar fails its own claim: B05 (`--quickbar`) has no strip at all; at B03 (`--kit
   slot`) it is there but the viewmodel blade crosses it and the unselected digits are near-
   invisible grey."

What moved.

- **The DR/C columns.** `characterRows()` in `native/src/render/session.cpp` now reads the carried
  list twice: once to learn the widest name, the widest DR and the widest C this page's OWN rows
  will print, once to compose each line against those three widths through one helper,
  `rightAlignedDigits()`. Every row's `DR` and `C` end under the last one now, whatever the name in
  front of them runs. `test_character.cpp` pins the padded strings for a five-item kit and, in its
  own case, reads the fix off three differently-named rows (COAT, LANTERN, ROPE) rather than off a
  screenshot.
- **The tile border.** Two bugs in `native/src/render/menu_view.cpp`'s `drawTile()`: the epithet
  row (row 1 of an unfocused tile -- THE GILDED GULL under THE CHART) clipped to the bare pane
  width with no cell of air before the divider, the one row on that pane that did not leave one.
  And the paginated list's own `MORE (n/n)` foot was the one row on the whole surface drawn with a
  raw `drawCellText`, no `clipLabel` -- every OTHER row on the tile goes through it. Both now leave
  or take the same one-cell gutter the rest of the vocabulary already keeps.
- **The `--quickbar` shutter.** `Session::showQuickBar()` used to snap the strip to fully open only
  from a dead stop (`quickBarAnim_.value() <= 0.0F`) -- right for the very first press of a fresh
  session, wrong for `SmokeRunConfig::quickbar`'s own script, which opens the Grimoire, walks a
  crafting onto a slot, CLOSES THE PAGE, then presses the number. Closing the Grimoire mid-sequence
  stands the strip down first; re-arming it a moment later hit `setTarget(true)`'s "resume from
  wherever it had got to" rule, which is correct for a live player and wrong for a shutter with no
  frames left to resume across. It snaps every time now -- a no-op if the strip was already open,
  the fix if it was not. `session.hpp` gains `quickBarFadeValue()` beside `quickBarWanted()` so a
  case can read the drawn alpha directly instead of inferring it from a screenshot; `test_hud_diet.cpp`
  pins a session mid-fall at 0.5, replays the shutter's own four calls with zero `step()`s between
  them, and checks it lands on exactly 1.0.
- **The digits and the strip's own y.** `hud.cpp`'s quick bar strip: unselected digits took
  `kPlateBone` at a bare 0.35 alpha for an unloaded slot, which is the aim reticle's own solved
  problem (`kAimNote`'s header) reinvented dimmer. They read at `kAimNote`'s ink now, with a 0.75
  floor -- the equipped cell keeps its inverted knockout, which is the one digit meant to read as
  SELECTED. And the strip buys one row of air before its own `take()`, always, not only when an
  earlier row happened to pad it there -- with nothing else on the band (the common case a number
  press interrupts) the strip used to land on the very first slot, right above the health bar,
  which is exactly the footprint the drawn hand and its blade rest in.

What did not move, and why. Finding 2's other half -- "the chart truncates to 'MISSION OF THE.' at
2560 vs 'MISSION OF THE FLA.' at 1600" -- is real and the cause is found: `panelMetric()` sizes
every terminal-register page off `hudMinorScale(height)` alone, and that function's cell width
grows a little FASTER than linearly with height (the flat `-1` in `hudScale(height) - 1` matters
less as height grows), so at a fixed aspect ratio the number of cells across a page falls as the
window grows, asymptotically, forever. It is not new and it is not a rounding slip: this file's own
`kit-sheet-960x540.png` row is captured at 960x540 rather than 1920x1080 for exactly this reason
(see `casebook_page.cpp`'s own comment on `kMasterShare`, "hudMinorScale steps up with height"), and
the same arithmetic already gives 640x360 MORE cells across than 960x540 -- 128 against 96 -- two of
the five sizes this whole build is captured and pinned at. A fix that holds cells-across
non-decreasing as the window grows has to change `hudScale()`/`hudMinorScale()`/`panelMetric()`
themselves, which every composed page in the game sizes itself off, and there is no toolchain in
this worktree to rebuild and run the suite that arithmetic is pinned against. Wrong here is worse
than slow, so this pass leaves it named rather than guessed at: a lane with a build and the room to
walk every capture size belongs on it next. (It did, the same day. See THE CELLS below.)

Reviewer command lines, off `dist\granadad.exe` on the gated build, each with
`--music-off --scale=1 --hold --smoke=0` (as first written every one carried `--settle-steps=0`
and the strip's went `--quickbar` bare; THE CELLS below says why neither photographs what was
asked for):

```
dist\granadad.exe --smoke=0 --hold --width=1280 --height=720 --scale=1 --music-off --kit --screenshot=docs\frames\kit\kitfix-kit-1280x720.png
dist\granadad.exe --smoke=0 --hold --width=1600 --height=900 --scale=1 --music-off --kit --screenshot=docs\frames\kit\kitfix-kit-1600x900.png
dist\granadad.exe --smoke=0 --hold --width=2560 --height=1440 --scale=1 --music-off --kit --screenshot=docs\frames\kit\kitfix-kit-2560x1440.png
dist\granadad.exe --smoke=0 --hold --width=1280 --height=720 --scale=1 --music-off --character --screenshot=docs\frames\kit\kitfix-character-1280x720.png
dist\granadad.exe --smoke=0 --hold --width=1600 --height=900 --scale=1 --music-off --character --screenshot=docs\frames\kit\kitfix-character-1600x900.png
dist\granadad.exe --smoke=0 --hold --width=2560 --height=1440 --scale=1 --music-off --character --screenshot=docs\frames\kit\kitfix-character-2560x1440.png
dist\granadad.exe --smoke=0 --hold --width=1280 --height=720 --scale=1 --music-off --flame --quickbar --settle-steps=12 --screenshot=docs\frames\kit\kitfix-quickbar-1280x720.png
dist\granadad.exe --smoke=0 --hold --width=1600 --height=900 --scale=1 --music-off --flame --quickbar --settle-steps=12 --screenshot=docs\frames\kit\kitfix-quickbar-1600x900.png
dist\granadad.exe --smoke=0 --hold --width=2560 --height=1440 --scale=1 --music-off --flame --quickbar --settle-steps=12 --screenshot=docs\frames\kit\kitfix-quickbar-2560x1440.png
```

Shot on 2026-09-26 off the cells pass's gated exe (THE CELLS, below). What to look for: DR/C sharing an end column on the kit sheet at all three widths, the chart
tile's epithet clear of the divider, `MORE (n/n)` never past its own tile, and the quickbar strip up
and snapped (not a sliver) the instant the `--quickbar` shutter fires.

## The cells, 2026-09-26

The kitfix's own unmoved finding: `MISSION OF THE.` at 2560x1440 where 1600x900 fits `MISSION OF
THE FLA.` and 1280x720 fits the whole name. Named that pass, fixed this one.

`hudMinorScale` was `hudScale(height) - 1`, and `panelMetric()` sizes every terminal-register page
off it. The flat `-1` made the cell grow a little faster than the frame, so at a fixed aspect the
cells across a page FELL as the window grew: 640x360 dealt 128, 960x540 96, 1280x720 85, 1600x900
80, 1920x1080 76, 2560x1440 73. The register is `hudMinorScale(width, height)` now, the smaller of
`height / 240` and `width / 425` with no offset (`kMinorScaleDivisor` and `kMinorScaleWidthDivisor`
in `native/src/render/hud.cpp`; 425 is five units a cell times 85). Floored means the scale is never
more than `width / 425`, so any window at least 425 px wide deals at least 85 cells across, which is
1280x720's own 85, the size the register was judged at, at ANY aspect. The first cut of this pass
keyed the register off the height alone, and both critics caught 16:10 at once: 1920x1200 dealt the
old 76 and the chart read `MISSION OF THE.` again, on the aspect the owner's own display is. With
both axes it deals 96 there, 112 at 1680x1050, 85 at 2560x1600 and at 1707x1067, 102 at 4:3
1024x768, 128 at 21:9 2560x1080. At 16:9 the height term binds, so the four smaller capture sizes
did not move (scales 1, 1, 2, 3) and every pin at them stands; 1920x1080 goes from scale 5 to 4.
With an integer glyph scale the count is a sawtooth, not a climb -- 1280x720 85, 1600x900 106,
1920x1080 96, 2560x1440 85 -- and that is the honest shape of the fix: a bigger window never shows
less than the size the register was judged at. Two pairs collapse onto one grid. 1920x1080 at scale
4 is 960x540's 96x38 grid at twice the pixel, and 2560x1440 at scale 6 is 1280x720's 85x34 the same
way, so a page that composes at the smaller one composes identically at the larger.

What moved, what did not.

- 320x180, 640x360, 960x540 and 1280x720 did not move. Their scales are what they were, so every pin
  at those four sizes stands as written.
- `hudMinorScale`, `panelMetric` and `hudTopRightReserve` take the width now and every caller hands
  it over: the pages had it already, the HUD reads `target.width()`.
- 1920x1080 goes from scale 5 to 4. `test_creation.cpp` pinned two things off the old premise that
  1920 was the narrowest window in cells (76 against a 77-cell measure). The NAME card composes its
  77 cells at 1920 now exactly as it does at 960 and 1280, seated by the one rule, and the
  clamp-at-the-window proof moved to 320x180, the one capture size actually narrower than any
  measure. Everything else pinned at 1920 was written as a relative check and rides the new numbers.
- `hudScale` keeps its 180 and its one argument. The glance register is not a page and has no cells
  across to defend, so from 900 tall up the two registers are no longer one step apart (1080 tall is
  6 and 4). The doc on `hudMinorScale` in `hud.hpp` says so.
- `kPanelNarrowestFullInteriorCells` stays at 74. Every held-height floor was judged against it, and
  no window at least 425 px wide deals under 83 inside the border now, so from 1280x720 up it is a
  floor with headroom at any aspect. Loosening it would re-judge seven steps and two views for
  nothing. The comments that said "the biggest window is the narrowest in cells" (`panel.hpp`,
  `casebook_page.cpp`, `creation_page.cpp`, `keys_page.cpp`, `map_view.cpp`, two cases,
  `docs/HUD-REAL-ESTATE.md`) say what was true and what is now; the two HUD-REAL-ESTATE table rows
  for 1920x1080 were re-measured off the exe rather than asserted (both show the 960x540 page at
  twice the pixel, the keys table paging at both sizes now).
- `test_panel.cpp` gains the contract: the seven capture sizes' scales and cells, the 85-cell floor
  at every 16:9 height from 720 up to 2160 and at 16:10, 4:3, 5:4 and 21:9 sizes besides, and
  1600x900 joins the composition sizes as the one grid, 106x42, no other capture shares.
- Everything drawn at the minor register moves with it, not only the composed pages. The world's
  signage (`signage_renderer.cpp`), the place name, the purse, standing, heat, the sack, the stealth
  line and the rest of the reading rows go one step smaller at 1600x900, 1920x1080 and 2560x1440,
  and the glance register (compass, hour, health, a shout) does not, so the two are two and three
  steps apart there where they were one. The critics read them as one HUD still.

The quick bar strip, the critic's other line ("`1234567890` runs as one number"). Each slot is its
own ruled cell now, a rule between cells at the plate's own border weight and a closing rule after
the tenth, the digit near two units of air off either rule. A loaded slot is filled with bone at
0.30 and its digit is the plate's bone at full strength. An empty slot is an unfilled cell with the
note ink at 0.60. The equipped slot keeps its inverted knockout, and the selected slot a gold frame
at the rule's own weight over a faint gold tint. The plate is at 0.92 now, opaque enough that the
world behind it no longer decides a cell's colour, and the cell box holds the digit's drop-shadow
row so nothing hangs under the plate. Four states, four shapes, before a digit is read.

The strip and the band. The first reshoot off this pass's own gated exe showed NO STRIP at 960x540,
1280x720 and 2560x1440 with `--flame --quickbar --settle-steps=12`, and the auditor found why. The
voice lane's street line (Father Maell's farewell, in that drive) was taken off the bottom band
ahead of the strip, and the row of air the kitfix bought for the strip, always, then had no slot
left above the play-space floor: a four-slot band, two for the alert and one for the line, refused
the padded take, and the feature under test was not drawn. In `hud.cpp`'s `drawBottomBand` the strip
is taken ahead of the voice line now (hand-on feedback outranks a bark it can share the band with),
the air is bought only when the band is empty, the one case it exists for, and given back when the
take is refused (`BottomBand::taken()`, `BottomBand::unpad()`); a plated row's take carries its plate's
padding as clearance, so a plate never crosses the floor and never sits on a line's first glyph row
(the pass first clamped the plate at the floor, and at 320x180 the player critic read `FAIHER HAELL`
off the rail: the smoke size's three slots cannot seat the alert, the strip and the street's line
whole, so the line is dropped there, never drawn under a rail); and the strip and the voice line buy
the half unit their plates stand above their glyphs back for the row above, the way the alert does,
but only where a further slot still fits under it (`BottomBand::padIfAfforded`).
The wheel's tutor
toast rides the strip's own plate now, after the name in the reference ink, and costs the band no
slot: as a fourth tier of bare grey text it was squeezed between the voice line and the strip and
unreadable under the drawn fist. `test_hud_diet.cpp` lights the alert, the voice line, the strip and
the hint together at seven sizes and pins that the strip and the hint drew, drew only in the band,
and evicted neither the speech nor the alert, and that emptying, un-equipping and un-selecting a
cell each change the strip.

Three things the critics read off the strip that were not the strip's. One mark for "in your hand":
the inverted cell followed casts only (`quickEquipped` off `equippedSpell()`), so the knife pressed
onto slot 3 got the gold frame and nothing more; an item slot whose item is the one held inverts the
same way now, off `heldItem()`. The echo: `SLOT 3 - CLEAR THE HEAD.` was shouted on the alert row
at the glance register directly under a strip that already names the slot, and at 1600x900 the
shout was near twice the strip's size -- a loaded slot's press is quiet on the alert row now, the
strip is its answer, and the empty slot and the thing no longer carried still say so
(`Session::selectQuickSlot`; `test_character.cpp` and `test_grimoire.cpp` pin the quiet). And the
carried rows' state word: at the floor of 85 cells the Character tile is 27 wide and `IN HAND` was
the word the row lost, `KNIFE   8DR   4C  IN..` on every common monitor, so the carried rows say
`HELD` and `WORN`, a pair (the sheet's own `IN HAND  KNIFE 11-13 EDGE` row is unchanged). That alone
was not enough: the `SLOT 3` tag rides the same row, and the tile's list still cut inside the word
(`HEL..`, all three critics). So the option list's labels lose whole columns now before they lose a
letter (`clipColumns` in `panel.cpp`, pinned in `test_panel.cpp`): at the floor the row reads
`KNIFE    8DR   4C  HELD`, the tag gone whole, and the strip says which slot; from 106 cells up the
tag is back.

Reviewer lines and frames. The nine lines above are shot now and corrected: `--settle-steps=0`
photographs the Menu on its first ease frame (a ghost of the page over the street at 2560x1440) and
catches the raised viewmodel shattering across the strip frame, and `--quickbar` without `--flame`
lands none of its beats. Pages take the default settle; the strip takes the controls README's own
line. `kit-slot-960x540.png`, `controls/quick-bar-kb-960x540.png` and `pull/ribbon-follow-1920.png`
are reshot off the same exe, the kit set through `shoot-kit.ps1`. What the frames show is under THE
FRAMES below.

## The frames, 2026-09-26

All off `dist\granadad.exe` at the gate stamped 2026-09-26T22:38Z (1401 cases, 102/102), the lines
as corrected above.

| frame | line | what it shows |
| --- | --- | --- |
| `kitfix-kit-1280x720.png` | `--kit` | The tiled Menu at 85 cells. `- MISSION OF THE FLAME  +2` whole on THE CHART, `4 KNIFE    8DR   4C  HELD` whole on the carried rows (the `SLOT 3` column dropped whole at 27 cells; the strip says which slot), `5 COAT    60DR  12C  WORN` under it. |
| `kitfix-kit-1600x900.png` | `--kit` | 106 cells, the widest grid the game composes: the same rows with air to spare. |
| `kitfix-kit-2560x1440.png` | `--kit` | 1280x720's 85x34 grid at twice the pixel. The case name whole where the first critic's frame read `MISSION OF THE.`, the DR and C columns under one end, the rules on the grid. |
| `kitfix-character-*.png` | `--character` | The Character tile's first page at the three widths: the ward rows, the skills, the four attributes, the guild joins, all whole. |
| `kitfix-quickbar-1280x720.png` | `--flame --quickbar --settle-steps=12` | The strip on one plate -- ten ruled cells, the third inverted for CLEAR THE HEAD, `CLEAR THE HEAD` beside the digits, `WHEEL - STEP` after it in the reference ink -- and the alert row QUIET under it: the drive waits the Grimoire's own binding line out before the press now, the way play does, so the frame shows what a number press says, which is nothing on the alert row. The half unit of air the plate stands above its glyphs is bought for the row above it. |
| `kitfix-quickbar-1600x900.png`, `-2560x1440.png` | the same | The same rows at scales 3 and 6; the four-slot band at 1440 holds the alert's two, the strip's one and the line's one. |
| `kit-slot-960x540.png` | `--kit=slot --settle-steps=0` | The knife on slot 3 and pressed: the third cell inverted now, `KNIFE` beside the digits, `WHEEL - STEP` on the same plate, `KNIFE -- SLOT 3.` (the Kit's binding line) on the row, the blade up. |
| `controls/quick-bar-kb-960x540.png` | `--flame --quickbar --settle-steps=12` | The controls README's own frame, reshot: the strip as above at 960x540. |
| `pull/ribbon-follow-1920.png` | the pull README's first drive at 1920x1080 | The pull ribbon's other pin, at scale 4 now (960x540's at twice the pixel). |

Not in the set, by scope: a real drive that binds two craftings (so a loaded, unselected cell is pinned
by `test_hud_diet.cpp` rather than photographed), the keys page's rebind hint wrapping backwards at
1280x720 and up (older than this pass), and the Character tile's row numbers reading like quick-slot
numbers on the carried rows.
