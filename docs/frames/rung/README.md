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
  rings -- the longest of its five strikes, and always that one -- one
  strike on the World bus. Heavier than the toast's chime. No new
  art, no LOT row on purpose (the LOT tree's only candidates would make a
  rung sound like a case closing on one checkout and a bell on the gate).
- `--rung=TRACK`. Plays the shortest real path to a rung on that track and
  stops on the plate, fully up. Prints
  `rung=<track> <a>-><b> row="..." head="..." plate=up|down found=yes|no
  called=<the rung's name> aim="<what the prompt names>" rose=yes|no
  [reason="..."]` so a frame of a plate is a frame of words and the words
  are the claim.
- Twenty-one rows in `legend_barks.json`. Every one fits one line of the 4x6
  font at 320x180 (56 glyphs and under), no dash in any of them, and the
  priests' word for their quarry isn't in there.

## How each track rises in the drive

Every path is beats the smoke already walks, reused whole. The hour is the
borrowed line's own hour unless you pass `--time=`.

One rule runs through the wire, the roofs and the flame: **nothing walks after
a rung.** The plate holds five seconds and a scripted walk spends them faster
than anything else in the build, so the frame is made first and the rung is the
last thing that happens. `--trail=opened` has said the same about the
lead-opened notice since the casebook pass. Two of these tracks were written
the other way round and neither of them landed a plate. The trade and the law
are the declared exceptions: both rise across Cull's table, and the bounty
line's own stand-back (three tiles, about twenty-five steps of the hold)
walks after the rung so the frame is the room and not his table's edge.

| track | what the drive does | why that's the shortest | hour |
|---|---|---|---|
| `wire` | The burglary's own beats. Crouch, in at the door, up the stair, the wire in a guest's box (`kRooms[2]`, ROOM 3 on its door), the pins worked, a shoulder if the wire ruined it. Then the eye goes up the landing the burglar came up, and then the lid. | A cracked box is 8 points of THE WIRE, the first threshold exactly. Four caught lifts would do it too, but every caught lift is a bouncer's warning, which outranks the plate. The lid is the rung, so it goes last: `--burgle` walks back to the landing to shoot the room and that walk, crouched, costs more than the plate has. | 2 |
| `roofs` | In at the door, up the stair, out over the north wall onto the lead, then the alley leapt west and east until the roofs will have it. Stops on the roof it rose on, pitched down over the Gull's lead, the harbour off to the left. | A roof-run is 3, skyrunning is 2 a level, a leap is two uses. The skyrun line's own alley beat, bounded the same way (24 leaps). | 18 |
| `flame` | The trail's loop for its first two leads. Nearest open lead on this band, backed off four tiles the way the body came in, and read from there with Q, twice. Onto the anchor itself only if it will not read from off it. | A lead read is 6. Two is 12. The trail stands back after it reads; this stands back first. Four tiles is inside kLookRangeTiles so the clue reads the same, and the flagstones are indoors, so the trail's seven-tile step back is out of the door, round the almshouse and down to the quay. Fifty tiles. The plate has five seconds. | 9 |
| `trade` | The bounty line, whole, played to `away`. Take the job off Cull, do it, turn it in across his table, stand back the line's own three tiles, and dip the eye to him. | A paid job is 6 and the coin over four is the rest. The smallest bounty on the board (`bounty_kennel`, two units at four) pays 8, so 6 + 2 lands on the threshold exactly. The plate comes up once the talk's close tail is down. | 21 (the line runs the clock to 23) |
| `law` | Talk to Watchman Cull, stand him four drinks, close the talk, stand back, the eye level and a touch up. | A bought drink is a deed worth 2 of standing with the drinker's own guild. The Watch at 8 on a clean sheet is KNOWN TO THE WATCH. The bounty alone leaves you three short. | 23 |

A word that's none of the five (`--rung=bloodletter`) lands neither beat and
the summary says `found=no`. `--time=` moves the clock, but the trade and the
law need Cull at his table, so a daytime hour on either lands no plate and the
summary says so (`0->0 plate=down rose=no reason="Watchman Cull is not in the
Gull when the line starts"` -- the reason is read off the roster when the line
starts, not assumed from the hour). The roofs run at six in the evening on
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
| `legend.wire.3` | THE WIRE  --  ROBBER | Finch pays before the sack is open. Robber's rates. |
| `legend.wire.4` | THE WIRE  --  THE QUIET TENANT | The ward never saw your hands. It found a word anyway. |
| `legend.roofs.1` | THE ROOFS  --  TENANT | Up and over, and the roofs had no objection. |
| `legend.roofs.2` | THE ROOFS  --  ROOF-WALKER | Slates do not creak under you. Someone was counting. |
| `legend.roofs.3` | THE ROOFS  --  SKYRUNNER | The alley is a step to you. The snug drinks to it. |
| `legend.roofs.4` | THE ROOFS  --  THE WARD'S OWN SHADOW | Nobody looks up. You are what they'd see if they did. |
| `legend.flame.1` | THE FLAME  --  DISCIPLE | In the Mission's book already. Not your face, your work. |
| `legend.flame.2` | THE FLAME  --  THE FLAME'S MAN | The priests nod your way now. The dockers saw it. |
| `legend.flame.3` | THE FLAME  --  WIELDER OF THE FLAME | Doors open before you knock. Some folk cross the street. |
| `legend.flame.4` | THE FLAME  --  THE ONE WHO WENT DOWN THERE | Down there and back. The quay stopped asking why. |
| `legend.trade.1` | THE TRADE  --  STALLKEEP | A job done and coin taken for it. The counter remembers. |
| `legend.trade.2` | THE TRADE  --  TRADER | Your word buys on credit. Not much. But some. |
| `legend.trade.3` | THE TRADE  --  CRAFTLORD | The Weighhouse stamps your paper without reading it. |
| `legend.trade.4` | THE TRADE  --  THE WARD'S CREDITOR | Half this quay owes you. The other half is asking to. |
| `legend.law.1` | THE LAW  --  KNOWN TO THE WATCH | The Watch has your name now. Not sour about it. Yet. |
| `legend.law.2` | THE LAW  --  SWORN IN | Cull calls out to you across the room. On purpose. |
| `legend.law.3` | THE LAW  --  THE SERGEANT'S MAN | Voices drop when you pass. The sober ones drop lowest. |
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
plate at its first fully-up frame. Leave it off and you get the default
settle (`kCaptureRestSteps`, 240 zero-input steps: four seconds into the
five-second hold), which is the same picture -- the wire's lift starts about
fifty steps after it, the trade's and the law's about thirty, their stand-back
having spent the first twenty-five.

