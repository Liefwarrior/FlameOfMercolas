# S10 — THE DEMO

What the tenth sprint built, what it fixed, and what it did not do.
`PLAY.md` at the repo root is the version written for playing; this one is the
version written for reading the code afterwards.

---

## 0. The one-paragraph version

Nine sprints built a district that keeps its own hours and gave the player no
reason to be in it. S10 is the reason: **the bloodletter trail**, twelve
authored leads at the map's own clue anchors, two of which are dead ends on
purpose; **the long game**, five tracks the ward remembers you by, derived from
counters that already existed; and **a first run** — the game opens on the case,
the keys are in the game on `F1`, and the corner of the screen always says where
the trail wants you next. Plus the S9 review's findings, closed with cases that
go red on the old code, and a polish pass driven entirely by looking at captured
frames.

Gate: **464 ctest entries** (was 442), **694,785 + 902,056 assertions**,
twin-run gate PASS, `content/` untouched except the one new file.

---

## 1. The trail

**Where it lives:** `content/raws/quests/casebook.json` — a NEW authored file,
not an edit to an existing one. `content/` is the owner's canon and read-only.

**Where its words come from:** `docs/design/DOCKS-GAZETTEER.md` §5.4 (the three
surface clue sites, scene-level, blessed) and §4.4 (the Forty Notables).
Nothing in the file invents setting; it selects it. The gazetteer's own forensic
language is carried through verbatim where it exists — claw penetration, bled
past what the wounds explain, terror rictus, black tacky ichor, a thin fellow
through the gap no man fits.

**Where its coordinates come from:** the map's own authored `script_anchor`
markers, laid down by an earlier pass in `tools/scripts/gen_docks_surface.py`
and never read by anything until now — `clue_c1_mission_backroom`,
`clue_c2_weighhouse_ledger`, `clue_c3_drowned_hold`,
`clue_wrackhouse_salvage_anchor`, `clue_brann_grayledger_anchor`,
`shrine_drowned_name_wall_anchor`, `strand_beaching_anchor`,
`business_k06_harlsyard_anchor`, `kennel_dog_anchor_01`,
`business_k12_kingsbond_anchor`. Converted to global tiles by the one documented
rule `docks.hpp` already states: local + 32, local z + 8.

Markers are not carried in the baked TROJSAV, so nothing at run time can
re-derive them. What **is** checked, and checked in the gate:

- every site is somewhere a body can stand in the **baked** Docks
  (`every lead stands somewhere a body can stand in the baked Docks`);
- no two sites are within one look of each other, which would make the second
  unreachable;
- and every site can actually be **walked to** from the authored spawn, by the
  district's own breadth-first router, with no teleport anywhere in the path
  (`the trail is walked across the real district, on foot, by the router`).

That last one closes the whole class of "a coordinate drifted and nobody
noticed".

### The design law, and why there is no roll

`DOCKS-GAZETTEER.md` §5.3, verbatim: *"People TELL the Wielder things. The
investigation is never persuasion — it is knowing WHERE to ask. No
dialogue-skill checks exist; the gate is geographic and social-topological."*

