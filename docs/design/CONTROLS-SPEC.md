# CONTROLS-SPEC. Nine and the sticks

**Status.** BUILT on `lane/controls`. This is the canon for what a key does. `native/include/granadad/render/controls.hpp` is the table, `native/src/client/main.cpp` is the event bridge, and every prompt on screen reads the table through `promptLabel` and the grammar functions below. If this file and the table ever disagree, the table is the bug.

**Ruling (Eli, 2026-09-10, verbatim, binding).** *"I want you to work on simplifying the controls and minimizing the number of inputs necessary. Even a game like Morrowind worked on the console with just a few buttons. When I press LMB I want to enter fighting mode and hit whoever is in front of me. I'd expect the watch and crowd to react appropriately to the violence they're witnessing."*

Fighting mode itself (hands up, the room reading it, the Watch closing on seen violence) is the sim's and is already built. See `sim/tavern.hpp`, `playerHandsUp()`, and COMBAT-ACTION-SPEC. This spec is the control scheme that drives it.

## 1. The count

Before this build a player had to learn 13 core verbs and 42 physical inputs with a meaning on the keyboard. The pad spent every button and five of them changed meaning per page.

Now it is nine world verbs, plus M on the keyboard.

| # | Verb | Keyboard and mouse | Pad | Press | Hold |
|---|---|---|---|---|---|
| 1 | SWING | MOUSE1 | RT | hands down, one press raises them AND swings. Hands up, it swings | 15+ steps, hard swing |
| 2 | GUARD | MOUSE2 | LT | none | held guard. From hands down it raises the hands without a blow |
| 3 | CAST | C | RB | casts the readied crafting, refuses out loud | none |
| 4 | USE | E | A | the interact chain. Last slot before LOOK is LOWER HANDS (hands up, nothing in reach) | none |
| 5 | SNEAK | LCTRL | B | tap latches, hold holds. B is BACK on every page | held |
| 6 | JUMP | SPACE | Y | jump, mantle or drop, by what is ahead or below | none |
| 7 | RUN | LSHIFT | stick magnitude (L3 optional) | none | sprint. The walk toggle is cut |
| 8 | NOTES | J | D-pad up | your papers. Six pages, see section 3 | none |
| 9 | PAUSE | ESC | START | resume, wait, controls, settings, quit. ESC and B back out of anything | none |
| 10 | MAP | M | (a page of NOTES) | the full-screen ward map | none |

Movement is WASD plus mouse look, or the two sticks. The arrows turn, as an accessibility fallback, and they are not counted.

