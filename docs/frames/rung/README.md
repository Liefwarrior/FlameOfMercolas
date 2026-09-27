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
Ropewynd locks its boxes twice these days. That is you.
There is nothing above that. Mind how you wear it.         (top rung only)
CRACKSMANSHIP RISES TO 2                                   (the same step's skill, dim)
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
  fill, one cell row of air, then the row in prose ink, the top row dim under
  that. Sized to its content. Seated a quarter of the way down the spare
  height plus one more cell row of collision clearance, clamped so that row
  never comes out of the reticle's own fence -- it sits under the compass
  band and the announce plate and never on the reticle or the aim prompt.
  Rises through its seat and lifts on out, never slides back the way it
  came. Own ease, own bell.
- The queue and the priority (`Session::stepRungPlate`). Two rungs in one
  step, or a second while one's up, the second waits. Queued, never dropped.
  A page, a talk, the court, the rope, the death veil and a bouncer's
  WARNING all outrank it. A rise under any of those queues and shows when
  the screen's free. A plate that's up when one of those takes the screen is
  dismissed on the spot, which is what "dismissable by any page key" means
  in practice. A rung and a skill level, same step or not, the plate wins:
  a new toast waits behind it, and one already up when the plate lands goes
  dark rather than share the corner and does not come back -- a rung and its
  matching skill are sometimes the same event said twice, and the plate has
  already told it.
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

One rule runs through all five: **nothing walks after a rung.** The plate holds
five seconds and a scripted walk spends them faster than anything else in the
build, so the frame is made first and the rung is the last thing that happens.
`--trail=opened` has said the same about the lead-opened notice since the
casebook pass. Two of these tracks were written the other way round and neither
of them landed a plate.

| track | what the drive does | why that's the shortest | hour |
|---|---|---|---|
| `wire` | The burglary's own beats. Crouch, in at the door, up the stair, the wire in a guest's box (`kRooms[2]`, ROOM 3 on its door), the pins worked, a shoulder if the wire ruined it. Then the eye goes up the landing the burglar came up, and then the lid. | A cracked box is 8 points of THE WIRE, the first threshold exactly. Four caught lifts would do it too, but every caught lift is a bouncer's warning, which outranks the plate. The lid is the rung, so it goes last: `--burgle` walks back to the landing to shoot the room and that walk, crouched, costs more than the plate has. | 2 |
| `roofs` | In at the door, up the stair, out over the north wall onto the lead, then the alley leapt west and east until the roofs will have it. Stops on the roof it rose on, pitched down over the Gull's lead, the harbour off to the left. | A roof-run is 3, skyrunning is 2 a level, a leap is two uses. The skyrun line's own alley beat, bounded the same way (24 leaps). | 18 |
| `flame` | The trail's loop for its first two leads. Nearest open lead on this band, backed off four tiles the way the body came in, and read from there with Q, twice. Onto the anchor itself only if it will not read from off it. | A lead read is 6. Two is 12. The trail stands back after it reads; this stands back first. Four tiles is inside kLookRangeTiles so the clue reads the same, and the flagstones are indoors, so the trail's seven-tile step back is out of the door, round the almshouse and down to the quay. Fifty tiles. The plate has five seconds. | 9 |
| `trade` | The bounty line, whole, played to `away`. Take the job off Cull, do it, turn it in across his table, and turn to face him. | A paid job is 6 and the coin over four is the rest. The smallest bounty on the board (`bounty_kennel`, two units at four) pays 8, so 6 + 2 lands on the threshold exactly. The plate comes up once the talk's close tail is down. | 21 (the line runs the clock to 23) |
| `law` | Talk to Watchman Cull, stand him four drinks, close the talk, stand back, face him. | A bought drink is a deed worth 2 of standing with the drinker's own guild. The Watch at 8 on a clean sheet is KNOWN TO THE WATCH. The bounty alone leaves you three short. | 23 |

A word that's none of the five (`--rung=bloodletter`) lands neither beat and
the summary says `found=no`. `--time=` moves the clock, but the trade and the
law need Cull at his table, so a daytime hour on either lands no plate and the
summary says so (`0->0 plate=down`). The roofs run at six in the evening on
purpose: the skyrun line's own hour is one in the morning, and a plate over a
black frame is a card over nothing.

## The rows

Every line the plate can say, for the redline. One per rung per track, plus
the one a topped-out track gets under its own. The head above each is what
legend.cpp prints, the row is what the ward says.

| key | head | row |
|---|---|---|
| `legend.wire.1` | THE WIRE  --  LIGHT FINGERS | Ropewynd locks its boxes twice these days. That is you. |
| `legend.wire.2` | THE WIRE  --  CUTPURSE | Coats get held shut when you walk by. They have heard. |
| `legend.wire.3` | THE WIRE  --  ROBBER | Finch pours before you ask. That is a robber's welcome. |
| `legend.wire.4` | THE WIRE  --  THE QUIET TENANT | Your hands are never seen. That is all the name means. |
| `legend.roofs.1` | THE ROOFS  --  TENANT | Up and over, and the roofs had no objection. |
| `legend.roofs.2` | THE ROOFS  --  ROOF-WALKER | Slates do not creak under you. Someone was counting. |
| `legend.roofs.3` | THE ROOFS  --  SKYRUNNER | The alley is a step to you. The snug drinks to it. |
| `legend.roofs.4` | THE ROOFS  --  THE WARD'S OWN SHADOW | Nobody looks up. You are what they'd see if they did. |
| `legend.flame.1` | THE FLAME  --  DISCIPLE | You are in the Mission's book. Nobody asked you first. |
| `legend.flame.2` | THE FLAME  --  THE FLAME'S MAN | The priests nod to you first now. The dockers saw it. |
| `legend.flame.3` | THE FLAME  --  WIELDER OF THE FLAME | Doors open before you knock. Some folk cross the street. |
| `legend.flame.4` | THE FLAME  --  THE ONE WHO WENT DOWN THERE | Down there and back. The quay stopped asking why. |
| `legend.trade.1` | THE TRADE  --  STALLKEEP | A job done and coin taken for it. The counter remembers. |
| `legend.trade.2` | THE TRADE  --  TRADER | Your word buys on credit. Not much. But some. |
| `legend.trade.3` | THE TRADE  --  CRAFTLORD | The Weighhouse stamps your paper without reading it. |
| `legend.trade.4` | THE TRADE  --  THE WARD'S CREDITOR | Half this quay owes you. The other half is asking to. |
| `legend.law.1` | THE LAW  --  KNOWN TO THE WATCH | The Watch has your name now. Not sour about it. Yet. |
| `legend.law.2` | THE LAW  --  SWORN IN | Cull calls you by name across the room. On purpose. |
| `legend.law.3` | THE LAW  --  THE SERGEANT'S MAN | Voices drop when you pass. Even sober ones. |
| `legend.law.4` | THE LAW  --  THE MAN VESS SENDS FOR | When it goes bad, Vess says your name first. |
| `legend.top` | (under any rung 4 head) | There is nothing above that. Mind how you wear it. |

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
| population, `--population --population-hour 16 --ticks 7200`, twice | `0xDD3890042DD2B472` |
| tavern twin, `--tavern --ticks 900`, twice | `0x63F354D02A6B600F` |

Both are `dist\granadad-twin-gate.exe` runs. The population number here read
`0x2646C1AAA2BA38DF` until the third pass; that was the digest from before
street senses moved it (wip d23ba091, the tree this lane was cut from), copied
in without being re-run. Re-run on the fourth pass's gated exe, both twice,
both IDENTICAL. They prove the SIM is where it was: the twin gate links the
sim and never the renderer, so it cannot see the plate at all. The plate's own
evidence is the hash case above, which draws it.

## The frames

960x540 is `--width=960 --height=540 --scale=1`. `--settle-steps=0` is the
plate at its first fully-up frame. Leave it off and you get the plate a
second into its hold, which is the same picture.

`scripts\shoot-rung.ps1` shoots all six and keeps every summary line in
`shoot-rung.log` beside the PNGs (kept locally: `*.log` is gitignored, so the
branch carries the frames and not the log). Or run them by hand.

| frame | drive | what it proves |
|---|---|---|
| `rung-wire-960.png` | `granadad.exe --smoke=0 --hold --width=960 --height=540 --scale=1 --rung=wire --settle-steps=0 --screenshot=docs\frames\rung\rung-wire-960.png` | One cracked box at two in the morning. `THE WIRE  --  LIGHT FINGERS` on the plate, the wire.1 row under it, the landing away in front and the done room behind. Summary reads `rung=wire 0->1 row="Ropewynd locks its boxes twice these days. That is you." plate=up found=yes`. |
| `rung-roofs-960.png` | the same with `--rung=roofs` | The alley leapt until the roofs will have it. `THE ROOFS  --  TENANT` over the lead, the alley under the eye. Skyrunning levels on the same leap, and `SKYRUNNING RISES TO n` is the plate's own dim last row -- the same climb, told once, on the card. |
| `rung-flame-960.png` | the same with `--rung=flame` | Two leads read at the Mission at nine. `THE FLAME  --  DISCIPLE`, the back room stood back from. The pull line under the ribbon still reads the next lead. |
| `rung-trade-960.png` | the same with `--rung=trade` | The bounty paid across Cull's table. `THE TRADE  --  STALLKEEP` once the talk's close tail is down, faced at Cull from the table with no walk after the rung. What is under the reticle is the summary's `aim="..."`; see the fifth pass for why it is not him. |
| `rung-law-960.png` | the same with `--rung=law` | Four drinks stood to Cull at eleven. `THE LAW  --  KNOWN TO THE WATCH`, faced at Cull the same way, `aim="..."` in the summary. |
| `rung-wire-320.png` | the first drive at `--width=320 --height=180` | The strip pin. `wire.1` is 52 glyphs, so this plate is 58 cells wide; the 56-glyph row (`flame.3`) makes the widest plate the sheet can build, 62 cells in a 64-cell frame -- a cell of margin off each edge, two cells of gutter inside each keyline -- and the test pins them at that width rather than a drive. Seated in the middle of what the smallest window has between the stat stack and the aim prompt. |

## Second pass, 2026-09-25

The critic scored the plate a 7 and left two things in the way of an 8.

- **The moment didn't own the screen.** On the roofs drive, a leap that tips
  THE ROOFS also levels the SKYRUNNING skill, and the green `SKYRUNNING
  RISES TO n` toast used to fire right through the plate landing on top of
  it, clipping the compass ribbon's own bearing glyph. It's the same climb
  said twice. Now the toast goes dark the instant the plate wants the
  screen -- a new one waits behind it same as always, but one already
  counting down when the plate lands is cut there and doesn't come back,
  the identical `outranked` idiom the plate already uses on itself under a
  page or a warning. Separately, the `NEW LEAD` box was landing close enough
  above the plate on the flame drive that the two keylines read as one pile.
  The seat now spends one more cell row pushing the plate down off it,
  clamped so that push never costs the reticle its own clearance.
- **Zero leading between the head and the prose.** The knocked-out head sat
  flush on the row under it at every size. The plate now keeps one blank
  cell row between them, so it grows by a row -- the geometry pins in
  `test_rung_plate.cpp` grew with it.
- `legend.law.1` read a half-step more writerly than its four neighbours.
  Trimmed the connective tissue and kept the "Yet.": *The Watch has your
  name now. Not sour about it. Yet.*

## Third pass, 2026-09-25

The record critic on e3e7a600 held the plate at 7 and named three things.

- **The gold banner outweighed the plate.** The amber alert row at the foot
  of the frame -- `CRACKED IT - 14C AND 1 PIECE...` on the wire drive,
  `WATCHMAN CULL: THAT IS HONEST WORK...` on the trade -- fired in the same
  frame, wider and just as bright. It's not cut the way the skill toast was:
  the toast is the same climb said twice, but the take off a box or the
  length of a leap is news of its own, and its four-second hold would be
  lost whole under a five-second plate. It's not held back either; a leap's
  length five seconds after the leap is a notice out of its moment. It
  stands at **half weight** while the plate is up, scaled on the plate's own
  fade so the two cross rather than snap: dims as the plate rises, whole
  again as it lifts. A bouncer's WARNING rides that same row and dismisses
  the plate outright, so the one line you must not miss is never the one
  dimmed. `Session::alertFadeNow` is the one arithmetic, drawn and pinned.
- **One cell of gutter.** The widest row all but touched its keyline while
  the plate kept a whole row of air between its head and its prose. Two
  cells inside each keyline now, at every size (`kRungPlateGutterCells`).
  The strip pin still holds exactly: 56 glyphs, two gutters a side, two
  borders is the 64-cell frame less its margin, so `kRungPlateRowCells`
  stands at 56 and the geometry pins are exact widths now, not floors.
- **`OVER 3 TILES.`** isn't the plate's. It's the alert row's own leap
  announce (composed in `Session::tryClimb` the moment the jump is armed, and
  said through `say()`), on screen because the roofs drive ends on the leap
  that tips the rung. The plate's
  prose has no numeral in any of its twenty-one rows and the sheet is now
  pinned that way, so a count can never turn up in a docker's mouth on this
  card. The leap line itself keeps its number: coin and tiles are the alert
  row's register, and at half weight under the plate it reads as what it is.

The doubled speaker on the law drive (`WATCHMAN CULL: WATCHMAN CULL TAKES
THE DRINK.`) is the drink line's own composition, not the plate's, and has
its own chip.

## Fourth pass, 2026-09-26

Three critics on the third pass -- a director on the frames, a player on the
drives, an auditor on the claims -- came back 7, 7 and 6, and the lowest
governs. Their lists, taken whole:

- **Everything else stands down while the plate holds.** The third pass
  halved the alert row and left it at the glance size, one tier bigger than
  the plate; the voice row (a docker's bark, Cull's farewell) was never
  dimmed at all, and `NEW LEAD` sat above the plate at full strength. Now:
  the alert row steps down to the plate's own register (the minor scale) as
  well as to half weight, and its backing plate takes half again so the
  keyline never outshines the words; the voice row rides the same weight;
  the announce slot (`NEW LEAD`, a named place crossed) is drawn at zero the
  instant the plate is wanted (its own ease-out would cross the rise), holds
  its clock behind the plate, and rises again with what it had once the plate
  lifts. The compass ribbon,
  the aim prompt and the corner stacks are the HUD's furniture and stay.
  The plate keeps the terminal register on purpose -- the reference's one
  idiom for emphasis is the knocked-out head, never a second glyph size --
  and it is the one *notice* at full strength.
- **The tick is folded in.** A skill that levels on the same step as a rung
  (skyrunning on the leap that tips THE ROOFS, cracksmanship on the box that
  tips THE WIRE, fieldcraft on the bounty) used to fire its green toast the
  moment the plate's hold ended -- the same climb, told twice, five seconds
  late, in bigger type, clipping the compass line. It is the plate's own
  dim last row now (`SKYRUNNING RISES TO 2`, the dateline slot), told once,
  on the card. A skill that levels later in the hold still queues, and
  starts only once the plate has lifted all the way. At 320x180 there is no
  row to spend between the stat stack and the aim prompt, so the tick row is
  the one thing the smallest window drops; the Character tile has the level.
- **The alert row's clock stops under the plate.** A line said with the rung
  (`CRACKED IT - 14C AND 1 PIECE...`) used to spend its whole four seconds
  at half weight and be gone before the lift, so the take was never shown
  whole. Its countdown holds while the plate is wanted and resumes when the
  plate lifts, capped at two seconds so a leap's length is not five seconds
  late for long. A bouncer's WARNING is not on that clock and never was.
- **The Watch's word outranks the plate.** A street watchman's HALT, SHEATHE
  or OFFENCE rides the alert row through `say()` like any other line, and an
  ignored halt ends in an arrest at reach -- exactly the row a burglar earns
  THE WIRE under. The auditor's finding. A demand on the row now dismisses
  the plate the way a bouncer's warning does, for as long as the row holds
  it, so it is never the line dimmed. It is pinned by reading, not by a
  case: no test fixture in this build raises a street halt through a
  Session, and building one is a Watch lane's job.
- **The plate waits for a page's tail.** A rung earned under a page used to
  start the step the page key went down and spend its whole rise under the
  page's own eight-step close tail, then pop in at full strength. The tail
  outranks the plate now, so it rises where it can be seen.
- **The lift is a lift.** Eight steps and three units of drift read as
  switched off after a five-second hold that opened on a heavy bell. The
  rise keeps its eight; the lift takes twenty-four with twice the drift.
- **Geometry.** A cell row of clearance between the plate and the aim
  prompt at every size that has one to spend; the smallest window splits
  what it has evenly between the stat stack above and the prompt below. The
  ground slab sits symmetric about the keyline (the panel vocabulary's own
  border cells put two units above the rule and four below, and on a bright
  wall that read as a card with its keyline off centre). The head's gold
  block is one unit of gold, six of glyph, one of gold, and drops the font's
  trailing advance so its pads match.
- **The rows, for the redline.** The player caught the first row a player
  ever reads contradicting the screen: *The rope hands know your face now*
  on an unseen lift with HIDDEN on the stat stack. The director counted
  "now" in seven of twenty-one rows, "Nobody" opening three of the four
  capstones, `flame.1` narrating the player's verb where every other first
  rung reports how the ward reacted, and the system's word "rung" in a
  docker's mouth on `legend.top`. Nine rows changed, each still under
  fifty-six glyphs, no dash, no numeral, and still salted:

  | key | was | is |
  |---|---|---|
  | `legend.wire.1` | The rope hands know your face now. That is not nothing. | Word travels. The rope hands have your name already. |
  | `legend.wire.4` | Nobody sees your hands. That is all the name means. | Your hands are never seen. That is all the name means. |
  | `legend.roofs.2` | Slates do not creak under you now. Someone was counting. | Slates do not creak under you. Someone was counting. |
  | `legend.roofs.3` | The alley is a step to you now. The snug drinks to it. | The alley is a step to you. The snug drinks to it. |
  | `legend.flame.1` | You followed the trail. The Mission wrote you down. | The priests saw you read. You are in the Mission's book. |
  | `legend.flame.4` | You went down there and came back. Nobody asks you why. | You went down there and came back. Folk stopped asking. |
  | `legend.trade.2` | Your word buys on credit now. Not much. But some. | Your word buys on credit. Not much. But some. |
  | `legend.law.3` | Folk lower their voices when you pass now. Even sober. | Folk lower their voices when you pass. Even sober. |
  | `legend.top` | There is no rung above that one. Mind how you wear it. | There is nothing above that. Mind how you wear it. |

- **The drives frame their cause.** Trade and law end facing Watchman Cull
  by his own bearing (he was off frame in both, a patron and a hood under
  the reticle). The roofs run at six in the evening so the lead, the Gull
  and the harbour are in the frame under the plate rather than a black sky
  at one in the morning. The wire stays at two in the morning: a burglary
  is dark, the box that tipped the rung is under the reticle, and the plate
  is the light in the room.
- **The auditor's paperwork.** `Session::leap` never existed (the leap line
  is composed in `Session::tryClimb`); the gutter's "two" and the weight's
  "half" are pinned as numbers now, not as the symbol the code uses; the
  drawn wiring is pinned on pixels (the alert row's amber under a plate
  against the same frame without one); a warning's first frames are pinned
  step by step against a twin session with no rung; and the room index in
  the wire drive's prose is `kRooms[2]`, ROOM 3 on its door.