`scripts\shoot-rung.ps1` shoots all six and keeps every summary line in
`shoot-rung.log` beside the PNGs (kept locally: `*.log` is gitignored, so the
branch carries the frames and not the log). Or run them by hand.

| frame | drive | what it proves |
|---|---|---|
| `rung-wire-960.png` | `granadad.exe --smoke=0 --hold --width=960 --height=540 --scale=1 --rung=wire --settle-steps=0 --screenshot=docs\frames\rung\rung-wire-960.png` | One cracked box at two in the morning. `THE WIRE  --  LIGHT FINGERS` on the plate, the wire.1 row under it, the landing away in front and the done room behind; the prompt names the box (`aim="THE STRONGBOX"`) and the lid itself is in the dark, not the picture. Summary reads `rung=wire 0->1 row="Ropewynd locks its boxes twice these days. That is you." head="THE WIRE  --  LIGHT FINGERS" plate=up found=yes called=LIGHT FINGERS aim="THE STRONGBOX" rose=yes`. |
| `rung-roofs-960.png` | the same with `--rung=roofs` | The alley leapt until the roofs will have it. `THE ROOFS  --  TENANT` over the lead, the alley under the eye. Skyrunning levels on the same leap, and `SKYRUNNING RISES TO n` is the plate's own dim last row -- the same climb, told once, on the card. |
| `rung-flame-960.png` | the same with `--rung=flame` | Two leads read at the Mission at nine. `THE FLAME  --  DISCIPLE`, the back room stood back from. The pull line under the ribbon still reads the next lead. |
| `rung-trade-960.png` | the same with `--rung=trade` | The bounty paid across Cull's table. `THE TRADE  --  STALLKEEP` once the talk's close tail is down, from the bounty line's own stand-back with the eye dipped to Cull. The summary's `aim="..."` is the INTERACT target in reach from the stand-back tile, not what the reticle crosses, and on this corner it is a patron; see the fifth and seventh passes. |
| `rung-law-960.png` | the same with `--rung=law` | Four drinks stood to Cull at eleven. `THE LAW  --  KNOWN TO THE WATCH`, from the stand-back, the eye level and a touch up; `aim="HOOD"` in the summary: the hood on the stand a tile south of the stand-back tile, the interact target in reach, which no pitch changes. |
| `rung-wire-320.png` | the first drive at `--width=320 --height=180` | The strip pin. `wire.1` is 55 glyphs, so this plate is 61 cells wide; the 56-glyph rows (`flame.1`, `flame.3`, `trade.1`) make the widest plate the sheet can build, 62 cells in a 64-cell frame -- a cell of margin off each edge, two cells of gutter inside each keyline -- and the test pins them at that width rather than a drive. Seated with five of the smallest window's seven spare pixels above its keyline and two below. |

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

What is declared and left: the top rung at 320x180 (fifteen boxes, no
drive) sits on its floor with no clearance to spend. (The squarer windows
that clipped a 56-glyph row -- 1440x1080, 720x720 -- step their register
down since the sixth pass; see below.)

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
  ease, the words dim, and the rails drop to a quarter -- bone at half is
  still brighter than amber at half, and the frame outshone the words it
  framed. A whole minor unit of pad while stepped down, so the glyphs'
  shadows stay in the field. Same for the voice row.
- **The tick row breathes.** Set solid under the prose it read as a second
  line of the docker's in the system's voice. It sits one unit low in its
  row: two of air above it and two between its feet and the rule, the
  font's own drop shadow counted (the fifth pass set it two low and the
  shadow, invisible on the slab but drawn, sat on the rule).
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
- **The cause in the picture, and Cull's corner.** `standBackFrom` wants a
  clear line now -- every tile between the spot and the man floor, nobody
  of the Gull's roster on it -- before it settles for the sim's own line of
  sight, which sees over a table and through a standing man. It does not
  see the ward's bodies that drink in the Gull, and Cull's corner is theirs.
  Four gates went on it: from the stand-back's three tiles the prompt names
  a patron on the west approach (trade) or the hood on his own table (law),
  which is nearer than he is; a ring of spots walked round him and read off
  the prompt spent the plate's whole hold finding none; from a tile off him
  the prompt does name him, and the picture is the wall behind his head,
  eye dipped or not, because his stool stands against it. So the drives
  stand back as they always did, dip the eye a touch toward a seated man
  (`faceFromHere`), and **the summary prints what the prompt named at the
  shutter (`aim="..."`) on every track** -- `THE STRONGBOX`, `The Gilded
  Gull`, `Father Maell`, and on trade and law the patron and the hood,
  honestly. Putting Cull himself under the reticle with the room behind
  him wants his stool moved off the wall or the frame taken from his side
  of the table, which is the Gull's own lane. The flame drive pitches the
  eye down once the last read has tipped the track; its back room is a
  tile deep against the anchor's wall and the frame is still mostly that
  wall -- declared, a Mission lane's geometry. The wire drive stays as it
  is: the box is under the reticle at two in the morning with the lanterns
  out, and a burglary is dark.
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