So there is no die anywhere in `sim/casebook.cpp`. You cannot fail a lead. You
can stand directly on one nobody has told you about and get "NOTHING HERE WORTH
WRITING DOWN", which is a case (`you cannot read a clue nobody has pointed you
at`) and is the best thing in the sprint.

### The shape

Twelve leads. One starts open. Ten close the case without leaving the quayside
plane; two more sit one band down on the strand. **Three go cold** — two of them
authored dead ends, one of them the closing lead which opens nothing because
there is nothing after it on the surface.

The trail **converges**: Harl's yard, Brann's gray ledger, the dogs on Kennel
Row and Tarry Jek all point at the Drowned Hold. That is what corroboration is,
and it is why a lead is cold when it points *nowhere* rather than when it opens
nothing *new*.

**The two dead ends are the point.** The Outfall grate is corroded shut *from
outside* — so the sea, which is what the entire ward believes, is wrong. The
King's Bond is where struck cargo is supposed to go and the struck line never
came there. Both cost a walk across the district and both pay a clue and a rung
of THE FLAME, because a system that paid nothing for ruling something out would
be a system telling the player not to look.

### The ward's nerve

`dread`, 0..100, rises with every lead read, and four authored bands name it:
THE WARD HAS HEARD NOTHING → THE WARD IS TALKING → THE NIGHT STREETS ARE
EMPTYING → NOBODY WALKS THE GULLET ALONE. It colours the `CASE` row red at the
top two bands. A case asserts the trail can actually reach the last band, so no
band is a string nobody sees.

---

## 2. The long game

`sim/legend.hpp` — five tracks, four rungs each, twenty authored titles taken
from the ward's own ladders (`ranks.json`'s Tenant, Cutpurse, Robber,
Skyrunner; the Temple's Disciple; the merchants' Stallkeep, Trader, Craftlord).

**It holds no state.** Every rung is a pure function of counters that already
existed and were already hashed: the crime tallies, the skill track, the faction
standings, the contract ledger, the casebook. So there is nothing new to keep in
sync, nothing that can desync, no addition to the twin-run gate's surface, and
every rung is explainable in units the player already sees.

Three of the five buy something, each wired at exactly one call site, named in
the header so a reader can check rather than believe:

| Track | Boon | Wired at |
|---|---|---|
| THE WIRE | extra picks in a bought set | `Tavern::buyPicks` |
| THE FLAME | extra tiles of look | `Session::examine` |
| THE TRADE | a discount at a counter | beside `guildPricePercent` |

THE ROOFS and THE LAW give a title and nothing else, and the header says so in
as many words rather than implying more. SKYRUNNING already buys a band of safe
drop directly and the Watch's memory is already the heat model's; a second
modifier on either would be a second answer to a question that has one.

THE LAW is deliberately hard to hold while holding THE WIRE — it subtracts heat
and arrests. That tension is the sandbox.

---

## 3. The first run

- The game opens with **the casebook up** and the hook on it. The first step the
  player takes puts it away, and it never reopens itself.
- `F1` lists **every key, in the game**, paged nine at a time off the same
  numbers a conversation uses — so the list can grow to any length without a row
  falling off the bottom with nothing on screen saying so, which is the bug the
  S3 review found in the topic grid.
- The `CASE` row **always names the next place**. A player who put the game down
  for a week and came back to a district of 692 people gets one line telling
  them where they were walking.
- Both surfaces draw in **the conversation's own two bands**, where the centre
  of the screen is already proven clear by a case. A journal and a controls
  overlay are the two elements most likely to break the HUD rule; the Java
  build's first-person view died on exactly this.
- The client starts a new game at **dawn**, because that is when the gazetteer
  has the Wielder arriving and because it is simply the right hour to hand
  somebody a district. `--time` still wins.

`SessionConfig::openingPage` is off by default and set by the client, for the
same reason `SmokeRunConfig` gates `--talk` and `--burgle`: two hundred test
cases and every scripted capture build a Session and most want a frame of the
world, not a frame of a menu over it. `tests/test_firstrun.cpp` sets the same
flag and drives the same code.

---

## 4. The S9 review's findings, closed

| # | Finding | Closed by |
|---|---|---|
| 1 | the dark-room stealth case asserted `>= itself` and passed with the notice rule disabled | it now requires somebody awake, upright and in range to miss you (`Tavern::watchersInReach`), and asserts `witnessCount() == 0` |
| 2 | the burglary acceptance could not detect broken stealth | beat 2 needs a watcher in reach; the summary prints `watchers=` beside `hidden` |
| 3 | the HUD stealth line was only ever tested saying SEEN | a case asserts HIDDEN, in the dark, crouched, with the simulation agreeing |
| 4 | no test or run anywhere picked a lock without foreknowledge of its pins | `workTheWire()` is a strategy that may read the depth, the pins down and the last `Feel` and nothing else; a case drives it over four locks and ten seeds |
| 5 | `--burgle=lock` was posed under a comment saying it was not | the wire is bought off Finch through the Join topic, and the probes are aimed by the solver |
| 6 | `GRANADAD S6` hardcoded in the HUD | reads the project version, which CMake sets in one place |
| 7 | stealth's building scope undocumented in code | marked on `Tavern::worstNotice`, where it lives |
| minor | `hashInto` omitted strainLimit/tolerance/feel | added |
| minor | assertions behind a data-dependent `if` | `REQUIRE`d |
| minor | two framebuffers rendered and discarded | deleted |

**And the tuning that made #4 possible.** `kStrainPerPick` 3 → 5,
`kFeelLevel` 10 → 3, `kStrainPerCraftLevels` 12 → 10, and **strain resets when a
pin drops**. That last is the structural one: strain used to run for the whole
attempt, so the third pin of a three-pin box was always worked on a wire the
first two had already half-spent. That is the arithmetic behind `jammed=4
forced=4` on every shipped mode. `--burgle` now ends `locks open=4 jammed=0
forced=0`.

**A defect found while closing #1.** `Tavern::worstNotice` ranked observers on
`read - cover` while `noticeOf` decides with `read + noise > cover`, so the
noise the body is making was left out of the comparison that picks whose opinion
counts. A body sprinting through a dark room could be ranked behind a sleeping
man whose Notice reads a flat zero, and `hidden()` would answer off the sleeper.

---

## 5. The polish pass, and how each defect was found

Every one of these was found by **looking at a captured frame**, which is what
the frames are for.

| Defect | Frame that caught it |
|---|---|
| the capture stamp printed through "THE CASEBOOK" | first `--trail=notes` |
| a clue printed through the `CASE` row | first `--trail=mission` |
| the guild row and the lock row are the same pixel row: "THE 1OCKUNPINS -TENADEPTH" | first `--burgle=lock` |
| a long alert cut at every resolution — the 4x6 font scales with height, so 1280px still only holds 64 characters at scale 4 | first `--trail=mission` |
| "SPACE  UP: MANT.", "E  TALK TO WHOE." | first `--trail=keys` |
| "1 ? MISSION OF." | first `--trail=notes` |
| the dread band printed under the clock; "OF THE FLAME" lost off the title | first `--trail=notes` |

The fixes worth naming: the alert **drops a font size rather than dropping
words**; the lock's row is suppressed in `guildLine()`/`objectiveLine()` rather
than at the draw call, **so a case can assert it** instead of pixels having to;
and every casebook row and key row now has an authored short form with a case
pinning its width.

---

## 6. What S10 does NOT do

Marked here and in the code, not only in a report.

- **The dedicated first-person combat screen.** Deferred in S8, S9 and now S10.
  It is the largest hole in the build. Fists, shoves and ejections resolve
  in-world; a drawn blade is ruled to be a different fight and that fight has no
  screen.
- **The starter dungeon.** The trail ends at the breach in the Drowned Hold's
  floor and there is nothing under it. `DOCKS-GAZETTEER.md` §6 has three levels
  designed.
- **Bodies in the district.** The 692 actors live in `Ward`'s economic roll and
  have no position. The clue sites are therefore places rather than people: you
  find Harl's scratch-marks, you do not meet Harl. This is also why stealth is
  building-scoped, and it is marked at `Tavern::worstNotice`.
- **Saving.** No save file. `Legend` was built to be derivable precisely so a
  save that stores the counters stores the legend, but nothing stores anything
  yet.
- **VERIFICATION GAP (S10):** two of the twelve leads sit on the strand plane
  (z10 / band 18) — Brann's back-cellar and the Beaching Strand where Tarry Jek
  sleeps. The scripted walk does not climb, so it reads ten and leaves those two
  open, which it reports. Whether a player can reach them on foot with `X` is
  **not verified**.
- **VERIFICATION GAP (S10):** the guild row (`y - 24*scale`) and the objective
  row (`y - 32*scale`) still cross the HUD exclusion rectangle at 320x180 and
  640x360 when non-empty. Pre-existing since S4, documented at `drawRoom` since
  S8, marked on `HudState::caseLabel`, and not fixed here — the honest fix is a
  bottom band that spans the frame the way Barony's does, which is a layout
  change and not a sprint's tail end. It is why the case row went **below** them
  rather than above.
- **VERIFICATION GAP (S3, still open):** nothing tests the client's key switch.
  Every branch calls a `Session` method the suite drives directly, so the
  behaviour is covered and the BINDING is not. `Q`, `J` and `F1` inherit that
  gap.
