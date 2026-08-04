# S6 — Contraband, radiant work, and a Watch that takes you

Patch notes for sprint 6 of the C++ rewrite (the **Mercolas Engine**, which is a marketing
name and not a directory). Every claim here has a command beside it or a case name in the
gate that can go red for it.

Gate: `docker compose run --rm --build build` → **exit 0**, `100% tests passed, 0 tests failed
out of 371`, floor raised 349 → 371. Cross-toolchain: `scripts\verify-windows.ps1` → **exit 0**,
both reports byte-identical between linux/gcc and mingw/windows.

---

## First: six of the seven things S5's review found

| Finding | Was | Now |
|---|---|---|
| `--skyrun` failed at its own documented invocation | Defaulted to 20:00; Finch keeps the snug from 22:00. 0 of 9 beats, exit 1. | A scripted line names the hour it needs (`scriptedStartHour`) and sets it when nobody asked for one — never over a `--time` that was given. `--skyrun` → **9 of 9, exit 0**. |
| The leap was a teleport in the shipped client | `climb()` ran `while (airborne()) step()` inside the keypress. Every jump instant; 24 steps of tavern clock burned in one frame. | The press ARMS the arc. The ordinary step pump flies it and `Session::step` settles the landing on the step the feet touch — which is also the only step `takeFallBands()` has anything to report. |
| `Session::settleLanding` had no test | It owned fall damage, a skill use, a crime and two questline tallies, and lived in the client layer where the sim suite could not reach it. `test_crime.cpp` hand-called `noteTally`/`noteCrime` past it. | Moved to `Tavern::settleLanding`, where the hit points and the ledgers already live. Driven by two new cases and by `test_crime.cpp`, which no longer imitates it. |
| 24,960 claimed more than it proved | Stated as reachability. | `docks.hpp` says what it is: what the RULES permit, counted by a flood fill; an upper bound on what the body achieves, with the one known divergence (the stair clause in `PlayerBody::mantle`) named. |
| Topic labels cut mid-word | `label.resize(room)` — S5's own frame reads `THE VANISHED CLE`. | `clipLabel`, which prefers a word boundary, falls back to the column when the boundary would throw more than three columns away, and always MARKS the cut. |
| The heat clock was narrowed by its own codec | `cooledAtTick_` is an `int64_t` written through `putI32`. | Written as 64 bits, hashed as 64 bits, and a case that puts the clock past 2^31 and watches exactly one point cool. |

The seventh — nothing casts — is still open, and still marked in `spellforge.hpp`.

---

## Five goods, and a sack with a size

`native/include/granadad/sim/contraband.hpp`.

| Good | Worth | Heat a unit | Drams a unit | Skill | Legal |
|---|---|---|---|---|---|
| **scalp** | 4 | 0 | 2 | fieldcraft | yes, under the Flame's mark |
| **dust** | 16 | 9 | 3 | mixtures | no |
| **moonshine** (QUAYFIRE) | 7 | 4 | 24 | mixtures | no |
| **flower** | 10 | 6 | 8 | mixtures | no |
| **artifact** (a piece with a name on it) | 24 | 12 | 10 | cracksmanship | no |

**Weight is the load-bearing column.** A watchman notices a LOAD, not a count: four jars of
spirit are 96 drams and four twists of dust are 12, and the same four units are conspicuous or
invisible depending which. The sack holds 240 drams or 48 units, whichever runs out first.
`what a sack holds is measured in weight, and that is what gets you caught`.

**A bale stops being a bool.** S5's own note read "a bale is a BOOLEAN, not an item… no weight,
no contents, no owner". It has a cargo the boat decided — powder, spirit or bales, drawn once a
night so both bales in the snug came off the same hull — and three units of it. Carried out of
the door with no job open, the boat's own buyer takes it and pays the flat runner's fee, exactly
as in S5. Carried out with a job open, the sack comes off your shoulder into your own and the
contract is what pays, because being paid twice for one bale is a faucet with extra steps.

**Where contraband comes from**, all three through the same `G` a strongbox already used:

* the **bale** in the snug (dust, quayfire or flower — the boat's choice);
* a **strongbox** above the stair now yields a named piece as well as coin and anonymous loot;
* **rats**. Four of them on the skirting between eleven at night and eleven in the morning,
  which overlaps Watchman Cull's after-shift drink by two hours on purpose. Punch one down —
  not an offence, no bouncer crosses the room about it — and skin it. A skinned rat does not
  get up, and there are four a night, which is what stops the ward's bounty being a faucet
  with whiskers.

---

## Radiant work that names nobody who does not exist

`content/raws/contracts/contracts.json` (new file; the owner's files are untouched),
`native/include/granadad/sim/contract.hpp`.

**The shape is generated. Every proper noun is authored.** Which broker, which of the Forty
wants it, whose ground it comes off, how many, by which night and for how much are all drawn
from the counter chain bound to `(worldSeed, day)` — the same pure `mix64` chain everything else
in this simulation draws from, so the same night of the same world always offers the same four
jobs on any machine. A broker, patron or source the owner's `notables.json` does not have is
**refused at load, by name**, and an offer left with no patron or no source is dropped whole.
`a generated job can only ever name somebody the owner's file has`,
`a broker, patron or source the registry does not have is refused at load`.

What a brief actually reads like, composed from the template plus the notables' own file:

> Squall keeps a back room at the bathhouse and the back room keeps a smell. It comes off
> Foreman Cathal Hemp's ground at the Ropewalk baled like hemp, because at four in the morning
> it looks like hemp.

Three brokers, and the gate on each is a gradient rather than a switch:

| Broker | Speaks for | Needs | Sells |
|---|---|---|---|
| **Watchman Cull** | the Watch | nothing — a public bounty | scalps |
| **Master Venn** | the merchants | to not dislike you | quayfire for his own cellar |
| **Finch** | the Skyrunners | your name on the roll | flower, dust, and pieces with names on them |

That last row is where **the Skyrunner questline graduates**: S5's line ends by making you a
Cutpurse, and a Cutpurse is somebody Finch will give a job to. Every broker has at least one
job every night — four slots, three of them one per broker and the fourth whoever the tide
favoured — because a board that left the ward with no bounty on two nights in five reads as
broken rather than as quiet.

**The pay is the ward's own economy.** A contract is priced through exactly the
`guildPricePercent` S4 already uses for a mug of ale: the broker's guild, its influence over the
district and what it thinks of you. A guild that sells to you as one of its own pays you as one
of its own. `a bounty is not paid without the Flame's mark, and pay is the ward's own economy`.

**And a bounty is redeemed under the Flame.** DECISIONS.md's tenure ruling says the Church
"sanctions the redemption of a scalp", so it does: Father Maell signs for the taking, and the
Watch will not pay for an unsanctioned one. He keeps an evening hour that ends at half past
nine and Cull arrives at nine, so there is exactly a quarter of an hour in the day when the
ward will both sell you the work and sign for it — which is the shape of the job.

---

## The warrant finally has teeth

`native/include/granadad/sim/watch.hpp`.

**Was:** S5's own header said it out loud — "NOTHING ARRESTS. A warrant is issued, hashed, shown
in red on the HUD and read by exactly one thing — how long a bouncer waits before putting you
out."

**Now:** Watchman Cull glances up every five seconds. He acts on two independent things:

* **a LOAD**, noticed against his own kit-keeping and your streetwise, in drams —
  `20 + drams*6 + kit*3 - streetwise*10`, clamped to 750. A clean man is never taken for a load
  he is not carrying, and a loaded one is never safe.
* **PAPER**, which is not a beacon: he has to connect the face, and one glance in twenty-two
  does. Roughly two minutes in front of him is a coin flip and a man who walks through and out
  is usually through. That number was tuned, not guessed — at eight times the rate the build's
  own scripted Skyrunner line was arrested on its way back from the last delivery.

Then he crosses the room, and he has twelve seconds to reach you before his drink gets warm.
**Out of his sight is out of it** — which is the whole counterplay and what the roofs are for.

The sentence is canon's, verbatim from DECISIONS.md (Eli, 2026-07-14): one to three days for
anybody, the hand for a Skyrunner's first offence and the rope for the second. A search with no
paper behind it is a seizure and a charge and no cell at all. A maimed hand permanently takes
half of what two hands take — the only lasting statistical penalty in this build.

**Nothing kills the player.** `Sentence::Condemned` is a status the ward and the HUD both know
about and no death is simulated; the day there is a combat screen, that is the hook it hangs on.

**And an arrest costs more than the night.** Every taken contract whose goods were in the sack
dies with them: `a job whose goods are in the impound is a job you have lost`.

`a watchman notices a load, not a count, and never a man with nothing on him`,
`the sentence is canon's: a night for anybody, the hand and then the rope for the roofs`,
`an arrest empties the sack, tears up the paper and remembers the hand`,
`a warrant alone is enough, given long enough in front of the wrong man`.

---

## The acceptance, played

Two room cases, driven through the calls a keypress makes.

**`a contract taken, performed and paid: the ward's bounty, end to end`** — quarter past nine,
take the bounty off Cull, get Maell's mark in the fifteen minutes he has left, skip two hours
for the room to quiet, punch four rats off the skirting and skin them, hand them back across the
same table for the pay the board posted. It also asserts that **none of it raised a single point
of heat**, because the ward pays for these.

**`caught: a load, a warrant, and a job that dies in the impound`** — join the roofs, take the
roofs' work, crack four boxes for four named pieces, run a bale out of the door past the impound
keeper, lean on the taproom until there is paper, and then stand in front of Watchman Cull. He
takes the lot: goods to the impound, coin to the ward, the hand for a first offence, and the job
dead beside the goods that were going to pay for it.

Playable from the command line:

    dist\granadad.exe --contract        # 6/6 beats, exit 0
    dist\granadad.exe --skyrun          # 9/9 stages, exit 0

---

## The hour the Skyrunner line moved to, and why

`--skyrun` used to run at ten at night. With S6's law in the room it was **arrested on its way
back from the last delivery** — `arrests=1 sentence=maimed`, seven of nine beats, exit 1. That
is the game working, and the summary now prints `arrests=` and `sentence=` so a run stopped by
the world can be told apart from a run stopped by a bug.

The fix was not to soften the law. The scripted burglar now starts at **one in the morning**,
when Finch still keeps the snug until three and the impound keeper went home at one. The hour
is part of what the line teaches.

---

## The cases are not decoration

Five mutations to the S6 layer, one gate run, `MUTATION_EXITCODE=1`,
`98% tests passed, 9 tests failed out of 371` — every one of them on a case whose NAME states
the claim that broke.

| Mutation | Went red |
|---|---|
| `contrabandLegal` returns true for everything | `what a sack holds is measured in weight…`, `every good resolves both ways…`, `the sack and the job are one line each…`, `caught: a load, a warrant…` |
| a Skyrunner's first offence stops being the hand | `the sentence is canon's…`, `an arrest empties the sack…`, `caught: a load, a warrant…` |
| the board stops refusing ids `notables.json` does not have | `a generated job can only ever name somebody the owner's file has` |
| a watchman's eye stops depending on the load, IN THE PURE FUNCTION | `a watchman notices a load, not a count…` |
| ~~a watchman's eye stops depending on the load, AT THE CALL SITE~~ | **NOTHING. This survived, and S7 found it.** |
| the Flame's mark stops being required | `a bounty is not paid without the Flame's mark…` |

**CORRECTED BY S7, and the correction matters more than the table.** The row about the
watchman's eye was applied to `noticePermille` itself and not to the one place the simulation
calls it. Swapping `illicitWeight()` for `illicitUnits()` at `tavern.cpp`'s call site left all
371 cases green: the case named above drives the pure function, and nothing drove the wire. In
play the mutation is severe — four jars of quayfire is 96 drams against four twists of dust at
12, and under the mutation both are four, so the whole good-choice tradeoff the sprint is built
on evaporates in silence. The table as originally written read as broader coverage than existed.
S7 adds *a watchman's eye is on the load AT THE CALL SITE, not only in the arithmetic*, which
drives `Tavern::tickWatch` with both sacks and requires the heavier one to be seen sooner.

The third of those only goes red because `content/raws/contracts/contracts.json` carries a
**deliberate bad id** — `nobody_at_all`, in `bounty_rats`' patron list. A filter that has never
had anything to filter is a filter nobody has tested. Do not remove it.

The tree was restored from git afterwards and `git status` is clean.

---

## Frames

| | |
|---|---|
| `docs/frames/s6-work-offered.png` | Watchman Cull's list at ten at night: `8 TAKE 3 SCALPS.` and `9 TAKE 4 SCALPS.` beside his own business and the ward's gossip. **THIS CAPTION WAS WRONG AND S7 CORRECTED IT.** Those two rows are the bug, not the feature: the label was built patron-LAST, the eighteen-glyph column ate the authored proper noun, and two different jobs for two different people printed as the same row with one integer changed. S7 rebuilt the label patron-first (`VETCH - 4 SCALPS`) and put the picked row's full label on a detail line under the grid. The frame is kept as the evidence. |
| `docs/frames/s6-bounty.png` | the same table an hour later, paid: *"THAT IS HONEST WORK AND YOU LOOK STRANGE DOING IT."*, purse 40 → 52 |
| `docs/frames/s6-skyrun-quiet.png` | the Skyrunner line finished at one in the morning, nine of nine, with nobody from the Watch in the building. **It also shows a 68-character bouncer warning running off the right edge, cut mid-glyph** — `hud.alert` was drawn centred, unwrapped and unclipped. S7 clips it where the frame width is actually known. |
| `docs/frames/s6-smoke.png` | the authored spawn on the Tarwalk, 27 lamps, custom art |

**Not captured, and said rather than implied:** there is no frame in which a rat is clearly
legible. They are a third of a person's height, unlit, and the corners they keep are the corners
the lamps do not reach. That they exist, wander, can be punched down and can be skinned is
proved by cases, not by a picture.

---

## What is still open

* **Nothing casts.** `spellforge.hpp` composes and refuses compositions; no spell has an effect.
  Carried from S4, still marked in the code.
* **Fall damage lands on the tavern's copy of the player's hit points**, because that is the
  only place hit points exist. Carried from S5, still marked.
* **A leap's parabola is uncollided** — the arc is cosmetic between the two validated ends.
* **The ward outside the Gull is unstaffed.** A contract names Fenner, Squall, Brann and the
  rest, and their sites, and their trades — and you cannot walk up to any of them, because the
  Gilded Gull is still the only room with people in it. The patrons are real names doing real
  things in a district nobody else is simulated in yet.
* **A contract's goods come from three sources inside one building.** A dust job is filled by
  whatever the boat happened to land in the Gull's snug, not by going to Merle's boathouse.
* **The board has no byte codec** — the same disclosure `QuestJournal` makes. It is hashed, and
  a save needs four integers and a string per taken row.