## Sixth pass, 2026-09-26

The fifth pass's director scored 8 and the player 7; the plate's choreography
was closed on both lists, and what was left was polish and the world around
the card. Their lists, taken whole:

- **The rails under the words.** Bone at half is still brighter than amber
  at half: the stood-down rows' frames outshone the words they framed by
  half as much again. The rails drop to a quarter now, and a stood-down row
  takes a whole minor unit of pad so the glyphs' shadows stay in the field
  instead of hanging under the lower rail as nicks on the wall.
- **The tick's feet.** Two units low, the tick's drop shadow -- invisible on
  the slab, but drawn -- sat on the bottom rule. One unit low now: two of
  air above, two between its feet and the rule.
- **The voice row keeps the alert row's clock.** Cull's farewell and a
  passer-by's bark lived their whole life dimmed under the plate and were
  gone before the lift. The voice row's hold waits under the plate now and
  is capped at two seconds after it, exactly as the alert row's.
- **A moment is not news.** `OVER 3 TILES.` came back whole six seconds
  after the leap. The take off a box or a watchman's word is news and is
  held; a leap's length or a ledge mantled is a moment, and its clock runs
  under the plate (`messageHeld_`), so it is gone before the lift.
- **The register follows the width too.** 1440x1080 and 720x720 clipped the
  row's last beat at the height's own register. `rungPlateMetric` steps a
  scale down until the frame holds the sheet's 64 cells -- the same move
  640x360 makes upward -- and a case pins both windows whole.
- **The slab leaves ahead of the words.** Over the evening sky the lift was
  a fogged pane of glass with grey words on it for a dozen steps; the slab's
  alpha is the fade squared now, so the words are the last thing left.
- **The law drive's eye comes up, and the hood is still there.** From the
  stand-back the dip put the hood under the reticle: a red TAKE prompt under
  the Watch's own card. The eye is level-and-a-touch now, and the summary
  still says `aim="HOOD"`: the hood is not on his table but hung at head
  height on the line to him, so no pitch clears it from that spot. Declared;
  it is the same corner as the trade's patron, and the Gull's own lane.
- **The summary says whether the rung rose.** `rose=yes|no` on every run,
  and `reason="Cull is not at his table at this hour"` on a daytime trade
  or law, so `found=yes plate=down` no longer reads as a success.
- **Four rows, for the redline.** `flame.1` owns the anonymity now -- *In
  the Mission's book already. Not your face, your work.* -- so Father
  Maell's "whoever you are" three seconds later is the row's own joke and
  not the world answering it back; `wire.4`, `law.2` and `flame.2` lose the
  "That is" / "name" / "first" tics the director counted:

  | key | was | is |
  |---|---|---|
  | `legend.flame.1` | You are in the Mission's book. Nobody asked you first. | In the Mission's book already. Not your face, your work. |
  | `legend.wire.4` | Your hands are never seen. That is all the name means. | Nobody saw your hands. The ward found a word for that. |
  | `legend.law.2` | Cull calls you by name across the room. On purpose. | Cull calls out to you across the room. On purpose. |
  | `legend.flame.2` | The priests nod to you first now. The dockers saw it. | The priests nod your way now. The dockers saw it. |

