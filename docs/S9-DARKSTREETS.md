# S9 — Stealth, thievery, lockpicking

What changed, in the order it was built. Two halves: the four S8 review findings, then the sprint.

Gate: `docker compose run --rm --build build` → **exit 0**, `ctest knows about 442 tests (floor: 442)`,
`100% tests passed, 0 tests failed out of 442`, `[doctest] assertions: 476081`, twin-run gate `=== PASS ===`.
Windows half: `scripts\verify-windows.ps1` → **exit 0**, both reports byte-for-byte identical, gate
stamp matches the tree.

---

## Part one — the S8 findings

### The ward's day never turned in the windowed game

- **Was:** `Ward::tick` counts one second per engine tick and does a day's work at 86,400 of them.
  The windowed game ticks the engine once per sixty movement steps — and every *skip* in
  `render::Session` (sleeping in a rented bed, a night in a cell, the blackout after a beating,
  a scripted capture reaching a named hour) moved the **tavern's** clock and simulated none of the
  seconds it jumped. The ward never heard about any of it. The S8 review's probe: ten slept nights
  plus ten minutes of continuous play left `stats().days` at **0**. S7's finding #8 — 3,300 lines of
  economy the player cannot see — was therefore still open two sprints later.
- **Now:** the *world's* day is the authority and the ward follows it. `Ward::advanceToDay(worldDay)`
  runs `endOfDay()` once per day crossed; `Ward::tick` is expressed through it
  (`advanceToDay(seconds_ / kSecondsPerDay)`), so with nothing else driving the ward the behaviour is
  bit-for-bit what it was and `test_compound`'s "a day off the engine's clock is the same day as a
  day off endOfDay" bridge case is untouched. `Session::syncWardToCalendar()` calls it with
  `Tavern::dayNumber()` after every step, every `skipToHour`, every `restHere` and every
  `settleDefeat`. It is monotonic and idempotent, so the engine's own counter and the tavern's
  calendar cannot double-count each other.
- **The two tautologies are gone.** `test_tavern_render.cpp` asserted `ward().day() >= dayBefore` and
  `ward().stats().days >= 0` on monotonic `int64`s that start at zero. It now asserts
  `day() > dayBefore`, `stats().days > 0` and `stats().harvests > harvestsBefore` after two
  twelve-hour skips — all three of which fail on the old code.

### `NemesisBook::of()` lost a rival whose roster id changed

- **Was:** `of()` scanned `rivals_` and `break`ed on the first `actorId >` the one asked for. That is
  only correct while the list is sorted by id — and `entryFor()` reassigns `actorId` in place,
  without re-sorting, on exactly the path `nemesis.hpp` advertises as *"the persistent-ward door,
  left open on purpose"*. One rival past the reassigned one and the book returned `nullptr` for a man
  it has.
- **Now:** the early `break` is gone. There are a handful of rivals in a ward; a linear scan over them
  is free, and a sort that has to be remembered on every write is not.
- **Case:** *a rival keeps his record when the roster hands him a different id* — two rivals entered in
  ascending id order, then the first re-enters as an id past the second. The assertion that used to
  read `CHECK( nullptr != nullptr )` is `REQUIRE(book.of(11) != nullptr)`.

### A founded chapter's parent faction was ignored

- **Was:** `ChapterRaws::forTrade`'s second pass matched on **trade alone**. Sella Brinewall and Kled
  Tarbeck are both dockhands whose trade is `streetwise`; the first `streetwise` row in
  `chapters.json` is The Chandlers' Row, which the owner's file says belongs to the **merchants**.
  `recordDefeat` then booked her influence (`shiftInfluence`) and her toll (`tollPercent`) against
  `entry.faction` — the dockhands. A hand founded a merchants' house and taxed the quay gang for it.
- **Now:** the trade-only pass requires the chapter's faction to be empty, or the founder's faction to
  be empty, or the two to agree. What falls through reaches the faction-only pass, which is the right
  answer anyway: you found a house of your own guild.
- **Case:** the existing *a guild is a guild OF something* now also asserts that
  `forTrade("streetwise", "dockhands")` and `forTrade("streetwise", "merchants")` are different
  houses of the right factions, and sweeps every (faction, trade) pair requiring
  `chapters.at(found)->faction == guild`.

### `test_nemesis` asserted the constant it was meant to detect changes in

