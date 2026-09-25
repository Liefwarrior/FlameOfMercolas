# The rung plate

Branch `lane/rung`, roadmap 21b. When a Legend track goes up a rung, the ward
says so. Until now it didn't. You'd open the Character tile later and the
title had changed, and that was the whole moment. Oblivion's level-up prose is
a big part of why levelling there feels like something, and this game had
none of it.

So a rung gets a plate. Not the skill toast, that's a tick in the corner. A
rung is bigger than a tick. This is a card in the terminal register, centred
high where your eye already is, three lines tops, held five seconds, then it
lifts off and it's gone.

```
THE WIRE  --  LIGHT FINGERS
The rope hands know your face now. That is not nothing.
There is no rung above that one. Mind how you wear it.     (top rung only)
```

The head is the track's name and the new title, straight off legend.cpp's own
tables. The line under it is authored, one per rung per track, in
`content/raws/barks/legend_barks.json`. It's a docker talking, never the game.
No code writes a spoken line. A key nobody wrote draws an empty row.

Every frame in this folder comes off the gated `dist\granadad.exe` through
the shipped shutter. The drives are the game's own verbs. The burglary is the
burglary line's own beats, the roofs are real leaps, the leads are read with
the same Q, the bounty is the bounty line whole, the drinks are bought at
Cull's table with real coin. Nothing reaches into the sim sideways.

## What landed

- `LegendRiseWatch`. The Legend is a pure function of counters the sim
  already hashes, so the only honest way to know a rung ROSE is to remember
  what it was and look again. The watch diffs the five rungs once a step on
  the render side, same shape as the skill toast's `SkillRiseWatch`. First
  look seeds and says nothing. Rises only. A fall is the sheet's business.
- The plate itself (`render/rung_plate.hpp`). A `PanelFrame` in the terminal
  register, diamond junctions, the head knocked out of an inverted accent
  fill, the row in prose ink, the top row dim. Sized to its content. Seated a
  quarter of the way down the spare height, so it sits under the compass
  band and the announce plate and never on the reticle or the aim prompt.
  Rises through its seat and lifts on out, never slides back the way it
  came. Own ease, own bell.
- The queue and the priority (`Session::stepRungPlate`). Two rungs in one
  step, or a second while one's up, the second waits. Queued, never dropped.
  A page, a talk, the court, the rope, the death veil and a bouncer's
  WARNING all outrank it. A rise under any of those queues and shows when
  the screen's free. A plate that's up when one of those takes the screen is
  dismissed on the spot, which is what "dismissable by any page key" means
  in practice. A rung and a skill level in the same step, the plate wins and
  the toast waits behind it. A toast already up finishes.
- The bell. `SoundId::LegendRung`, the Kenney heavy bell the harbour already
  rings, one strike on the World bus. Heavier than the toast's chime. No new
  art, no LOT row on purpose (the LOT tree's only candidates would make a
  rung sound like a case closing on one checkout and a bell on the gate).
- `--rung=TRACK`. Plays the shortest real path to a rung on that track and
  stops on the plate, fully up. Prints
  `rung=<track> <a>-><b> row="..." head="..." plate=up found=yes` so a frame
  of a plate is a frame of words and the words are the claim.
- Twenty-one rows in `legend_barks.json`. Every one fits one line of the 4x6
  font at 320x180 (56 glyphs and under), no dash in any of them, and the
  priests' word for their quarry isn't in there.

## How each track rises in the drive

Every path is beats the smoke already walks, reused whole. The hour is the
borrowed line's own hour unless you pass `--time=`.

| track | what the drive does | why that's the shortest | hour |
|---|---|---|---|
| `wire` | The burglary's own beats. Crouch, in at the door, up the stair, the wire in a guest's box (room 2), the pins worked, a shoulder if the wire ruined it, the box emptied. Then back onto the landing looking at the room just done. | A cracked box is 8 points of THE WIRE, the first threshold exactly. Four caught lifts would do it too, but every caught lift is a bouncer's warning, which outranks the plate. | 2 |
| `roofs` | In at the door, up the stair, out over the north wall onto the lead, then the alley leapt west and east until the roofs will have it. Stops on the roof, pitched down at the alley. | A roof-run is 3, skyrunning is 2 a level, a leap is two uses. The skyrun line's own alley beat, bounded the same way (24 leaps). | 1 |
| `flame` | The trail's loop for its first two leads. Nearest open lead on this band, walked to by the district's router, read with Q, twice. Then the trail's own stand-back so the frame is the room and not a wall. | A lead read is 6. Two is 12. | 9 |
| `trade` | The bounty line, whole, played to `away`. Take the job off Cull, do it, turn it in across his table. | A paid job is 6 and the coin over four is the rest. The smallest bounty on the board (`bounty_kennel`, two units at four) pays 8, so 6 + 2 lands on the threshold exactly. The plate comes up the step the talk closes. | 21 |
| `law` | Talk to Watchman Cull, stand him four drinks, close the talk, stand back. | A bought drink is a deed worth 2 of standing with the drinker's own guild. The Watch at 8 on a clean sheet is KNOWN TO THE WATCH. The bounty alone leaves you three short. | 23 |

A word that's none of the five (`--rung=bloodletter`) lands neither beat and
the summary says `found=no`.

## The rows

Every line the plate can say, for the redline. One per rung per track, plus
the one a topped-out track gets under its own. The head above each is what
legend.cpp prints, the row is what the ward says.