What is declared and left: the two-cell gutter holds at every window that
deals the panel register 64 cells across (every 16:9 size from 320x180 up);
a squarer window (1440x1080, 720x720) spends the gutter first and then clips
a 56-glyph row with the cell drawer's mark. The top rung at 320x180 (fifteen
boxes, no drive) sits on its floor with no clearance to spend.

## Fifth pass, 2026-09-26

The fourth pass went back to the three critics: director 7, player 6, the
auditor's score is on the record with theirs. The card itself passed; what
was left was the choreography round it and the frames under it.

- **Nothing comes back until the plate has left.** The stand-downs followed
  the plate's *hold*, so on the first frame of the lift the alert row jumped
  back to the glance size at near full weight and `NEW LEAD` rose across a
  card still three-quarters up. They follow the plate *on screen* now (its
  drawn fade), through the whole lift; the skill toast already waited for
  that. And a line that was on the row under the plate finishes its capped
  two seconds in the small type: it never swells.
- **The latest level supersedes.** The roofs run levels skyrunning on the
  way up; `SKYRUNNING RISES TO 2` was still queued behind `RISES TO 1`'s hold
  when the leap that tipped THE ROOFS levelled it to 3 and folded that into
  the plate -- so after the lift the old toast played, with the number going
  backwards, over the compass line. The queue keeps one toast per skill,
  the newest; a fold clears that skill's queue.