- **Was:** `CHECK(influence(dockhands) == influenceBefore + kInfluencePerWin)`. Zero the constant and
  the case stays green while a win stops moving the ward at all.
- **Now:** three assertions — the influence strictly rose, the size of the rise is `kInfluencePerWin`,
  and `kInfluencePerWin > 0`. Any of the three can go red on its own.

### And one the review did not find: the gate's own registration check could not be trusted

Every *"is this case registered"* line in `docker/build.Dockerfile` was
`printf '%s\n' "$ctest_list" | grep -qF "$case"`. **`grep -q` exits the moment it matches.** With
`set -o pipefail` on and a test list that has grown to four hundred lines, `printf` is still writing
when grep goes away, takes `SIGPIPE`, and the pipeline reports a **registered** case as missing.

It failed this sprint's gate on *"the stealth line is one row on an edge"*, sitting at #265 of 442 —
a case that was plainly in the list printed four lines above the FATAL. It would have picked a
different victim every time the suite grew. All sixteen are shell glob matches against the variable
now: no process, no pipe, no race.

---

## Part two — being unseen

### Light crossed the line, in integers

`render/lighting.hpp` opens with *"there is no stealth system"*, and it was right: light was float,
presentation, computed once at load, and read by nothing that could change the world. Meanwhile the
only thing deciding whether anybody **saw** you was `Tavern::witnessCount` — eight tiles, same floor,
a sight line. Standing in a lit taproom at noon and crouching in the dark of the snug at four in the
morning were, to the simulation, the same act.

`sim/stealth.hpp` builds a **second field over the same authored lamps**, in integers, on the
renderer's own curve — radius `4 + (lum-8)/12` tiles clamped to 3.5..5.5, peak `0.55 + 0.45*lum/26`,
falloff `P*(1-(d/R)^2)^2`, overlaps combined by saturating union. Two fields over one set of lamps is
a real cost and it is the honest one: the alternatives are floats in simulation state or a simulation
that reads the renderer, and this project has a ruling about both.

`Tavern::simLights()` derives it from the same `houseLights()` the renderer draws, so a flame cannot
be bright to the eye and dark to the law. The hearth is out at three in the morning; the lanterns and
the table candles only burn while the doors are open. **Five in the morning, the Gull's light list is
empty and the room is black.**

### One rule, six clauses, and every one can go red on its own

| moves the read | | moves the cover | |
|---|---|---|---|
| `kNoticeBase` | 60 at zero distance | `kCoverBase` | 20 |
| `kNoticePerTile` | −7 a tile | `kCoverCrouch` | +26 |
| `kNoticeLightWeight` | up to +40 | `kCoverPerSneakLevel` | +1/level, capped at +30 |
| `kNoticeFacing` | +12 inside a quarter-turn arc | `kCoverRoomNoiseWeight` | up to +20 from the room's own din |
| `kNoticeAlert` | +15 for somebody on duty | | |
| `kNoticeBlindPenalty` | −45 through masonry | | |

plus half the noise the body itself is making. `seen = read + noise/2 > cover`.

Two things about those numbers. **Masonry is a penalty, not a refusal** — blind is not deaf, which is
why a lock probed behind a shut door is still a risk. And **`kNoticeAlert` is deliberately smaller
than the light term**: a bouncer is paying attention, not carrying a lantern. Set high enough to see
through a dark room it would have made every clause under it decorative.

`witnessCount`, `spreadWitness` and the Watch's own `canSeePlayer` all resolve through the one
`noticeBy()`, with a `static_assert` holding `kWatchSightTiles` and `kWitnessRangeTiles` together —
a crime witnessed by somebody the heat never counted is exactly the defect S3 and S5 each fixed once
already.

**No rolls.** The rule is an integer comparison in both directions, for the same reason the S3
pickpocket check has never rolled one: a draw taken inside a `const` query is a hole in the twin-run
gate.

### Crouching is the room's state and the body obeys it

`C` toggles it. The stance lives on the `Tavern` — it decides who sees a crime, so it is simulation
state and it is hashed — and `Session::step` copies it onto the `MoveInput` the body walks with.
`PlayerBody` halves the walk (`kCrouchSpeedPercent`) and refuses to be a run at the same time. What
being unseen costs in a first-person game is **time**, and that is where the bill is paid.

The room is told back how the body is moving, which is how footfalls reach the rule: still is silent,
crouch-walking is 9, walking is 30, running is 55. An **act** — a probe, a snapped pick, a boot
through a lid — is louder still and fades linearly over two seconds.