Declared and left: the bell is the harbour's own heavy strike on the World
bus and no frame can hear it -- whether a rung sounds like the harbour and
not like you wants a listen on the quay (the player's note); `wire.1`'s
"these days" after the first box of the night is licence; the trade frame's
patron and the flame's wall are the fifth pass's declared corners still; a
pre-rise shutter would put the plate's rise on the record and is tooling.

## Seventh pass, 2026-09-26

The sixth pass scored 8 (director), 8 (player) and 5 (auditor): the claims
had drifted off the code in a dozen places, and two rules had holes a
player would never see and a reader would. Everything below is on the gate.

- **A watchman's demand is never capped.** Before, both caps applied to
  the demand (it is news, so `messageHeld_` was true and it was capped and
  stepped down like the take off a box), while a moment that outlived the
  lift was uncapped and swelled back to the glance size. Now EVERY line still on the row when the
  plate goes is capped at two seconds and finishes small, and the demand is
  the one named exception (`!messageDemand_` at both sites, and
  `alertStepsDownNow` says so). Pinned through the session: a blade drawn on
  a patron in Cull's sight at eleven, his halt on the row the step the plate
  goes, the row at the glance size, its clock past the cap
  (`alertStepsLeftNow`, added for the pin).
- **The fall is news again.** "DOWN 2 LEVELS. - 5M - 50 HURT" is the part
  of a fall a player acts on; the sixth pass had made it a moment with the
  leap and the mantle. Held under the plate like the take off a box.
- **Dismissed is gone.** A card dismissed by a three-step page eased back to
  three quarters and lifted out after it, re-arming every stand-down. The
  dismissal snaps the lift to nothing; the one-plate case pins a page opened
  three steps and closed, and the card never comes back.
- **The rows step down to the plate's register, not the HUD's.** On a
  square window (1440x1080, 720x720) the plate is a step under the minor
  scale and the alert and voice rows were a tier above it while "stepped
  down". `HudState::alertStepScale` carries the plate's own scale; the
  drawn-amber twin runs at 1440x1080 too and pins six rows of the plate's
  scale, and pins the rails' luminance under the words'.
- **The slab reaches its right corner.** The right-hand diamonds fill their
  cell's four columns and the slab stopped a unit short of them. One unit
  wider; the geometry loop reads the slab's edges off pixels at every size.
- **The ink holds through the first half of the lift.** The slab is the
  fade squared (sixth pass); the words, rules and head block now take twice
  the fade, clamped, so the card leaves as words on a thinning slab and not
  as grey words on a pane of glass over the sky.
- **One bell, always the same.** The five heavy strikes run from 1.74 s to
  0.30 s and the rung drew one at random; a five-second card opening on a
  tink one time in five had no voice. `impactBell_heavy_001` every time.
- **The reason is read, not assumed.** `reason=` said "Cull is not at his
  table at this hour" whenever a trade or law run rose nothing, whatever the
  cause. It is set only when Watchman Cull is not in the Gull's roster when
  the line starts, and the drive case runs both tracks at one in the
  afternoon and reads it back.
- **`aim=` is the interact target.** `Session::aimSubjectNow` is what the E
  key would act on from where the body stands, resolved by position and
  reach, never by the reticle's ray. The sixth pass's story -- the hood
  "hung at head height on the line to him" -- was wrong: the hood is a stand
  item one tile south of the stand-back tile, in reach, and no pitch changes
  what the prompt names. `faceFromHere` (which stepped the sim to read the
  prompt back) is `dipToward` now, a pure turn used by the trade; the law
  stands back and sets its pitch. The dead `table` ending of the bounty line
  and the unused `under` parameter of the drive case are gone.
- **Three rows.** `wire.3` and `wire.4` shared the "That is" tic with
  `wire.1`, and `law.3`'s "Even sober ones" read as a leftover:

  | key | was | is |
  |---|---|---|
  | `legend.wire.3` | Finch pours before you ask. That is a robber's welcome. | Finch pours before you ask. Robbers get poured for. |
  | `legend.wire.4` | Nobody saw your hands. The ward found a word for that. | The ward never saw your hands. It found a word anyway. |
  | `legend.law.3` | Voices drop when you pass. Even sober ones. | Voices drop when you pass. The sober ones drop lowest. |

- **The claims match the code.** `bouncerWarned()` is the one copy of the
  warning test (two inline copies replaced); "three Watch sites" is four;
  `alertFadeNow` is documented as the ink's alpha as drawn; "half" is a
  quarter wherever the rails are described; the tick table in the code
  comment says which windows drop it (960x540 holds both, 640x360 and
  1920x1080 drop it topped, 320x180 always, 3840x2160 has no row); the
  320x180 seat is five above and two below, pinned as 36 and 31; `wire.1`
  is 55 glyphs and 61 cells, and the 56-glyph rows are `flame.1`, `flame.3`
  and `trade.1`; the trade and the law stand back after their rung and the
  drive rule says so. (This bullet first said the default settle was
  sixteen steps; it is 240, `kCaptureRestSteps` -- the eighth pass.)

Declared and left: `standBackFrom`'s strict clear-line pass (fifth pass)
serves five other drives that were not re-shot for it; the flame's frame is
the back room's wall a tile off the anchor; the trade's patron and the law's
hood are the Gull's own corner; no frame can hear the bell.

## Eighth pass, 2026-09-27

The seventh pass scored 8 (director), 8 (player) and 6 (auditor). The
auditor: the record had a new false number in the very bullet that said the
claims matched the code, the lift window had two holes of the same classes
the seventh pass closed, and about eight comments contradicted the code.
The director and the player: the shadows carried the lift, three heads sat
half a cell off the card's axis, the stood-down rows' shadows dented their
rail, a passer-by's bark was held under the plate, and on square windows a
capped bark swelled when the plate left. Everything below is on the gate.

- **The lift window is closed, both ways.** A line said DURING the 24-step
  lift had its cap cleared by `say()` and swelled to the glance size the
  frame the card left; the cap fires once more on the card's last frame
  (`rungPlateWasOnScreen_`, the edge on the drawn fade), the demand excepted
  as everywhere. And a page or a warning over a LIFTING card dismisses it
  (the outranking test reads `rungPlateAnim_.value() > 0` too), where before
  the ease ran on unseen and a short page handed the card back at full ink.
  Both pinned (a punch mid-lift; a three-step page six steps into the lift).
- **The default settle is 240 steps, not sixteen.** `kCaptureRestSteps`,
  four seconds into the five-second hold: the auditor proved it on the exe
  (a default-settle shot is byte-identical to `--settle-steps=240`). The
  seventh pass's bullet, the frames section and the drive doc say so now,
  and "about a second of the hold" for the trade's and the law's stand-back
  is about twenty-five steps.
- **The history of the demand is told straight.** The seventh pass said the
  old caps "skipped the demand"; they did not -- the demand was news and was
  capped and stepped down. Corrected in place.
- **The demand case proves its name.** It requires the plate live before
  the landing step and its hold still running ON the landing step (the
  plate is cut the step after the demand is said, so the hold did not
  expire under the loop; the ninth pass added that require, and the tenth
  rules out a bouncer's warning by name, since both come out of the same
  tick) -- requires Cull on the row, and pins the hold exactly: read off
  the row the step it lands, past the cap, counted down one a step.
- **The shadows leave with the slab.** The plate's text drew its drop
  shadows at the ink's alpha while the slab thinned as the fade squared, so
  from the lift's midpoint the letters were black outlines round grey words
  on a pane (the director: at settle 308 the prose 2.02:1 and its shadow
  2.96:1). `drawText` and `drawCellText` take the shadow's own alpha now and
  the plate draws its shadows at the slab's curve -- its frame's too: the
  keyline's dashes, bars and diamonds shadowed at the ink's alpha as well
  (the pin caught them, gate 29), so `FrameStyle::shadowAlpha` carries the
  slab's alpha through `drawRuleRun` and `drawMotif`, every other frame
  keeping the font's three quarters. Pinned at fade 1, 0.5 and 0.25 on a sky
  (the brightest prose pixel at the midpoint equal to the up card's; the slab
  lighter by more than a tenth of luminance there -- a quarter of its alpha
  by the curve, not pinned as that number; the darkest pixel in the prose
  rows nowhere near the up card's shadow).
- **Centred to the half cell.** A head block or tick row whose spare cells
  were odd sat half a cell left of the card's axis (5 px at 960, 12.5 at
  1920, on three of five tracks). Rows with an odd spare shift right half a
  cell; pinned on pixels for all five heads at 960 (the ninth pass).
- **The stood-down rows keep their rail clean.** A whole unit of pad put the
  glyphs' shadow row ON the rail (the director measured the dent under every
  letter, at every size); the pad is the shadow's unit plus the rail's
  thickness now, and the band's pad above the row grows with it.