| key | head | row |
|---|---|---|
| `legend.wire.1` | THE WIRE  --  LIGHT FINGERS | The rope hands know your face now. That is not nothing. |
| `legend.wire.2` | THE WIRE  --  CUTPURSE | Coats get held shut when you walk by. They have heard. |
| `legend.wire.3` | THE WIRE  --  ROBBER | Finch pours before you ask. That is a robber's welcome. |
| `legend.wire.4` | THE WIRE  --  THE QUIET TENANT | Nobody sees your hands. That is all the name means. |
| `legend.roofs.1` | THE ROOFS  --  TENANT | You went up and came down whole. The roofs noticed. |
| `legend.roofs.2` | THE ROOFS  --  ROOF-WALKER | Slates do not creak under you now. Someone was counting. |
| `legend.roofs.3` | THE ROOFS  --  SKYRUNNER | The alley is a step to you now. The snug drinks to it. |
| `legend.roofs.4` | THE ROOFS  --  THE WARD'S OWN SHADOW | Nobody looks up. You are what they'd see if they did. |
| `legend.flame.1` | THE FLAME  --  DISCIPLE | You followed the trail. The Mission wrote you down. |
| `legend.flame.2` | THE FLAME  --  THE FLAME'S MAN | The priests nod to you first now. The dockers saw it. |
| `legend.flame.3` | THE FLAME  --  WIELDER OF THE FLAME | Doors open before you knock. Some folk cross the street. |
| `legend.flame.4` | THE FLAME  --  THE ONE WHO WENT DOWN THERE | You went down there and came back. Nobody asks you why. |
| `legend.trade.1` | THE TRADE  --  STALLKEEP | Paid and on time. The counter remembers that first. |
| `legend.trade.2` | THE TRADE  --  TRADER | Your word buys on credit now. Not much. But some. |
| `legend.trade.3` | THE TRADE  --  CRAFTLORD | The Weighhouse stamps your paper without reading it. |
| `legend.trade.4` | THE TRADE  --  THE WARD'S CREDITOR | Half this quay owes you. The other half is asking to. |
| `legend.law.1` | THE LAW  --  KNOWN TO THE WATCH | The Watch has your name and is not sour about it. Yet. |
| `legend.law.2` | THE LAW  --  SWORN IN | Cull calls you by name across the room. On purpose. |
| `legend.law.3` | THE LAW  --  THE SERGEANT'S MAN | Folk lower their voices when you pass now. Even sober. |
| `legend.law.4` | THE LAW  --  THE MAN VESS SENDS FOR | When it goes bad, Vess says your name first. |
| `legend.top` | (under any rung 4 head) | There is no rung above that one. Mind how you wear it. |

The font draws in caps, so on the plate the rows read in caps. The file keeps
them in sentence case so they're readable in the redline.

## Outside every hash

The watch, the plate, the queue and the bell are render state.
`test_rung_plate.cpp` runs two sessions through the same sim acts, draws the
plate in one, dismisses it under a page, queues a second rung behind it, and
compares the engine's combined hash, the tavern's own section and the three
books. Byte identical at every checkpoint. The gate agrees.

| baseline | number |
|---|---|
| population, `--population --population-hour 16 --ticks 7200`, twice | `0x2646C1AAA2BA38DF` |
| tavern twin, `--tavern --ticks 900`, twice | `0x63F354D02A6B600F` |

## The frames

960x540 is `--width=960 --height=540 --scale=1`. `--settle-steps=0` is the
plate at its first fully-up frame. Leave it off and you get the plate a
second into its hold, which is the same picture.

`scripts\shoot-rung.ps1` shoots all six and keeps every summary line in
`shoot-rung.log` beside the PNGs. Or run them by hand.

| frame | drive | what it proves |
|---|---|---|
| `rung-wire-960.png` | `granadad.exe --smoke=0 --hold --width=960 --height=540 --scale=1 --rung=wire --settle-steps=0 --screenshot=docs\frames\rung\rung-wire-960.png` | One cracked box at two in the morning. `THE WIRE  --  LIGHT FINGERS` on the plate, the wire.1 row under it, the landing and the done room behind. Summary reads `rung=wire 0->1 row="The rope hands know your face now. That is not nothing." plate=up found=yes`. |
| `rung-roofs-960.png` | the same with `--rung=roofs` | The alley leapt until the roofs will have it. `THE ROOFS  --  TENANT` over the lead, the alley under the eye. `SKYRUNNING RISES TO n` may be queued behind it in the corner, and the plate is up first. |
| `rung-flame-960.png` | the same with `--rung=flame` | Two leads read at the Mission at nine. `THE FLAME  --  DISCIPLE`, the back room stood back from. The pull line under the ribbon still reads the next lead. |
| `rung-trade-960.png` | the same with `--rung=trade` | The bounty paid across Cull's table. `THE TRADE  --  STALLKEEP` the step the talk closes, Cull still in frame. |
| `rung-law-960.png` | the same with `--rung=law` | Four drinks stood to Cull at eleven. `THE LAW  --  KNOWN TO THE WATCH`, Cull on his stool. |
| `rung-wire-320.png` | the first drive at `--width=320 --height=180` | The strip pin. The longest rows are 56 glyphs and the plate is 60 cells wide in a 64-cell frame, so a cell of air each side and the row whole. Under the compass band, clear of the reticle. |

## Not in this folder yet

- The PNGs. The exe in `dist\` is the wip baseline and doesn't have the plate.
  Once the gate builds this tree, run `scripts\shoot-rung.ps1` and the six
  land here with their log.
- A frame of the plate dismissed by a warning, or queued behind a page. Those
  are timing, not a picture. `test_rung_plate.cpp` pins both, plus the toast
  waiting behind the plate and the plate never moving a hash.
- A frame of `legend.top`. That's fifteen boxes or a closed case plus more,
  and no drive is that long on purpose. The row is pinned to fit and to
  draw, and the top-rung case in the tests reads it back off the plate.