---

## Part three — the lock

S5 shipped burglary as one keypress beside a container with no lid on it. CRACKSMANSHIP — *"locks,
traps"*, in the owner's `skills.json` since S1 — was read in exactly one place, **after** the box had
opened itself, to scale what fell out. A skill that only multiplies a reward is not a skill.

Four things make `sim/lockpick.hpp` a game rather than a wait:

**The pins are secret and fixed.** `pinDepth(worldSeed, lock, pin)` is the four-step chain
`rng.hpp` fixes, with the lock's id as the spatial key and the pin index as the draw index, and **no
tick** — so a lock does not change between one second and the next, a player may walk away from a
half-picked box and come back to the same box, and a retry is not a fresh lottery. Wards raise the
*floor* of the range, so an apprentice's habit of trying zero first stops working on the good rooms.

**The pick can break.** Strain accumulates on a wrong probe and the wire snaps at `strainLimit`,
which rises with skill and falls with the wards. **Every pin you had set drops back** — a snapped
pick is not a checkpoint. Picks are finite: five to start, three more for nine coin off Finch, and
only if you are one of the roofs.

**Skill buys information and forgiveness, never success.** Below `kFeelLevel` (10) a wrong probe
answers *"nothing"*; at and above it the wire says which way you were wrong and a binary search
becomes possible. Tolerance — how far off a probe may be and still set the pin — is 0, then 1 at
level 15, then 2 at 30, and stops. A lock that opened at any depth would not be a lock.

**Failure has somewhere to go.** A lock whose last pick snapped is **jammed for good** — hashed,
permanent, and the box will not open to wire again. Force always works, is `kForceNoise` (88) loud,
and costs half the coin. That is a choice, not a dead end.

Every probe goes into the same `StealthState` a footstep does, so who heard it is decided by the same
rule that decides everything else.

### The four boxes

`gull::kStrongboxPins` = 3, and `strongboxWards(room)` = 2 for the two harbour-side rooms (a captain
pays for the window) and 1 for the other two. `crackStrongbox()` refuses a shut lock with
`ServiceResult::Refused`; `G` on a locked box puts the wire in instead of opening it.

---

## Part four — the hand in the coat

`T` lifts from whoever is at your elbow, with no conversation open. The dialogue layer's own
`PickPocket` topic is untouched and still works across a table; this is the same crime committed from
behind.

**Two skills.** Getting there is SKYRUNNING — whose row in the owner's `skills.json` says in as many
words that it covers *"sneak, pickpocket, takedown"* — and what the fingers do once they are there is
CRACKSMANSHIP, which is what the ward's ledger has charged a lift to since S4. Both are charged
whether the hand came out full or not, which is the standing Morrowind steer: you are charged for the
attempt, and being caught teaches more than most things do.

**Stealth moves the number; it does not decide the lift.** Whether a mark feels the hand stays
CRACKSMANSHIP against their STREETWISE exactly as S3 resolved it. The light on you, the crowd around
you and your own posture move their guard by `clamp((read - cover)/3, ±12)`. That bound is what stops
either half swamping the other — a rule where crouching beat a master's wits would be a rule where
the skill did not matter, and a rule where being seen at arm's length was an automatic catch would
make the verb unreachable.

The measured consequence, in one case: Father Maell at his table at nine at night, the same hands
(CRACKSMANSHIP 12, SKYRUNNING 30) at the same tile in the same second — **upright and walking, he
feels it; crouched and still with the room shouting over him, he does not.**

---

## What you can actually play

At two in the morning, from the keys and nothing else:

1. `C` — down on your haunches. The HUD's right-hand stack turns green: `HIDDEN CROUCH DARK 2 QUIET`.
2. Walk in. The doors are barred, the lanterns and candles are out, the hearth is banked, and three
   of the night staff are still in the building. Nobody makes you out.
3. `T` at somebody's elbow. At CRACKSMANSHIP 0 against a captains'-house bouncer it goes wrong, he
   turns, and the house sends the other bouncer at you — which is the game working.
4. `SPACE` up the stair. `G` at a bed-foot that is not yours: **`LOCK PINS --- DEPTH +........
   STRAIN 0/3 PICKS 5`** across the bottom band.
5. `W`/`S` to move the pick, `SPACE` to probe. Blind, at level zero. Pins drop and drop back; picks
   snap; the lock jams.