- **A passer-by's bark is a moment.** `holdClock` held every voice line under
  the plate, so Vidar's "ROPE COMING THROUGH" finished seven seconds after
  the rope had gone; an event line (a farewell, a react, the Watch) is held,
  a pass or overheard bark runs.
- **The voice row has its own stand-down.** It took the plate's register
  only while its ink was dimmed, so on a square window a capped bark swelled
  back to the minor scale when the plate left; `HudState::voiceStepsDown`
  (the plate on screen, or this row's line capped under it:
  `StreetVoice::capped`) drives its register, field, rails and pad, the
  alert row's own rule.
- **`drawTextPlate` is declared and pinned.** Its side rails run between the
  top and bottom rails (seventh pass, in a commit message only); the change
  is HUD-wide -- every plate-backed row, the quick-slot strip at 0.80 of its
  fade, the world's signs by distance -- and pixel-identical at full alpha.
  A case draws a plate at a quarter and at one and reads the corner against
  the rails.
- **Comments that contradicted the code**, each rewritten: the warning
  test "duplicated rather than shared" above `bouncerWarned()`; `alertFade`
  "halved" by Session; the alert row "starts from the minor register"; the
  rules block's "(the minor scale"; `aimSubjectNow` "under the reticle";
  `messageCapped_` (also set on dismissal and on the last frame);
  `rungPlateOutranked` (the demand and the page tail); the test header's
  320x180 seat; the dead `style.alpha` store; "at three sizes" over a
  four-size loop; the toast case's title.

Declared and left, with reasons:
- **Square windows: the register step, not a wrap.** Both critics: at
  720x720 and 1440x1080 the card is the smallest type on screen. Wrapping the
  prose to two rows at the HUD's size drops the tick and breaks the sheet's
  one-line rule at those windows; stepping the prompt and stat stack down
  for the hold touches every HUD row. Either is a design call for the HUD
  lane, taken with the 640x360 glance rule. A chip.
- **The frames over their cause.** The flame card over the Mission's wall,
  the law's red HOOD / TAKE, the trade's reticle on Wick Hempson: the fifth
  pass walked a ring round Cull's stool and found no tile where the prompt
  names him without the wall; the hood is a stand item in reach. Framing
  the talk first so `aimSubjectNow()` is Cull (the street line's pattern)
  is the player's ask and a drive rewrite; a chip.
- **A once-a-game line can be lost.** A page or a talk in the card's first
  frames dismisses it and the row is shown nowhere else. Re-queueing on an
  early dismissal, or the row on the Character tile, is a feature; a chip.
- **A mantle onto a lower ledge that hurts is still a moment**: the hurt is
  on the health bar, the line is a move's length, and it runs.
- **Unpinned by design**: the bell's one sample (content), the cue firing on
  show (audio), the 3840x2160 tick row (no such frame is shot), the drop
  line as news and the leap and mantle as moments (no roof fixture in this
  suite: none of the three is pinned), the alert row's stood-down pad (a
  whole unit would pass the drawn-amber twin's rails pin; the director
  measured the rails clean -- the voice row's pad is pinned since the
  eleventh pass: its pin reads the rail a unit and a rail above the
  glyphs, and a whole-unit pad fails it).
- The side bars a column off centre on the diamonds, the tick's foot a unit
  tighter than the head: the panel vocabulary's, not this card's.
