# S5 — The Skyrunners

Patch notes for sprint 5 of the C++ rewrite (the **Mercolas Engine**, which is a marketing
name and not a directory). Every claim here has a command beside it or a case name in the
gate that can go red for it.

Gate: `docker compose run --rm --build build` → **exit 0**, `100% tests passed, 0 tests failed
out of 349`, floor raised 311 → 349. Cross-toolchain: `scripts\verify-windows.ps1` → **exit 0**,
both reports byte-identical between linux/gcc and mingw/windows.

---

## The roofs are a road

**Was:** 8,132 standable cells of the baked Docks could not be reached at all — every compound
roof deck, and the whole roof-slum plane at world z22, which had **zero** reachable cells.
`DOCKS-GAZETTEER.md` §2.6 filed that as deliberate, "until the law/economy layers learn to climb
(S5+)".

**Now:** three verbs on the player's body, all integer, all through the same collision a walking
step uses, none of them rolling a die.

| Verb | Key | What it does | What refuses it |
|---|---|---|---|
| **Mantle** | `SPACE` | One band onto the top of the wall you are facing | No solid face to grip; no surface on top of it; no room over your own head |
| **Leap** | `SPACE` | Up to three tiles of air, aiming at the far roof first | Ground at arm's length (that is a step); a wall in the flight path; no landing anywhere in reach |
| **Drop** | `X` | Off the ledge in front, to the first floor under it, at most three bands | A walkable tile ahead; a wall; water; the world's declared floor |

The facing is snapped to the four-point compass for all three, because "which cell am I facing"
has no answer at 47 degrees and a body that half-committed would climb the corner between two
buildings. `SPACE` also takes a stair.

Reachable from the spawn, re-derived from the baked bytes on every build by a flood fill that
uses nothing but these three moves and the ordinary walking one:

| | on foot | with the roofs |
|---|---|---|
| total | 17,054 | **24,960** |
| quayside z19 | 11,089 | 11,147 |
| mid-slope z20 | 3,043 | **7,459** (all of it) |
| upper z21 | 2,031 | 3,864 |
| roof slums z22 | **0** | **1,664** |

`the roof moves open two thirds again of the district`,
`the roof-slum plane was completely unreachable and is not any more`,
`the whole roof of the Gilded Gull can be stood on`.

**Two bounds, stated rather than discovered later.** A fall passes through AIR and nothing else,
so a body cannot step off the Long Quay and land dry on the harbour bed three levels under two
cells of water. And the caller declares the world's floor: below the harbour surface the Docks
is unbuilt dungeon, and 313 of the columns over the seabed are dry all the way down — a leap off
the mudflats went down one and **could not come back up**, because z17 has not one standable cell
to mantle onto. A trapdoor into an unfinished level is not a feature.
`the roof moves do not open a trapdoor into the unbuilt dungeon`.

---

## Six crimes, one call site

**Was:** one crime, a hand in a purse, with no consequence outside the room it happened in.

**Now:** six acts, and every one of them — a topic on a list, a box cracked upstairs, a bale
carried through a doorway, a body landing on a roof — goes through
`DialogueDirector::noteCrime`, which moves all four things an act should move:

- the **tally** a questline counts,
- the **heat** the Watch keeps,
- the **roofs' standing**,
- and the **garrison's**, halved and inverted, through the mirror `ranks.json` has declared
  since S4.

An act that reached one of those and missed the others is the bug that shape exists to prevent.
It is also the first thing in this build that moves a faction number with no conversation open,
which is what makes the mirror something a player feels rather than a note in a JSON file.
`a crime moves the tally, the heat and BOTH sides of the mirror at once`.

| Act | Where it happens | Heat | Worth to the roofs |
|---|---|---|---|
| Lift a purse | a topic, on anybody with coin | 8 | 3 |
| Crack a strongbox | a bed-foot in one of the four guest rooms above the stair, `G` | 18 | 6 |
| Run a bale past the Watch | the snug, `G`, then out of the door | 26 | 8 |
| Fence what you took | a topic, on the one man in the room who buys | 4 | 2 |
| Lean on somebody | a topic, on anybody who is not a bouncer or the law | 14 | 5 |
| Be on a roof at all | the three verbs above | 1 | 2 |

**Heat is what the Watch HEARD, not what you did.** An act nobody witnessed raises none of it. It
cools one point every five minutes — including through a night asleep in a rented bed, charged
against elapsed seconds rather than a clock that wraps at midnight. Past sixty there is paper out
on you; it lapses at twenty rather than at sixty, so one cooled point cannot flicker the state of
being wanted. `a warrant is issued high and lapses low, so one cooled point cannot flicker it`,
`the ward forgets at one rate whether it is watched or slept through`.

