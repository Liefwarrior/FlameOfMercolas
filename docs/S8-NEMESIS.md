# S8 — Nemesis

What changed, in the order it was built. Two halves: the four S7 review findings, then the nemesis.

Gate: `docker compose run --rm --build build` → **exit 0**, `ctest knows about 413 tests (floor: 413)`,
`100% tests passed, 0 tests failed out of 413`.
Windows half: `scripts\verify-windows.ps1` → **exit 0**, both reports byte-for-byte identical, and
the gate stamp now names the tree it came from.

---

## Part one — the S7 findings

### The HUD alert was drawn on top of the topic grid

- **Was:** `hud.cpp` drew `state.alert` at `height - margin - 23*scale`. `dialogue_view.cpp` claims
  the bottom band from `height - margin - rowStep*6` and draws the topic grid inside it, and
  `session.cpp` draws the panel first and the HUD over it. A bouncer's warning shouted
  mid-conversation therefore overprinted row two of the grid across all three columns.
  `docs/frames/s7-skyrun.png` shipped as proof of a *different* fix while showing this one:
  `2KLEDCTARBECK@GYOU HAVE HAD/THESKONLY WORD0YOURGET.1/THE DOOR.`
- **Now:** `HudState::showAlert` goes off while a conversation is open, for exactly the reason
  `showHealth` already did, and the warning moves into `DialogueViewState::alert` — a row of the
  conversation's own top band, which is sized from what it draws. Nothing is lost and nothing
  overlaps. The speech gives up a row when there is an alert, so a warning is never silently
  dropped off a band that was measured before anybody asked for it.
- `docs/frames/s7-skyrun.png` is re-shot from the fixed binary.

### The case that let it through examined no pixels

- **Was:** `test_tavern_render.cpp` constructed a `Session`, set `hud.alert`, called `drawFrame` and
  `drawHud`, and then asserted a pure `clipToWidth()` call. The comment said "every pixel of it
  lands inside the frame". No pixel was read.
- **Now:** *a warning shouted mid-conversation does not land on the topic grid* reads the band's
  pixels at 320x180, 640x360 and 1280x720 and requires them byte-identical with and without the
  alert — and then runs the mutation itself, drawing the alert where S7 drew it and requiring the
  band to CHANGE. The assertion proves its own teeth in the same case.

### The lodgers' eviction was a tautology

- **Was:** `CHECK(stats.lodgersTurnedOut >= 0)` on an `int64` that starts at 0 and only increments.
  The S7 review deleted the whole eviction loop in `compound.cpp` and all 394 cases stayed green.
- **Now:** the claim is stated as an INVARIANT over the roll — no household may name a landlord who
  has been turned out of their own house — plus `housesDistrained > 0` for non-vacuity and
  `lodgersTurnedOut > 0` so the invariant is not holding because nothing ever happened.
- **Mutation:** deleting `compound.cpp`'s eviction loop → **3 failed assertions**, exit 1.

### `Verdict::Abatement` could be deleted

- **Was:** replacing `Verdict::Abatement` with `Verdict::Stay` left the gate green. The one answer
  that costs a Den Duke money forever had nothing anywhere requiring it.
- **Now:** *the abatement is the sharpest instrument in the ward, and it is not dead code* finds a
  real household on a real morning whose score lands in the band section 2.8's abatement owns, with
  the draw fixed so the jitter is zero, and requires the priest to answer abatement. It then runs
  two years and requires an owner still in their house to be paying LESS than the plot's authored
  ground penny — which nothing else in the file can produce.
- **Mutation:** `Verdict::Abatement` → `Verdict::Stay` → **3 failed assertions**, exit 1.

### The conditional assertion in the bond-pipe case

- **Was:** `if (headsWorkingIn(from) + kHeadsPerFarmHand <= fromBefore) { CHECK(...) }`. A guarded
  assertion is an assertion that can pass by not running.
- **Now:** the precondition is CONSTRUCTED — buy every transferable bond off the seller, and give
  the ward another day to write more paper if one round was not enough — and then REQUIRED, so a
  ward that stopped producing bonds is a red case and not a silent skip.
- **Mutation:** `farmHands` stops counting leased-in labour → **6 failed assertions**, exit 1.

### The gate stamp did not name a tree anybody could check