- The rows' shared shape, and the world made to honour more of what the
  rows promise (the player's M2): the sheet's next redline is a chip. One
  row could not wait: `wire.3` had Finch pouring, and Finch is the ward's
  fence, not a barman (`skyrunner_tenant.json`'s "the somebody is Finch"
  and the Fence topic in `dialogue.cpp`; the director). It pays off his
  work now:

  | key | was | is |
  |---|---|---|
  | `legend.wire.3` | Finch pours before you ask. Robbers get poured for. | Finch pays before the sack is open. Robber's rates. |

## Ninth pass, 2026-09-27

The eighth pass scored 8 (director), 8 (player) and 7 (auditor). The
auditor: the voice row's stand-down was keyed on the line being up, so a
capped bark swelled back to the minor scale for the eight frames of its
fade (seen on the exe at 720x720); the voice row's new pad put the next band
row's shadow on its top rail; the demand case could not rule out a warning
in the same step; four lines of the record were stale or over-claimed; the
centring and the moment clock were unpinned and undeclared. The director: the
dim tick row embossed for six steps of the lift. The player: at 320x180 a
level that folded into a card with no tick row was told nowhere.

- **The voice row stays stood down through its fade.** `StreetVoice::capped`
  reads the line being present, not up -- the row clears it, a new line
  clears it -- and `standDown` clears it too. Pinned twice: the emitter's
  own state through a cap, two steps and the clear; and the HUD on pixels
  at 720x720 through a half fade, the name's glyphs six rows of the plate's
  scale stood down and six of the minor scale otherwise. (The tenth pass
  closed the seam the auditor found between the two: a cap now marks
  whatever line is present, so a bark whose hold ran out inside the lift
  stays small through its fade -- pinned at the emitter only. On the
  session the twelfth pass pins its neighbours (the capped state lasting
  past a line's hold, the last-frame cap on a line said in the lift), not
  this seam: that needs a line run out on its own clock in the lift's last
  seven or so steps -- the row clears eight steps after a line's hold runs
  out, so an earlier run-out is gone before the last frame -- with nothing
  ready to take its row. In the Gull the waiting overheard line takes the
  first pass bark's row on the step it runs out; a later pass bark, after
  the pass cooldown and while the overheard channel is still cooling,
  might serve (the auditor's reckoning) and was not tried.)
- **The voice row buys its pad above.** The rail plus a minor unit of air,
  in pixels, the alert row's way. Pinned on pixels with a hands row above
  it, read across the plate's whole rail (the eleventh pass; the tenth's
  scan read the name's columns alone, where the hands' shadow never falls,
  and passed without the pad).
- **The dim rows' shadows go one power faster.** The tick and the top row
  are dim ink; their shadows are the cube of the fade where the prose's is
  the square, so on the greying slab the tick reads as dim letters and not
  as a raised edge (the director: by hue and by eye at every step; by
  luminance alone the shadow still edges the ink by a few levels near
  settle 308). Pinned: the tick row's darkest pixel lighter than the
  prose's at the midpoint.
- **The tick folds in only where the card has a row for it.**
  `rungPlateTickFits` asks the layout at the session's frame size before
  the fold; at 320x180 (and a topped card at 640x360 or 1920x1080) the level
  queues as a toast behind the plate. Pinned at 320x180.
- **The demand case says what it proves.** `rungPlateWanted()` on the
  landing step: the plate's own hold had not expired under the loop (the
  hold is cut the step after the demand is said, since the plate's step
  runs before the sim's tick). A bouncer's warning in the same second comes
  out of that same tick, so it is ruled out by name instead: no warning is
  up on the landing step (the tenth pass; the ninth's wording had the
  warning cutting the hold "at once", which it does not).
- **The record corrected**: the drive doc's "a second of the hold"; the
  `RungLineResult::aim` doc; the leap and mantle moments are not pinned;
  the slab's "quarter" is not the pin's number; Finch's fence is cited from
  where the game says it.
- **Pinned**: the head block on the card's axis for all five heads; a
  farewell holding under the plate; and -- the twelfth pass, after three
  passes of false reasons for not -- a pass bark RUNNING under the plate,
  at the emitter and on the session, by the step.
- **Declared and left**: the head block's height depends on the card's row
  count (the director: a card with a tick row seats its gold three pixels
  higher at 960 and 29 at 1920, where the card grows upward rather than
  down; two queued cards of different depth would make the gold hop -- the
  seat is the card's, and anchoring the head instead is a seat rule for
  the next pass); the full stop's weight pulling the prose rows a unit
  left of the slab's axis (two pixels at 960 on wire, roofs, law and
  trade, one on flame; about five at 1920, two and a half on flame), the
  slab itself half a unit
  left of the compass above it; the chips above unchanged.

## Tenth pass, 2026-09-27

The ninth pass scored 8 (director), 8 (player) and 7 (auditor). The
auditor's four: a cap keyed on the hold un-capped a line that had run out
inside the lift, so its fade swelled (the voice row and, in the same shape,
the alert row's moments); the demand case's stated reason was false; the
"never out-contrast" claim was false by luminance; two "unpinned" reasons
were false. Everything below is on the gate; no frame changed.

- **A cap marks whatever is present.** `StreetVoice::capHold` sets capped
  for any line on the row, exhausted or not; the alert row's three caps set
  `messageCapped_` for a line with steps left or a fade still running.
  Pinned at the emitter (a run-out farewell, capped, stays capped until the
  row clears), and there only: on the session (`voiceStepsDownNow`, public
  and what drawFrame hands the HUD) the twelfth pass pins the capped state
  lasting past a line's hold and the last-frame cap on a line said in the
  lift, but no cap there lands on a line already out of hold (the tenth
  pass's parenthetical above says why). The alert row's fade clause
  is UNPINNED: only a moment (a leap's or a mantle's line, whose clock runs
  under the plate) can run out inside the lift, and this suite has no roof
  fixture (test_roofrun's climb fixture could supply one).
- **The demand case rules out a warning by name** and its comment says what
  the require proves.
- **The tick row's shadow pinned against the prose's** at the midpoint; the
  claim rewritten to what the frames show.
- **The voice pad pinned on pixels** with a hands row above it (read
  across the whole rail since the eleventh pass). The pass bark's clock
  is pinned by the twelfth pass; this pass's reason for not pinning it
  was false, and so were the eleventh's two.
- Also: the tick-fits doc names 3840x2160; the drive doc's wrapped line;
  the hud.cpp slot comment; the Character tile's "1 RUNGS" (the player's
  side note, one line, this tile being the rung system's own) -- at
  320x180 the tile's own clip still cuts the noun ("LIGHT FINGERS  1.",
  `--rung=wire --character --width=320 --height=180 --settle-steps=40`);
  clipping the title before the count is the tile's, a chip.
- Declared and left as before: the square windows, the frames over their
  cause, the once-a-game row lost to an early page (the player: the fix is
  small and the Character tile's notes affordance is the place -- a chip),
  the head's seat with the row count, the prose's optical centre, the
  rows' shape.

## Eleventh pass, 2026-09-27

The tenth pass scored 8 (director), 8 (player) and 7 (auditor). Tests and
docs only; no drawing changed, no frame changed. The auditor: the Gull case
cited for the lift window had its bark outliving the plate by ninety steps;
the pad pin scanned the name's columns where the hands row's shadow never
falls; the pass bark was declared unpinnable a third time while its hold is
arithmetic on public constants; the alert row's fade clause sat under
"Pinned"; the eighth-pass demand bullet still carried the false warning
clause; the record contradicted itself on the pad; two comments were
ragged. The director: two of his round-seven items were not declared and
two numbers were understated.

- **The Gull case, as this pass left it, was wrong about its own runs**
  (the auditor, round nine; the twelfth pass corrects it). It said the
  room replaced the bark before its hold was out, and no ambient line can:
  another pass line waits `kPassCooldownSteps`, longer than any hold, and
  an overheard line ranks below a pass line (a farewell, a react, the
  Watch, a talk, the same speaker's line landing on the alert row, or a
  placement could, and none comes in that idle fixture). The run
  that left at 419 was the plate's hold-end cap (the 300-step hold plus
  the 120-step cap, less one); the run that left at 314 was the bark's own
  clock running out inside the lift, the case having taken the step it
  was said as five later than it was. And its "last-frame cap on a
  present line" passed with that cap reverted.
- **The pad pin reads the whole rail**, first bone pixel to last, and
  requires the rail wider than the name.
- **The alert row's fade clause declared unpinned** with its reason.
- **The record**: the eighth-pass demand bullet, the pad's two sentences,
  the pass-bark sentence, `messageCapped_`'s doc, the two wrapped comments.
- **Declared, from the player's round nine**: a HUD top-row collision on
  square and 4:3 windows, not this lane's and not yet tracked anywhere.
  At 720x720 four of the five rung frames show the top row broken. The
  roofs': the stealth row over the compass ("HIDDEN DARK 6 QUIET"), the
  place line cut to "S" (`--rung=roofs --width=720 --height=720
  --settle-steps=0`). The trade's changes shape with its coin readout:
  while "52 C" is up (settle 0 to 120) the stealth row straddles the
  box's bottom rail, the line cut to "SW 48"; once it has gone (by settle
  200, the card still up, and at 620) the stealth row moves up over the
  compass's "SE" and the line stays cut. The wire's place line is missing;
  the law's stealth row sits on the place line's row, the line cut. The
  flame's is whole: no stealth row is up. At 1440x1080 the roofs' HIDDEN
  sits on the compass box's right rail and its place line drops the name
  ("S 45  BELOW"); wire shows the missing place line; law and trade the
  cut one ("SW 48  MISSIO.."), the trade's stealth row clear of the box by
  about thirty pixels throughout; the flame's is whole again. A chip
  for the HUD lane, which lane/hud-cells (be82b92b, unmerged) may already
  move: it makes the minor scale min(h/240, w/425) and rewrites this
  card's `layoutFor` on `panelMetric(width, height)`, so the collision and
  the square-window register both want re-judging on the merged exe.
- **Declared, from the director's round seven**: the dim ink darker than
  the paling slab in the lift's last four steps (a polarity flip at 4x
  nobody reads at speed; fading the dim ink on the slab's curve is one
  pass); the 320x180 toast's chrome (its capitals under its top rail, the
  last row's shadow on the bottom rail: `drawSkillToast`'s one-unit pad,
  the HUD toast's own, now where this feature's level lands at 320); the
  seat's hop and the prose's centre with their measured numbers.

## Twelfth pass, 2026-09-29

The eleventh pass scored 8 (director), 8 (player) and 7 (auditor). Tests
and docs only again; no drawing changed, no frame changed. The auditor:
the eleventh pass swapped one false reason for another -- the Gull case's
runs were the bark's own clock and the plate's cap, never the room -- and
one of its pins passed with what it named reverted.

- **A pass bark leaves by arithmetic.** The emitter's clock is the body's
  step count (`VoiceScene::step`); a row's hold is `kVoiceHoldMinSteps`
  plus `kVoiceHoldPerGlyphSteps` a glyph, to `kVoiceHoldMaxSteps`; and no
  ambient line takes a pass line's row while it is up (a farewell, a
  react, the Watch, a talk, the same speaker's line landing on the alert
  row, or a placement could; none comes in this idle fixture, and the
  checks would fail if one did). So the Gull case runs the same bark
  three times and pins the step it leaves, exactly since the thirteenth
  pass (within two before it):
  - idled to run out mid-hold, it leaves on its own clock, before the
    hold's end: the pass clock runs under the plate (an `isEvent` revert
    would freeze it from the rise through the lift, and it would leave 119
    steps after the last frame instead);
  - idled to run out inside the lift, it leaves on its own clock between
    the hold's end and the last frame; the room's next line goes up in
    the lift, and it leaves on the plate's LAST-FRAME cap -- 120 steps
    less one after the last frame, before its own clock would have -- with
    the row stood down on the last frame itself (the thirteenth pass) and
    every step through its fade until it clears: the last-frame cap on a
    line said during the lift, and the capped state lasting past a line's
    hold, pinned on the session. Not the tenth pass's seam (a cap landing
    on a line already out of hold): no cap in these runs lands on one;
  - not idled, it outlives the plate and leaves on the HOLD-END cap, 120
    steps less one after the hold's end, before its own clock would have.