6. `F`. The lid goes, loudly, and the box gives half of what a clean pick would have.
7. `G` again. Fourteen coin and a piece with a name on it.

`--burgle` plays exactly that and reports it:

```
burgle beats=7/7 mask=127 lift=tried light=2 noise=88 hidden picks=0
locks open=4 jammed=4 forced=4 cracked=4 cracksmanship=3 skyrunning=1
```

`jammed=4 forced=4` is not the demo failing. That is what a level-zero cracksman does to a captains'
house, and it is why `Finch sells wire to his own` and why the skill rises off every probe.

**Frames** (all from the shipped `.exe`, no window, no GPU):

| | |
|---|---|
| `docs/frames/s9-burgle-lock.png` | the lockpicking row on the bottom band, two pins of three down, the pick on depth seven, `HIDDEN CROUCH DARK 2 HEARD` in green top-right, centre completely clear |
| `docs/frames/s9-burgle-box.png` | the guest floor after the box went, beds and the emptied strongbox block, still hidden |
| `docs/frames/s9-burgle-taproom.png` | back down among them with the bouncer's face filling the frame and `SEEN CROUCH DARK 6 HEARD` in amber — the same line saying the opposite thing |
| `docs/frames/s9-smoke.png` | `--smoke=40`, the mandatory boot-and-look |

---

## The cases have teeth

Two mutation runs, both against the same tree the green above names.

**Five mutations at once** — the light term deleted from the notice rule, the crouch worth nothing,
`crackStrongbox` back to ignoring the lock, every probe setting a pin, and `Ward::advanceToDay` a
no-op. `MUTANT_EXIT=1`, `97% tests passed, 13 tests failed out of 442`:

```
 27 - a day off the engine's clock is the same day as a day off endOfDay
 57 - a box above the stair is cracked, and your own is not a crime
128 - a wrong probe strains the wire, and enough of them snap it
129 - the last pick snapping jams the lock, and only force opens it then
130 - the feel tells a trained hand which way it was wrong, and an apprentice nothing
132 - the box above the stair is locked, and cracksmanship is what opens it
134 - a jammed lock is permanent, and the room says so with the box still shut
138 - the lock row draws the whole minigame, and the centre of the screen stays empty
139 - a burglary is played from the keys: crouch, cross, lift, climb, pick, empty
257 - every clause of the notice rule moves the answer, and none of them alone decides it
258 - crouching, the dark and the skill are what a player does about it
262 - a trained sneak lifts in a loud room what the same hands cannot lift standing up
320 - the ward's roll is in the windowed game, and a rival can take ground on it
```

**And the sprint's headline claim on its own** — `read += 0` for the light term and nothing else
changed. `MUTANT_EXIT=1`, `99% tests passed, 2 tests failed out of 442`: #257 and #262. Light
mattering is a claim that can go red by itself.

---

## What S9 does NOT do

Marked here and in the code, not left to be discovered.

- **No shadows.** `glowFrom` does not ask the tiles whether masonry is between the lamp and the tile,
  so a body behind the bar counter is lit as though the granite were glass. The *seeing* term does
  respect masonry (`TileQuery::lineOfSight`, since S3); what leaks is the brightness term.
  `VERIFICATION GAP (S9)` on the declaration.
- **Nothing but the four strongboxes is locked.** The district's doors, the Chandlery's counting room,
  the Drowned Hold — none of them have a lock, because none of them is simulated. `kMaxPins` is 6 and
  `Lock` carries wards precisely so the day they are, nothing here changes.
- **No light on actors.** The notice rule reads the light on the *player's* tile. An NPC standing
  under a lantern is no easier to see than one in the dark, because nothing yet asks.
- **No distraction, no hiding places, no takedowns.** You cannot throw something to move a guard, get
  inside a wardrobe, or put somebody down quietly. Crouching, the dark, the crowd and the distance are
  the whole vocabulary.
- **A lift you were seen making is not a chase.** The house's own bouncer machinery handles it,
  exactly as it handles a punch — a warning, then the door. There is no pursuit outside the Gull
  because there is no ward outside the Gull.
- **And still no combat screen.** It is not this sprint's brief and it remains the largest hole in the
  build: `brawl.hpp` correctly refuses every rematch, so wins two and three of S8's own headline arc
  are unreachable from a keyboard. Said again here so nobody has to find it twice.