- **Was:** `dist/GATE-STAMP.txt` carried a digest over raw bytes at container paths. The Dockerfile
  said out loud that it would not try to make a host recompute it, so "the gate ran on this tree"
  was not a checkable claim.
- **Now:** the container folds CRLF to LF in a scratch copy of `native/` and takes sha256 over
  `sha256sum` output for every file, sorted `LC_ALL=C` by its `./`-relative path.
  `scripts\verify-windows.ps1` computes exactly that from the repo and **fails** on a mismatch, so a
  green that belongs to a stale COPY layer, an uncommitted edit or a second worktree says which.

### The S7 numbers, re-run

The S7 review reported 21 petitions / 0 abatements / 72,787 pennies against the doc's 19 / 2 /
73,441. **I could not reproduce the review's figures on either toolchain.** At the S7 tip
(`824b8ba`), on the Linux/GCC host build and on the shipped `dist\granadad-twin-gate.exe`, three
runs each:

```
    pennies      73441 paid, 6559 short
    charge-rent  71874
    petitions    19
      dismissed   2   stay 5   abatement 2   bond 6   distraint 3   revoked 1
    distrained   3 houses, 1 lodgers turned out with them
```

which is what `docs/S7-COMPOUNDS.md` says, including "all six verdicts occur". Reproduce with
`.\dist\granadad-twin-gate.exe --ward-soak 730` or `.\dist\granadad.exe --ward`. Finding #4 stands
on its own — the abatement really was deletable — and is closed above; finding #5's numbers are not
reproducible here and the doc is left as it is, with this note added to it.

---

## Part two — the nemesis

**Killing the player is a promotion event.** Eli's own design, in his words: *"The player respawns
when killed, but the person who killed them should rise in political rank and retain a rivalry with
the player. Maybe the player could be killed by a laborer in a fist fight but next time the player
returns that person might be promoted to a carpenter who's the head of a new Carpenter's Guild."*

The rise goes through the systems the ward already runs on rather than beside them.

| What a win moves | The system it moves it through |
|---|---|
| A rung, with a title | `content/raws/factions/ranks.json`, by index. No rank name is invented. |
| The ward's weight | `FactionLedger::shiftInfluence`, so the S4 mirror takes the same off his guild's declared rival |
| A trade house | `content/raws/factions/chapters.json` (new), with real members enlisted out of the room |
| Every price he touches | the chapter's `toll`, added to `guildPricePercent` at the same counter a rung already moves |
| The ward's roll | `Ward::grantCharge` — an actor's version of the player's own `petitionForCharge` |
| His memory | `SocialLedger`, driven past `kHostileAtOrBelow` on the first win |
| What is in his hands | `Actor::setWeapon` / `setIntent`, which `classifyFight` already reads |

### What is new in `content/`

Two files ADDED, none touched. `git diff --name-status 824b8ba HEAD -- content/` is two `A` lines.

- `content/raws/factions/chapters.json` — nine trade houses, each naming a faction the owner's own
  `factions.json` has, a trade `skills.json` has, and a `DOCKS-GAZETTEER` §3 site. A row naming a
  faction the registry does not have is **refused at load, by name** — the same gate the compound
  roll and the contract board pass through. Loaded against an empty registry, all nine are refused.
- `content/raws/barks/nemesis_barks.json` — eight tables. `barks.json` always loads first and wins
  every duplicate key, so nothing here can take a line the owner wrote. No C++ writes a sentence
  he says; `NemesisBook` picks an authored KEY and the room resolves it through the ordinary
  fallback chain.

### The rise, in numbers

| | |
|---|---|
| Win 1 | a rung, `kInfluencePerWin` = 4 to his guild and −4 to its rival, disposition −45 (hostile band starts at −40), a quarter of the player's purse, `Intent::Harm` |
| Win 2 | a chapter founded: its `influence` on top, its `toll` on every price that faction quotes, members enlisted, `Weapon::Blunt`. Grudge past `kHuntsAtGrudge` — he stops keeping his own hours |
| Win 3 | the Flame re-lets a vacant charge to him. `Weapon::Edged`, `Intent::Kill` |

Nothing decays. `recordVictory` takes the grudge down and leaves the rank, the title, the house,
the members, the toll and the charge exactly where they are. **You can win the rematch. You cannot
un-found his guild.**