- **The pass clock at the emitter too**: a standalone emitter over the
  Gull's room, the plate's clock set throughout, runs a pass line out on
  exactly its hold.
- **The dead conditional is gone.** (The case's name still over-claimed;
  the thirteenth pass renames it.)
- **The record**: every sentence that carried the eleventh pass's reason
  (the tenth-pass parenthetical and cap bullet, the ninth-pass pinned
  list, the tenth-pass pad line, the eleventh-pass Gull bullet, the test
  header); the eighth pass's pad line (the voice row's pad is pinned); the
  HUD chip's wording; the drive doc's ragged comment.

## Thirteenth pass, 2026-09-29

The twelfth pass scored 8 (director), 8 (player) and 7 (auditor). Tests,
names and docs only; no drawing changed, no frame changed. The auditor:
the record claimed the tenth pass's seam pinned on the session and it is
not -- no cap in the Gull runs lands on a line already out of hold, so
reverting that fix passes every session case (the emitter's fails);
"nothing takes a pass line's row"
over-claimed (a farewell, a react, the Watch or a talk can); the HUD
chip's frames were wrong for the flame and the trade (the director and
the player too); the Gull case's name had a pass bark leaving on the
last-frame cap (the next line does); its comment had a drift that never
runs; and no case looked at the last frame itself, so a last-frame cap
moved a step late passed everything.