Bonus shortcuts, not counted, each with a door a new player finds without the key. WAIT is T or SELECT (the pause menu's WAIT row is the same page). The digits 1 to 0 ready a quick slot. The mouse wheel and D-pad left/right step the quick bar. F12 writes a screenshot.

Pad buttons left free on purpose. X, R3, LB in the world, D-pad down. An unrecognised press wakes the tutor bands and does nothing else.

## 2. Fighting mode from the controls' side

The sim owns the state. The controls only ever send the same edges they always sent.

- SWING down from hands down raises the hands and starts the charge in one press. Tap swings, hold swings hard. No wasted draw press, which is the one Oblivion habit deliberately not copied.
- GUARD down from hands down raises the hands with no blow.
- Being hit raises the hands (sim side).
- USE with nothing in reach lowers them. The reticle says LOWER HANDS before the press, through the same walk the press takes. Nothing is spoken. The row going down is the feedback (the walk's fixture link used to say NOBODY HERE SELLS WIRE on every USE at nothing, and no longer does).
- 600 idle steps (10 s) lower them on their own. No sheathe button exists.
- Talking, any page opening, picking, sleeping, arrest and defeat lower them.

The HUD row reads FISTS UP, CUDGEL UP, THE EVICTOR UP, STEEL UP while the hands are up. The crowd, the bouncer and the Watch read the same bit.

## 3. Pages and the ring

NOTES is one screen with four tiles. The sheet, the chart, the letters, the casebook. Two more pages sit past the casebook. The ward map and the grimoire. The six form a ring.

Character, Chart, Letters, Journal, Ward map, Grimoire, and round again.

The page keys walk the ring. `[` and `]` on a keyboard, LB and RB on a pad. NOTES opens on the journal, so one RB is the ward map and two is the grimoire. This is how a pad reaches both, since neither has a button of its own any more. Oblivion's own tabbed menu, where the map is a tab.

The page grammar, said once in `controls.hpp` and read raw by the router ahead of any binding.

| Step | Keyboard | Pad | Where |
|---|---|---|---|
| move the cursor | arrows | D-pad, left stick | every list |
| confirm | ENTER | A | every list |
| back | ESC | B | every page |
| page (the ring) | `[` `]` | LB RB | the tiles, the ward map, the grimoire |
| sub-tab | TAB (forward) | LT RT | the ward map's four views, the casebook page's LEADS / THE CASE, KEYS / OPTIONS |
| second commit | T | X | the ward map's TRAVEL, the haggle's TAKE THEIR PRICE |
| pick a row | digits | none | every list |
| more | 0 | none | every paged list |

On a page with no sub-tabs a trigger pull is swallowed, and on a page that is not in the ring a bumper is swallowed. Neither falls through to SWING or CAST and puts the page down with a punch or a spell. The one place a world verb still reaches through a page is a conversation, where SWING is the fight starting, which is the game.

With a pad in hand the casebook page and the ward map carry a fifth foot slot, LB RB NOTES, because the bumpers are the pad's only way on to the next page. The keyboard's foot keeps its four slots and one row.

The settings page rebinds into the slot of the key's own device. A pad button pressed at the prompt replaces the verb's pad half and leaves the keyboard key alone, and the other way round. The row shows the live hand's half.

The ward map on a pad. D-pad walks places, LT/RT cycle the views, the right stick zooms, LB/RB leave for the map's neighbours, A faces, X travels, B closes. On a keyboard the same page keeps TAB, `=` `-`, `[` `]`, ENTER, T and M.

## 4. What was cut, and where it went

| Was | Now |
|---|---|
| QuickWheel (Q hold, R3 hold). Tap opened the grimoire | cut. The bar steps on the wheel and on D-pad left/right, the digits stay, the grimoire is a page of NOTES. The tutor toast teaches the step instead of a hold |
| PagePrev / PageNext as bindable actions (`[` `]`, LB RB) | cut as actions. They are raw page grammar now, which is what lets RB cast in the world |
| KeysPage / OptionsPage (F1, F2) | cut. PAUSE, then CONTROLS or SETTINGS, on both devices |
| F3 free mouse | cut. Every page brings the pointer out on its own, and alt-tab still works |
| the walk toggle (tap RUN) | cut. RUN is a plain hold. SNEAK is the quiet stance, and a pad picks its gait off the stick |
| Map on SELECT | folded into NOTES on the pad. SELECT is WAIT |
| Attack on X, Cast on RT | RT swings, LT guards, RB casts. The primary hand on the right trigger, where the mouse's primary is |
| the map's zoom on the triggers, its views on the bumpers | views on LT/RT (Oblivion's sub-tabs), zoom on the right stick |
| the haggle's TAKE THEIR PRICE on RB | on X |
| LEFT/RIGHT reserved and inert on the casebook page while its foot advertised them | they step LEADS / THE CASE, as the foot always said |

Not cut. SNEAK (every theft resolves off it), JUMP (the roof verbs), the digits, the page grammar.

## 5. The settings file and the second clean break

The enum was rebuilt a second time, stated once in `controls.hpp` and in DECISIONS.md. There are still no shipped players. The insert-only rule resumes the moment this ships.

Every surviving verb keeps its name in the file, so a saved `bind attack ...` line still parses onto SWING. Five defaults moved, and an old `toText()` wrote every default out explicitly, so the loader has to tell an old default carried forward from a player's own choice. The rule in `fromText()`.

- A file is an OLD file exactly when it names a retired verb (`quick_wheel`, `page_prev`, `page_next`, `keys_page`, `options_page`). Every old build wrote all of them. A file naming none is read as written.
- In an old file, per slot, an old shipped key becomes the new shipped key and anything else stands.

| Verb | Slot | Old default | New default |
|---|---|---|---|
| attack | pad | PAD_X | PAD_RT |
| cast | pad | PAD_RT | PAD_RB |
| map | pad | PAD_BACK | none |
| menu | keyboard | TAB | J |
| menu | pad | PAD_BACK | PAD_UP |
| quick_next | pad | none | PAD_RIGHT |
| quick_prev | pad | none | PAD_LEFT |

The retired lines are dropped, which frees Q, R3, the bumpers and the F-keys. The whole-table validation pass still restores any of the nine (or the map) that a file left with no key at all.

The two real files on this machine (the S13-era shape with menu on TAB and PAD_BACK, and the last shipped shape with map on PAD_BACK and F1/F2) both load onto the new table with nothing stranded. `test_controls.cpp` carries both shapes verbatim.

## 6. Must hold

- The sim is untouched. This is input and presentation. Both twin baselines stand (`0x2646C1AAA2BA38DF` population, `0x837E94019BC49C25` tavern).
- Every scripted drive calls Session verbs, not keys, so `--punch`, `--charge`, `--block`, `--cast`, `--court`, `--watch-halt`, `--nemesis`, `--street`, `--skyrun`, `--roofs`, `--demo`, `--creation` keep working through the new scheme.
- Every prompt names the key for the device that last spoke. The reticle, the opening band, the ward map's foot, the haggle's foot, the controls page's own key column and its lock rows, the casebook foot.
- A controller-only player can do everything a keyboard player can. `--padscript` beats cover the buttons, both sticks (`rsup`/`rsdown` were added for the zoom) and the triggers.