**Coin is coin and everything else is a thing.** A lifted purse pays out at once; a cracked box
and a leaned-on trader hand you property, and property has to be sold to somebody. Being a
Skyrunner is not enough to be that somebody — a cutpurse is not a fence, and that is the whole of
what the guild's second rung is worth. `a cutpurse is not a fence: the second rung is what makes
somebody buy`.

---

## Four dead tokens, and one that meant two things

The S4 review found eleven of fourteen unlock tokens with no reader, and warned that
`ranks.json` was becoming a design document pretending to be data. Four are wired, and each buys
a mechanic rather than a topic:

| Token | Rung | What it buys |
|---|---|---|
| `roof` | Skyrunner 1 (Tenant) | A band of safe drop and a tile of leap. The roofs' teaching is physical, which is the only kind they have |
| `fence` | Skyrunner 2 (Cutpurse) | Finch buys what is not yours to sell, at a rate that rises with the rung |
| `lair` | Skyrunner 4 (Skyrunner) | **Go to ground** — heat to nothing and the paper with it |
| `warrant` | Watch 3 (Sergeant) | **Lose the file** — the paper goes, the heat stays, because the ward remembers what the roll has forgotten |

And `grace` now means what it says. The S4 review found the trap in it: the bouncer's
grace-seconds read INFLUENCE — the ward's balance of power — which has nothing whatever to do
with the token that happens to share the word. A watchman's rung adds eight seconds of rope on
its own; a warrant takes ten away. `a watchman gets longer to finish his drink and a wanted man
gets none`.

Still declared and unread, and still marked so: `gang`, `call`, `credit`, `charge`, `bunk`,
`muster`.

---

## The Quiet Tenant — nine stages

`content/raws/quests/skyrunner_tenant.json`, with `content/raws/barks/roof_barks.json` for its
voice. Both are ADDITIONS beside the owner's files; nothing under `content/` was edited.

1. **Stop being a stranger** — the oath, in the snug after ten. Rank 1, Tenant.
2. **Put your hands in two purses** — the cheapest lesson.
3. **Crack a guest's box** — up the stair, four rooms, four boxes, one of them not yours.
4. **Get on the roof** — the way up you have walked past all week.
5. **Cross to the next roof** — a roof on its own is a trap with a view.
6. **Sell what you took** — which first means earning the rung that makes him a fence.
7. **Lean on somebody** — it works because of what the ward has heard, not what you say.
8. **Run a bale past the Watch** — Cull drinks by the threshold after nine.
9. **Ask what a tenant is** — and get the ward's tenure ruling back in the mouth of a burglar.

A player who finishes it has done every criminal act the ward has, once each, and knows where
each one lives. **Every counter string a stage names is checked against the strings something in
the build actually emits** — a stage counting a word nothing ever counts is a quest that cannot
be finished, which is worse than authoring none. `the Skyrunner line is authored against a
vocabulary that can finish it`.

The line is walked in order and reported between acts, because a counted stage counts what is
done WHILE IT IS THE STAGE. `a counted stage refuses to be turned in until it has been done`.

---

## Two names

**Finch replaces "Wisp".** S2 invented Wisp Low-Tide out of the authored wastrel name pools
because the Skyrunners had no line to hang anything on. The owner had already named him:
`notables.json`, `finch`, "the quiet tenant", sited at LAIR_SKYRUNNER — and `ranks.json`'s own
note says the roofs' first rung is called *Tenant* BECAUSE of that epithet. He arrives with a
personal bark table and an authored micro-history with Gullet Mag, neither of which an invented
name could ever have reached.

**Watchman Cull joins the roster.** The S4 review found that one of the five factions could not
be joined in play. Cull is canon (`cull`, the impound keeper at K02) and the impound yard is a
short walk from the Gull's door. He closes two things at once: the Watch is joinable, and
`enemyPresence()` — counting rivals since S4 with nothing in the room to count — finally has
bodies, because the roofs' contact and the law's recruiter drink in the same taproom between ten
and one. He is also the pair of eyes a bale has to get past.

---

## Two walls that were not walls

Both found by trying to WALK a scripted capture rather than by reading code, and both the same
family as S2's doorway-lintel correction.

**The Gilded Gull's guest floor has never been reachable on foot.** Its stair is one authored
cell — a STAIR at z19 and a STAIR again at z20 — with open floor all round it on both levels, and
the step rule prefers the same band, so every neighbour of the stair keeps a body downstairs
forever. A flood fill finds **zero** transitions between the two floors anywhere in the building.
S2 shipped `rentRoom()` and `sleep()`; S4 wrote a case about a robbery up there; all of them put
the body on that floor by constructing it there, and nobody ever walked. The sprint's own up-key
takes the flight now. `the Gull's guest floor has never been reachable on foot, and now is`.