- **The seam sentences say what is pinned.** The tenth pass's fix (a cap
  marking an exhausted line) at the emitter only; on the session, the
  capped state lasting past a hold and the last-frame cap on a line said
  in the lift. Declared, not pinned on the session: a cap landing on an
  exhausted line -- it needs a line run out on its own clock in the lift's
  last seven or so steps (the row clears eight steps after a line's hold
  runs out) with nothing ready to take its row; in the Gull the waiting
  overheard line takes the first pass bark's row on the step it runs out,
  and a later pass bark was not tried.
- **"No ambient line"** takes a pass line's row, wherever it said
  "nothing".
- **The last frame itself is looked at.** The Gull case checks the voice
  row stood down on the step the plate's drawn fade reaches nought; the
  alert row's lift-window case checks the capped hold and the stand-down
  on that step before stepping past it.
- **The leaving steps are pinned exactly**, not within two: the runs land
  on the arithmetic every time.
- **The Gull case's name and comment** say what it pins and how the bark
  comes (on `wake()`'s first step; the drift is a guard that never runs).
- **The HUD chip** describes each frame as shot, the trade's shape changing
  with its coin readout and the flame's whole; the prose-centring clause
  names the roofs.

## Fourteenth pass, 2026-09-29

The thirteenth pass scored 8 (director), 8 (player) and 8 (auditor): the
bar. Docs and comments only, from the critics' own lists: the thirteenth
section said reverting the tenth pass's fix passes every case, when the
emitter case fails (every session case passes); the reason the session
cannot reach that seam left out the timing (the line has to run out in
the lift's last seven or so steps, since the row clears eight after);
`voiceStepsDownNow`'s doc still promised the seam; the list of what can
end a pass line early left out the same speaker's line landing on the
alert row and a placement; the prose-centring clause's 1920 figure is two
and a half on the flame; and the trade's early shape straddles the
compass box's bottom rail, its place line staying cut after the coin
readout goes.

## Not in this folder yet

- A frame of the plate dismissed by a warning, or queued behind a page. Those
  are timing, not a picture. `test_rung_plate.cpp` pins both, plus the toast
  waiting behind the plate and the plate never moving a hash.
- A frame of `legend.top`. That's fifteen boxes or a closed case plus more,
  and no drive is that long on purpose. The row is pinned to fit and to
  draw, and the top-rung case in the tests reads it back off the plate.
