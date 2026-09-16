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
| `kit-equip-960x540.png` | `--kit=equip` | ENTER on the coat, ENTER on the knife. `ON THE BACK  COAT  DR 2`, `KNIFE  8DR  4C  IN HAND`, `COAT  60DR  12C  WORN`. `IN HAND  KNIFE 11-13 EDGE` on page one. | Equip on a row, the doll changes | Same press. The consequence is the row's own mark and the DR beside the slot, not a picture. |
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