**A body walked into an upstairs wall and fell through it.** The down-clause of the step rule read
"not standable at my band, standable one below, so step down" without asking whether the thing at
my band was AIR — and every interior partition of that guest floor is a wall standing over open
taproom. **429 places in the district.** Reachable-from-spawn goes 16,934 → 17,054, and every one
of the 120 is on the upper band, which is what says the fix stopped a FALL rather than opening a
door.

---

## What S4's review asked for, item by item

| # | Asked | Done |
|---|---|---|
| 1 | Pin the topic-paging DRAWING path with a case that can go red | **Yes.** The rows are built by `topicRowsFor`, which `drawDialogue` calls and prints verbatim; the new case asserts nine numbered rows on page two carrying the tenth to eighteenth topics. Re-run with `std::min(total, kTopicPageSize)` put back: `126/346 ... ***Failed`, `99% tests passed, 1 tests failed` |
| 2 | Make the scripted capture path fail loudly | **Yes.** A short run says how short in the summary, says it again on its own line, and exits non-zero. The PNG is still written, because a frame of a short run is evidence of the shortfall |
| 3 | Fix `walkToTile` so `--flame` works from the default spawn; correct the README | **Yes.** It routes with RegionPath — the room's own pathfinder — over a box that actually contains the spawn (`gull::kRegion` stopped one tile short of it). README corrected |
| 4 | Build the caster | **NO.** Nothing casts a crafting. Still marked at `spellforge.hpp:276`, and still the biggest hollow shell in the build. S5 spent its budget on the roofs and the crime layer and did not touch it |
| 5 | Give the topic list room, or wrap it | **Yes**, the cheap half: labels are truncated two glyphs shorter so they stop clear of the next column's key, with the arithmetic pinned. They still truncate mid-word; two columns with wrapping is not done |
| 6 | Wire the inert unlock tokens or thin them | **Four of eleven.** `roof`, `fence`, `lair`, `warrant`, plus `grace` corrected. Six remain declared and unread and are named as such |
| 7 | A Watch recruiter somewhere reachable | **Yes.** Watchman Cull, in the Gull from nine |
| 8 | Have the twin-gate workload move a faction number | **Yes.** The driver puts a hand in a purse every ninety seconds through the real path, and the report prints the lifts, the heat and both sides of the mirror so the rows can be READ moving. `the gate's workload actually moves a faction number` |

---

## Frames

All four captured from the shipped `dist/granadad.exe`, with no window and no GPU.

| File | Command |
|---|---|
| `docs/frames/s5-gull-roof.png` | `granadad --smoke=0 --hold --time=20 --roofs --screenshot=...` |
| `docs/frames/s5-roof-leap.png` | `granadad --smoke=0 --hold --time=20 --roofs=leap --screenshot=...` |
| `docs/frames/s5-skyrunner-line.png` | `granadad --smoke=0 --hold --time=23 --skyrun --screenshot=...` |
| `docs/frames/s5-wanted.png` | `granadad --smoke=0 --hold --time=23 --skyrun=away --screenshot=...` |

The last two are the same nine-stage run, once with Finch's last line on the panel and once with
it closed so the HUD can be read: `WANTED  HEAT 60` in red at the top right, `THE SKYRUNNERS -
CUTPURSE` bottom left, and the centre of the frame completely empty.

---

## Known gaps, marked in the code as well as here

- **Nothing casts.** `spellforge.hpp:276`. Unchanged from S4.
- **Nothing arrests.** A warrant is issued at heat 60 and the only thing that reads it is a
  bouncer's patience. `DECISIONS.md`'s Skyrunner escalation ruling (maimed on the first offence,
  hanged on the second) describes a Watch that acts on one, and no such Watch exists.
- **A roof-run is witnessed by nobody**, deliberately — DOCKS-GAZETTEER §2.5's "nobody looks up"
  is the guild's whole social premise — but it means the roofs have no risk in them yet beyond
  the fall.
- **Crime stops at the Gull's walls.** The six acts are real and the rest of the ward has nobody
  in it to commit them against.
- **The topic labels still truncate mid-word.** They no longer collide.
- **Six unlock tokens still have no reader.**