- **Fine print is still print.** At half weight the rows' backing let the
  Gull's lit plaster through and the amber read as mud on the trade and law
  frames. The weight is on the ink now: the backing keeps the row's own
  ease, the rails halve, the words dim. Same for the voice row.
- **The tick row breathes.** Set solid under the prose it read as a second
  line of the docker's in the system's voice. It sits two units low in its
  row: three of air above it, two between its feet and the rule.
- **The smallest window keeps its stack clear.** With seven pixels to share
  the fourth pass split them; the keyline sat two under the stat stack
  while the prompt had four. The card sits on the prompt's fence now and
  every spare pixel is above it.
- **640x360 takes the glance register.** The one window where the terminal
  register has bottomed out at one pixel-scale under a HUD at two: the card
  was the smallest type on its own screen. It draws at the glance scale
  there and only there (`rungPlateMetric`): a 64-cell frame at scale 2 is
  exactly 640 pixels, and the band between the floor and the prompt holds
  the card with a row to spare.
- **The cause in the picture, and where it cannot be.** `standBackFrom`
  wants a clear line now -- every tile between the spot and the man floor,
  nobody of the Gull's roster on it -- before it settles for the sim's own
  line of sight, which sees over a table and through a standing man. It
  does not see the ward's bodies that drink in the Gull, and at Cull's
  stool the frame is theirs: a patron stands on the west approach, and the
  hood on Cull's own table is nearer than he is from every cardinal, so
  the aim prompt names the hood or the patron from any spot a tile, two or
  three off him. A ring of those spots, walked and read, was tried (gate
  14) and spent the plate's whole hold finding none. So the trade and law
  drives stop walking after the rung: the bounty closes at the table
  (`runContractLine`'s `table` ending), the drinks end where the talk was,
  and both face him by bearing from there. **The summary now prints what
  the prompt named at the shutter (`aim="..."`), on every track, and the
  drive tests pin that it does; on trade and law it is the honest answer,
  not Cull.** Putting him under the reticle wants a different corner of the
  Gull or a different hour, which is a Gull lane's call. The flame drive
  pitches the eye down onto the flagstones once the last read has tipped
  the track (after the loop: the walk to the second lead reset it the first
  time). The wire drive stays as it is: the box is under the reticle at two
  in the morning with the lanterns out, and a burglary is dark.
- **The Gull's own watchman.** Cull's halt and a contraband demand ride the
  alert row through the same `say()` as the street's, and the fourth pass
  flagged only the street's three sites; his outrank the plate now too.
  The fourth pass also said no fixture raises a street halt through a
  Session, which was wrong: `--street-assault=watch-halt` does. A case that
  raises one under a plate is still owed; the rule is pinned by reading.
- **The clock is capped on a dismissal too.** A live hold cut by a page or a
  warning left the row's frozen clock whole, so a leap's length could
  surface six seconds after the leap; the cap applies wherever the hold is
  cut. And a toast never starts ahead of a plate waiting behind a page.
- **What the slab actually is.** Two units outside the rules' dashes above
  and below and outside the side bars' column left and right, the same on
  each pair of edges; the corner diamonds sit a unit inside that all round,
  and a tilde's second row a unit closer below. The fourth pass said "two
  on every side", which the corners never were.
- **The seat, honestly.** The floor is the announce slot's lowest row; at
  320x180 the stat stack's fourth row reaches two rows under it, so with
  seven pixels to share the card keeps five above its keyline and two
  below. A top-rung card at 320x180 (fifteen boxes; no drive) sits on the
  floor over the stack's last row. At 1920x1080 a top-rung card drops its
  tick row (the seat has no row for a seventh); 640x360 and 960x540 hold
  both.
- **Six rows again, for the redline.** The director counted "name" in five
  rows and "Folk" in three, `wire.1` echoing `law.1`, two rows sharing a
  sentence shape; the player read `roofs.1` ("came down whole") standing
  on the roof, `trade.1` as the player paying, and `flame.1`'s witness
  contradicted by a priest who asks who you are eight seconds later.

  | key | was | is |
  |---|---|---|
  | `legend.wire.1` | Word travels. The rope hands have your name already. | Ropewynd locks its boxes twice these days. That is you. |
  | `legend.roofs.1` | You went up and came down whole. The roofs noticed. | Up and over, and the roofs had no objection. |
  | `legend.flame.1` | The priests saw you read. You are in the Mission's book. | You are in the Mission's book. Nobody asked you first. |
  | `legend.flame.4` | You went down there and came back. Folk stopped asking. | Down there and back. The quay stopped asking why. |
  | `legend.trade.1` | Paid and on time. The counter remembers that first. | A job done and coin taken for it. The counter remembers. |
  | `legend.law.3` | Folk lower their voices when you pass. Even sober. | Voices drop when you pass. Even sober ones. |

Declared and left: the trade and law drives spend the stand-back's walk
inside the plate's hold -- "nothing walks after a rung" is not true of
them, the rung rises mid-talk and the stand-back is the frame -- so their
lift comes about twenty steps sooner after the shutter's first frame than
the wire's or the roofs'; the summary line on a daytime `--time=` for those
two says `found=yes plate=down` (the word was found, the plate was not);
the voice row's ink weight and the place plate's held clock ride the same
code as the alert row's and the case plate's and are pinned through them.

## Not in this folder yet

- A frame of the plate dismissed by a warning, or queued behind a page. Those
  are timing, not a picture. `test_rung_plate.cpp` pins both, plus the toast
  waiting behind the plate and the plate never moving a hash.
- A frame of `legend.top`. That's fifteen boxes or a closed case plus more,
  and no drive is that long on purpose. The row is pinned to fit and to
  draw, and the top-rung case in the tests reads it back off the plate.