### The thing the arc found, and it is not a bug

`brawl.hpp`'s third clause has said since S2 that beating a **bloodied** man while meaning him
**Harm** is not a bar fight whatever is in your hands. A nemesis means it from his first win. So:

- the **first** defeat is an ordinary taproom brawl, resolved in the world, and the scripted line
  plays it — walk up, press the punch key, and lose;
- the **rematch** opens as a brawl and escalates the moment he has the player under a quarter of
  their health. The room stops resolving it and says `BLADE OUT - THIS IS NOT A BRAWL`.

That is the S2 rule working, and `--nemesis` counts it as a beat rather than working round it. The
two defeats that finish the rise go through `Tavern::concedeTo`, which is the seam
`docs/design/COMBAT-SCREEN-SPEC.md`'s dedicated screen will call when it exists. Marked
`VERIFICATION GAP (S8)` at the call site, in the header, and here.

### The compounds are in the game now

The S7 review's eighth finding, verbatim: *"S7 built 3,303 lines of economy that the player cannot
see, touch, or be affected by."* `render::Session` constructs a `sim::Ward` and registers it on the
same engine the Gilded Gull runs on. It ticks while the player stands in the taproom, and the first
thing in the game that reaches into it is the man who put the player on the floor taking the
**Gullet's** vacant charge — after which every house-owner on that ground owes a ground penny to
somebody who was drinking two tables away.

No new ward economy was written. The compounds got a door, not another wing.

### On the HUD

One line, bottom-right, above the room line: `RIVAL TARN WRENHALE - FOREMAN x3 HUNTING`, red when he
is looking for you and ash when he is not. Right-anchored and clipped to 44 columns, which is 220
pixels at scale 1 against a 320-wide frame — the S6 defect is not being reintroduced from a
different corner. A case reads the exclusion rectangle's pixels with and without it and requires
them identical.

### The persistent-ward door

Eli ruled 2026-07-31 that the variant where the city survives your death and you return as somebody
else is **not** being built from the start — *"it's easy to change that later"*. So the player
respawns as themselves and `nemesis.hpp` knows nothing about who the player is. Every record is
keyed on the WINNER: a stable **name** first and the room's roster id second, because a roster id is
a fact about one building's cast and a name survives the building being rebuilt. `NemesisBook`
encodes and decodes to a versioned byte string with a round-trip case, and a case proves the same
man under a new roster id keeps his record. The day a save carries a second protagonist the book is
loaded unchanged and a player identity is added beside it.

### Mutations run against this sprint's own claims

| Mutation | Result |
|---|---|
| the winner's guild gains no weight | **3 failed**, exit 1 |
| the winner never founds a house | **4 failed**, exit 1 |
| the winner never takes the charge | **2 failed**, exit 1 |
| he forgets — no hostility follows a win | **4 failed**, exit 1 |
| `nemesisWeapon` returns Fists forever | **3 failed**, exit 1 |
| the chapter's toll never reaches a price | **3 failed**, exit 1 |
| he never leaves his post to hunt | **4 failed**, exit 1 |
| the lodgers are not turned out (S7 mutation A) | **3 failed**, exit 1 |
| `Abatement` → `Stay` (S7 mutation D) | **3 failed**, exit 1 |
| the bond stops moving labour | **6 failed**, exit 1 |

---

## What S8 does NOT do

- **There is no combat screen.** Defeats after the first cannot be taken in world, because the S2
  rule correctly refuses to resolve them with fist rules. `Tavern::concedeTo` is the seam and the
  gap is named at its call site.
- **One room.** The nemesis rises out of the Gilded Gull's fourteen and nowhere else, because the
  Gull is still the only staffed building in the district.
- **A founded chapter is a house inside a faction, not a sixth faction.** The owner's
  `factions.json` is canon and has five. A chapter has a parent, a seat, a roster and a toll; it
  does not have its own ladder, its own standing or its own recruiter.
- **He hunts, he does not ambush.** Past the grudge threshold a rival crosses the room to wherever
  the player is and stands there. He does not throw the first punch: starting a fight is still the
  player's verb, and an NPC-initiated brawl wants an aggression model this build does not have.
- **The rise is not persisted to disk.** `NemesisBook::encode` is round-tripped by a case and
  nothing writes a file, which is the same gap `SocialLedger` has carried since S3.
