# JUSTICE-SPEC — Daggerfall-inspired court, jail and the rope (v1)

**Status:** BUILT on `justice/build` (2026-09-11; DECISIONS.md "Justice build landed"), and REVIEWED: the fix pass of 2026-09-11 (DECISIONS.md "Justice fix pass") closed the critic's eleven defects -- the arrest has its moment on screen before the page (section 2.2), the priest's openings are keyed by the case (section 6.5), every redlined row is rewritten, the check block is packed by whole terms, a night is a whole night the clock agrees with (section 4.2), the end rows arm before they take (section 5), and the `--court=` drive photographs every state a real verb sequence can reach (the rest are declared gaps in `docs/frames/justice/README.md`). The nine open rulings of section 9 were adopted as recommended under the owner's standing canon and are folded here by that row; three build-time deviations, all presentation: the check block's sum reads `MAKES` (the 4x6 font has no `=` and the font is not touched), the plea's own term prints on its own line under the record's sum, and the one row after the judgment is worded by the answer (`WALK OUT.` / `PAY IT.` / `SERVE IT.` / `THE DROP.`). The original draft read: DRAFT FOR VETO. Design only; nothing in this document is built. Read on `C:\repositories\fom-combat` @ a50fa23 (combat/build, a strict superset of wip 9fa5544). The build program commits this file as `docs/design/JUSTICE-SPEC.md` after the owner's veto pass, with the rulings folded in the way COMBAT-ACTION-SPEC.md folded its four.

**Ruling (Eli, 2026-09-02, verbatim, binding):** *"If the player is tagged as a criminal it should be like Daggerfall where you can go to court and you can face jail or execution (game over)."* Sequenced as its own build strictly after combat (COMBAT-ACTION-SPEC §10, VETO 4). Combat landed the hook and stopped: crime, heat, arrest, `Sentence::Condemned`, no court.

**Binding canon this spec is written under.** Two player-end paths that are never conflated: **combat defeat** = the nemesis ruling (promoted-against, coin taken, quay revive, you live), already built; **court execution** = a true game over, the one and only place the player ends. Losing a brawl never ends the game; letting the law convict you can. Also: deference law (the Watch never goes hostile to a presented Wielder); the Flame is top dog and its priests already adjudicate (§2.8's hearing); "Bloodletter" is priest/learned vocabulary only; the Hall of Covenants is another district's; the city ships district by district.

**Laws:** integer sim math, no floats in state; twin-run determinism on the 60-steps-per-second spine; same-roll discipline (the plea carves a band off the arrest's one owned draw, no new stream); S9 draw-free where no chance is involved (the guilty plea, the charge sheet, every term); population baseline `0x2646C1AAA2BA38DF` does not move (no `WardActor` edits); terminal register + the four UI conventions (DECISIONS.md:61) + device-aware `promptKey`; no new art, no font change, no map edits; demos through real features. Daggerfall is the feel, adapted.

---

## 0. The model in one paragraph

Being **tagged** is a fact the ward already keeps: paper out on you (`warrant_`, heat past 60), blood on the record (`murderer_`), or the rope once passed (`condemned_`). The Watch's arrest loop is untouched. What changes is what an arrest with paper **resolves to**: instead of an inert sentence and a clock jump, the officer takes you to **the Mission**, and **Father Maell hears the paper**. The Watch is the arresting arm and the hangman; the Flame is the bench. The hearing is a **terminal-register page** (the strip-card master/detail shape, no room, no new art): the charge is read, you plead **I DID IT** (draw-free, never doubled, never spared) or **I DID NOT** (the arrest draw's own band, ±10, spared at the top and doubled below it), the priest's weighing is **shown in the four-line check block**, and the judgment is one of seven: SPARED, FINED, HELD, BOUND, THE HAND, COMMUTED, THE ROPE. Jail is the shipped `skipHours` with its consequences widened through systems that already exist. **The rope is the game's only end**: the held black dip, a two-line plate naming the place, the ward and the reason, and then A NEW MAN or LEAVE. Coin buys nothing at the bench; what you gave at the Mission's door before you were taken is the only currency the Flame reads.

---

## 1. The criminal tag

### 1.1 What marks you

Three states, all of which combat/build already keeps on `CrimeLedger`, hashed and codec'd:

| Tag | Sim fact | Set by | Clears by |
|---|---|---|---|
| **WANTED** | `warrant_` (heat ≥ `kWarrantAt` 60, lapses below `kWarrantLapsesAt` 20) | any witnessed act via `noteCrime`; a witnessed kill via `markMurderer` (`kMurderHeat` 60 = instant paper) | cooling (5 sim-min a point, through sleep and through a cell); `quashWarrant` (Watch `warrant` token); `lieLow` (Skyrunner `lair` token); **a hearing** (any judgment sets heat := 12, paper off) |
| **WANTED FOR BLOOD** | `murderer_` and the warrant it always leaves | `Tavern::slayActor` under the three-clause witness rule | **only the bench**: COMMUTED serves it (`murderer_` := false); THE ROPE ends the run. Cooling clears the paper but not the blood: a murderer whose heat has cooled is not WANTED on the HUD, but the next arrest on ANY paper is a rope hearing. Not `lieLow`, not `quashWarrant` (a sergeant can lose a file, not a corpse). |
| **CONDEMNED** | `condemned_` (+ new `commuted_`) | a rope hearing that ended COMMUTED | never. Recognised at `kCondemnedRecognisePermille` 120 for the rest of the run. Mercy is given once (§4.4). |

Plus the two riders the ward remembers: `maimed_` (the hand, permanent, `takePercent` 50) and `arrests_` (convictions, the priors term). `Sentence::Fined` (a paperless search at the door) is **not a tag and never reaches the bench**: Cull's "no paper on you, so no cell for you" stays the pre-court fast path exactly as shipped.

**Daggerfall's one crime slot becomes Granadad's ledger**, which is already the better instrument: a count of priors, a heat that cools, a blood flag that does not.

### 1.2 How it is exposed

- **The HUD status row** (`Session::heatLine`, already promoted when it carries MAIMED/CONDEMNED): `WANTED  HEAT 64` / **`WANTED FOR BLOOD  HEAT 60`** (new phrase while `murderer_`) / `CONDEMNED  HEAT 12` (after a commutation, for the rest of the run) / `MAIMED`. Heat stays a number as shipped; standing stays a phrase as ruled.
- **The Watch's behaviour**, unchanged from combat/build: one look every 5 s, recognised at 45‰ with paper, 120‰ condemned, `CLOSING` for 12 s, taken at melee reach. **Only the Gull has a Watch** in v1 (the street population is the baseline and is not touched, §7.3).
- **The world**: greeting, price and service already move on reputation and faction standing; a conviction moves the Skyrunner/Watch mirror (§4.3), so the ward feels it without a stat screen.

### 1.3 Serialization

Everything the court adds lives on **`CrimeLedger`** and goes into its codec (bump `kCrimeVersion` 4 → 5, appended fields, decode guards widened), never as bare `Tavern` fields. This is deliberate: `playerWeapon_` and `playerPresentsAsWielder_` are hashed but in no codec (SHIP-NOTE:200, the per-run hole), and `Tavern::lastArrest_` is neither hashed nor codec'd. The criminal tag itself (`arrests_`, `lastSentence_`, `maimed_`, `condemned_`, `murderer_`) is already codec'd and hashed; v5 appends: `commuted_`, `executed_`, `lastPlea_`, `lastJudgment_`, `hearings_`, `daysServed_`, `slewWitnesses_`, `servedTallies_[6]`, and the pending `HearingState` (§7.1) so a run saved between the arrest and the plea reopens at the bench. No save file exists today; the codec is the seam one will use, and the twin gate hashes all of it now.

---

## 2. Arrest → court

### 2.1 Who presides: the Flame's priest, with the Watch on either side of him

**Father Maell sits. The Watch petitions and the Watch hangs. There is no magistrate.** All three scouts converged here independently; the reasons, in canon order:

1. **The court already exists and is built.** §2.8's eviction hearing is a court: a petition, an offering that does not buy the verdict, a priest weighing *the person* as well as the debt, six outcomes, `weight` exposed, jitter off one owned draw, `apply()` separated from the weighing. Eli's own words: *"A priest should be required to evict and they should consider the tenant too."* A ward where a priest must sign before a family loses its roof but a sergeant can hang a man on his own authority is upside down. Generalised: **a priest is required to condemn, and he weighs the man.**
2. **The Flame already signs for blood** (S6: the priest sanctions the taking of a scalp "before the knife rather than after it"; `contract.sanction`: "The ward pays for these and the Church signs for them"). The rope is the same office one step up.
3. **"The Flame still is the top dog"** (Eli, 2026-09-02). Top dog holds the highest court. The Den Dukes are its property managers; the Watch is its bailiff.
4. **The Watch's dossier forbids a Watch bench**: "they keep order, they don't solve anything... investigation nonexistent" (GAZETTEER §4.2); "order-keepers, not investigators" (K21). Its summary justice is Eli's 2026-07-14 ruling and it survives intact as **what the paper asks for** (§3.1). A Watch that also adjudicated would be a longer arrest line.
5. **A magistrate is a fourth conscience** the tenure ruling already refused ("one conscience now sits under trade, under blood money, and under the roof over every family's head"). No magistrate is among the Forty; the Hall of Covenants is another district's and a registry, not a bench; the novel's Trojian court is the king and fifty senators, off-map and the wrong scale for a dockside knifing.
6. **Register.** "The Church never opposes anyone openly; it makes itself unavoidable while appearing to serve." A priest who weighs your soul in the Flame's register of mercy and then hands you to the sergeant is that sentence dramatised. And "Bloodletter" is priest vocabulary: a murder hearing before a priest is the one legal scene where the learned word may lawfully appear (§9, ruling 8).

Canon for a priest passing death: novel L1325 (a religious figure judged a boy and ordered him into the flame "as an act of mercy") is **tone precedent only**, flagged by the Daggerfall scout as unconfirmed to be the Flame's own church; hanging is canon for deserters (L1723).

### 2.2 Where: the Mission, as a page, not a room

The Mission (K17) is already a travel target with an arrival tile and already the close point of EVICTION ("travel to the Mission and walk in"). The court is **a page opened on arrival**, no map edit, no interior scene:

```
CLOSING 12 s (shipped) → TAKEN at reach (shipped) → applyArrest: goods to the impound, the sheet written, the draw spent (§3.3)
  → [NEW, FIX PASS] THE ARREST'S OWN BEAT, before the bench does anything: the officer's line
        (watch.held / maimed / condemned, "There is paper out on you and I am the man holding it.
        Walk.") said on the alert row IN THE ROOM with his hand on you, held kTakenOfficerSteps
        (150); then the cut -- black, one line in the rope plate's register, "TAKEN TO THE
        MISSION. 23:40.", held kTakenPlateSteps (105); every key swallowed but Pause, the body
        not yet moved. Pinned on the rendered frame (test_hearing_page.cpp, TAKEN).
  → [NEW] placeBodyAt(Mission arrival tile) + dressInstantCut() + the page
  → [NEW] THE HEARING page opens (§6.1) — modal, un-backable
  → the plea → the weighing shown → the judgment (§4)
  → SPARED / FINED: released where you stand, the Mission door
  → HELD / BOUND / THE HAND / COMMUTED: skipHours, then the shipped Tarwalk release + "TURNED LOOSE ON THE TARWALK. DAY 4. 23:40."
  → THE ROPE: the game over (§5)
```

Heard **at once, at any hour** (Daggerfall's shape; the priest is sent for). The K34 holding cell as a place you wake in, and a night in it before the bench, are named ceiling (§10). `Sentence::Fined` (no paper) resolves at the door as today and never dips.

**The officer who took you lays the paper.** `ArrestReport.officer` (Cull, in the Gull) is the petitioner: "WATCHMAN CULL LAYS THE PAPER ON THE TABLE." No Vess walk, no new actor, no `WardActor` touched.

### 2.3 Resist and escape

No surrender verb, no Oblivion pay/jail/resist menu (Daggerfall has neither; the ES reference §6.6 agrees). **Escape is spatial**: out of his sight is out of it, 12 s and he gives up, the roofs are for exactly this (all shipped). **Resist is a swing**: hitting the officer is combat v1's own Lethal path; killing him is a witnessed murder and a rope charge (§4.4, case a). Nothing new is built for either.

---

## 3. The plea and the check

### 3.1 The charge sheet (draw-free, written at the arrest)

`CrimeLedger::charge()` (const; the ladder half of today's `arrest()`) produces a `ChargeSheet`:

| Field | Source | Meaning |
|---|---|---|
| `tier` | `sentenceFor()` + `murderer_` (the shipped ladder, unchanged) | **PAPER** (`Sentence::Held`: anybody with a warrant) / **THE HAND** (`Maimed`: a Skyrunner's first) / **THE ROPE** (`Condemned`: a murderer, or a Skyrunner's second). This is **what the Watch asks for**; Eli's 2026-07-14 sentence survives verbatim as the petition. |
| `worst` | `Deed::Slew` if `murderer_`, else the highest-heat crime with `tallies_[c] - servedTallies_[c] > 0` | the one line the sheet names |
| `since` | `tallies_ - servedTallies_` | "TWO LIFTS AND A CRACKED BOX" — since you were last before the bench |
| `heatAtArrest`, `unitsSeized`, `witnesses` (`slewWitnesses_`) | the ledger | the terms below |
| `draw` | the ONE `drawForPlayerAction()` `applyArrest` already spends (`tavern.cpp:3268`) | nights and jitter, §3.3 |

The charge is fixed at the arrest. Nothing between the arrest and the plea changes it.

### 3.2 The rows

```
1 - I DID IT.
2 - I DID NOT.
3 - HEAR THE PAPER
```

`HEAR THE PAPER` is the UI reference's "View X's stats" idiom: the charge, what the paper asks (A CELL / THE HAND / THE ROPE), and the terms **as phrases, never numbers** before the plea ("THE MISSION KNOWS YOUR NAME." / "THREE SAW IT." / "TAKEN ONCE BEFORE."), then `0 - BACK` to the rows. The arithmetic is shown **after** the plea resolves (UI-REFERENCE: mechanics first, then consequence). Both standing rules hold: standing on the HUD is a phrase; a resolved check shows its sum.

### 3.3 The weighing: `weighArraignment()` copied one-for-one from `weighPetition()`

All integer, all named, all printed in the check block. Positive favours the accused.

| Term | Formula | Cap | Reads |
|---|---|---:|---|
| THE FLAME | `+24` | | `weighPetition`'s own zero point: the Flame's default is mercy, "or the institution is a formality with six names" |
| TONGUE | `+ streetwise / 2` | 20 | Daggerfall's Streetwise plea; `skills().level("streetwise")` (= `kHaggleSkill`) |
| THE DOOR | `+ max(0, templeStanding) / 3` | 24 | what you gave at the Mission: alms, sanctions, the oath (`FactionLedger` temple standing; Acolyte 52 → +17, Shepherd 84 → +24) |
| THE WARD | `+ reputation / 5` | ±20 | `SocialLedger::reputation()` [-100, 100] |
| TAKEN BEFORE | `- arrests_ × 10` | 30 | convictions, not hearings |
| HEAT | `- (heatAtArrest - 60) / 4` | 10 | the paper above the line |
| BLOOD | `- 30` if `murderer_` | | |
| N SAW IT | `- witnesses × 4` | 16 | the kill's witness count (rope tier, murder only) |
| THE ROOFS | `- 12` on THE HAND tier | | a Skyrunner's first |
| THE SECOND RUNG | `- 24` on a Skyrunner's ROPE tier | | |
| THE ROPE ONCE | `- 16` if `condemned_` | | a commuted man before the bench again |
| CONFESSED | `+ 6` on I DID IT | | draw-free |
| THE PRIEST IS A MAN | `+ ((draw >> 16) % 21) - 10` on I DID NOT | ±10 | `compound.cpp:1067`'s own jitter, off the arrest draw |

**The offering is not in this sum, and there is no offering row.** §2.8's rule (proven by two hearings at different offerings requiring the same verdict) is kept and sharpened: the bench is not a counter. Coin cannot be offered at the hearing at all; the FINE is a sentence paid to the ward, not a purchase from the Flame. What DOES weigh is coin and work already given at the Mission's door, which is temple standing, which is THE DOOR. The priest's line says so: *"I have read what you gave at this door. It is the only reason we are talking."* Bribing the officer at the CLOSING stance is named ceiling (§10), not v1.

### 3.4 Same-roll discipline, declared

`applyArrest` spends exactly one `drawForPlayerAction()` and `arrest()` reads `heldHours = 24 + draw % 49` off it. The court adds **no draw and no stream**:

- the nights keep `draw % 49` exactly where they are (the low residue; unchanged, so no sentence-length baseline moves);
- the plea jitter reads `(draw >> 16) % 21`, a declared band above the nights, the `strike()` habit (whiff bits 0-2, variance `(roll>>3)%3`, crown bits 8-15, tired band bits 32-37);
- the draw is spent **at the arrest**, stored in the hashed `HearingState`, and the plea reads a band off it later. The sim never waits on the page for a roll; the plea arrives as an ordinary stepped input (§7.1).

**I DID IT is draw-free** (S9: no chance is involved; the same man with the same record gets the same answer every time). **I DID NOT reads the band.** The nights are drawn either way because canon says "a range wants a roll" (watch.hpp).

### 3.5 The two pleas, in one table

| | I DID IT | I DID NOT |
|---|---|---|
| Draw | none | the arrest draw's band, ±10 |
| Best case (PAPER/HAND tier) | FINED | **SPARED** at ≥ 55 |
| Worst case | the band's own judgment, never doubled | the band's judgment **DOUBLED** (nights ×2, fine ×2, bond days ×2) and temple standing **−8** ("you lied to the Flame's face") |
| Rope tier | COMMUTED at ≥ 24, else THE ROPE, draw-free | COMMUTED at ≥ 24 ± the band, else THE ROPE. **Never SPARED**: the corpse is on the roster and the witnesses are named |
| Trains | nothing | `streetwise`, use-XP, win or lose (a plea is a haggle with your neck on the table) |

Daggerfall's gamble, exactly: a denial is the only road to walking out clean and the only road to the doubled sentence. A confession is predictable and capped.

---

## 4. The sentence branch, in integers

### 4.1 The judgments

New append-only enum, hashed and codec'd. `Sentence` (watch.hpp) is **not** extended: it stays the Watch's ask and `lastSentence_` keeps recording it; the court's answer is its own type because the two disagree by design.

```
enum class Judgment : uint8_t { None=0, Spared=1, Fined=2, Held=3, Bound=4, Maimed=5, Commuted=6, Executed=7 };
enum class Plea     : uint8_t { None=0, Guilty=1, NotGuilty=2, NoPlea=3 };
```

`scored = weight (+ jitter on I DID NOT)`.

**PAPER tier** (the ask is a cell):

| scored | Judgment | Coin | Clock | Ledger |
|---:|---|---|---|---|
| ≥ 55 (I DID NOT only) | **SPARED** | 0 | 0 | paper off, heat := 12, `hearings_`++; no prior |
| ≥ 38 | **FINED** | `fineFor(heatAtArrest, unitsSeized)` = heat/4 + 3/unit, doubled if the plea failed, **capped at the purse; the shortfall is worked off, 1 day per 4 Royals, at most 7 days** (the tenure ruling's own "payable in Royals or in yourself") | 24 h × bond days | heat := 12, paper off, `arrests_`++ |
| ≥ 14 | **HELD** | as FINED | `heldHours` = 24..72 (shipped draw), ×2 if the plea failed, + shortfall days | as FINED, `daysServed_` += |
| < 14 | **BOUND** | the fine **forgiven** | **120 h** (5 days) of labour in the Mission's yard, ×2 if the plea failed | as HELD; temple standing **+4** (the work was the Flame's) |

BOUND is ACTORS-SPEC's unbuilt `PLEAD` verb made real ("culprit released to the Priest's custody, fed, preached at") and the slot Daggerfall's banishment would have taken. **Banishment is dropped**: one district exists, and "banished from the Docks" is a game with no map.

**THE HAND tier** (a Skyrunner's first; the ask is the hand): the PAPER bands with one substitution. ≥ 38 → **HELD with the hand spared** (the priest overruled the sergeant; ruling 5 in §9); ≥ 14 → **THE HAND** (`maimed_`, plus HELD's coin and nights); < 14 → THE HAND + BOUND's five days. SPARED at ≥ 55 on I DID NOT as above.

**THE ROPE tier** (a murderer, or a Skyrunner's second; the ask is the rope): two answers and nothing else.

| scored | Judgment | What it does |
|---:|---|---|
| ≥ 24 (`kMercyLine`) | **COMMUTED** | the hand (`maimed_`; "the rope does not un-take the hand"), **288 h** (12 days) bondsworn to the Mission, fine forgiven; `condemned_` := true (recognised at 120‰ for the rest of the run), `commuted_` := true, **`murderer_` := false** (served); temple standing **+8**; heat := 12 |
| < 24 | **THE ROPE** | `executed_` := true. The game over, §5 |

**Mercy is given once.** A rope-tier hearing with `commuted_` already true has **no plea**: the page opens, the priest speaks, the one row is `1 - I HAVE NOTHING TO SAY.`, and the answer is THE ROPE. Worked examples (§3.3 terms):

- A first murderer, nobody, streetwise 10, two saw it: 24 + 5 − 30 − 8 = **−9**. I DID IT → −3, THE ROPE. I DID NOT → −19..+1, THE ROPE. *A nobody hangs.*
- The same killing by an **Acolyte** (temple 52 → +17) with a tongue (streetwise 20 → +10), one prior: 24 + 10 + 17 − 10 − 30 − 8 = **3**; I DID IT → 9, THE ROPE; I DID NOT needs a band the priest does not have. *The Mission's own acolyte hangs for a witnessed killing unless the ward loved him.*
- A **Shepherd** (temple 84 → +24), streetwise 30 (+15), ward warm (+4), no priors, two saw it: 24 + 15 + 24 + 4 − 30 − 8 = **29**. I DID IT → 35, COMMUTED, certain. I DID NOT → 19..39, COMMUTED 16 times in 21. *The devout may confess and live.*
- A **Skyrunner's second**, Acolyte, tongue 20, one prior: 24 + 10 + 17 − 10 − 24 = **17**; I DID IT → 23, THE ROPE by one. *Eli's "hanging on the second" stands for anyone the Flame does not know well.*
- A **first-time thief**, streetwise 12 (+6), Almsbearer (30 → +10), ward cold (−15 → −3), heat 68 (−2): 24 + 6 + 10 − 3 − 2 = **35**. I DID IT → 41, FINED (no cell). I DID NOT → 25..45: HELD doubled 13 times in 21, FINED doubled 8 in 21, never SPARED. *Daggerfall's lesson: confess the small ones.*

### 4.2 Jail lengths by charge

| Charge (what the paper asks) | Days | Doubled (failed denial) |
|---|---:|---:|
| PAPER, HELD band | 1–3 nights: `24 + draw % 49` h off the draw, rounded ONCE to the nearest whole night (`cellNights`, one at least), so `TWO NIGHTS` is exactly 48 h on the clock and the release lands on the same clock face two days on (fix pass) | 2–6 |
| PAPER, BOUND band | 5 | 10 |
| Shortfall on a fine | 1 per 4 Royals, max 7 | (the fine itself doubled) |
| THE HAND | 1–3 + the hand | 2–6 + the hand |
| COMMUTED | 12 | n/a |
| THE ROPE | the run | n/a |

The six crimes are not sentenced individually (a warrant is cumulative); the sheet names the worst of them for the record, and the heat above 60 is the term that lengthens the answer.

### 4.3 What the world does while you are held (all through shipped systems, nothing parallel)

- **The clock**: `skipHours(N)` (shipped, "what a sentence does"), then `syncClockAfterSkip` → `syncWardToCalendar` (shipped). `dayNumber()` moves.
- **Heat**: cools through the skip as shipped, **then** `heat_ := kHeatAfterSentence` (12) and paper off, applied **after** the skip. This fixes the scout's finding that the constant is dead today (set before the skip, cooled to 0 by it). "The ward has not forgotten what you did; it has been paid for it."
- **Contracts**: the goods-seized ones die at the arrest (shipped `seizeFor`); every taken job past `dueOnDay` fails at the next doors-open `refresh` (shipped `expireStale`). Radiant errands have no deadline and wait.
- **Cases**: casebook cases have no due day and the eviction door is an hour window on any evening; they wait, honestly. Nothing to build. (A case whose party died in the fight that got you taken already parks with the combat build's "dead men close no cases" line.)
- **The ground penny**: `Ward::advanceToDay` runs `endOfDay` per day and a `quarterDay` every 90. A player who holds a covenant or a charge and sits out a quarter-day accrues like any duke, and can end up the tenant in the OTHER hearing. **Allowed, not prevented**: the two courts touching is the design.
- **Standing**: the social ledger is untouched ("sleeping a night does not make anybody forget you robbed them"); a conviction is a justice event, so HELD/BOUND/THE HAND/COMMUTED call `addStanding(skyrunners, +4)` and the S4 mirror halves it onto the Watch as −2 ("warms to whoever the Watch corrects", factions.json's own note). SPARED and FINED move nothing. Temple: −8 for a failed denial, +4 BOUND, +8 COMMUTED.
- **The nemesis does not rise.** The man who had you taken gained nothing; losing to the law is not losing a fight. The two ledgers stay apart, which is the canon's whole point.
- **The body**: hit points to max on any judgment that cost a day or more (the days did it; `reviveAfterDefeat`'s own shape). A fine heals nothing.

### 4.4 Execution: exactly which offences, and the two-paths canon enforced

**The rope is the ROPE tier and nothing else**, which is already routed in `CrimeLedger::arrest` on combat/build:

- **(a) a witnessed murder** (`murderer_`; "the rope is for the blade, not the purse"). This includes killing a watchman (Daggerfall's guard-killing is murder), so no third case is needed;
- **(b) a Skyrunner's second arrest with paper** (Eli, 2026-07-14, "hanging on the second").

**Explicitly not the rope:** repeat violence short of killing (`Offence::Brawled` is house business and assault is heat; the Watch "beats per watch raws"); an unpayable fine plus violence (unpayable is the bond, §4.1); any number of thefts (ACTORS-SPEC's "the Watch arrests, never executes" stands for everyone off the roofs, and a run that can end over a lifted purse is a run nobody takes a second job in). The court and the execution were added by the ruling; that line was not repealed.

**The canon, enforced in code and in tests:**
- `settleDefeat` → `reviveAfterDefeat` → the quay is **never called** from the court path; `executed_` is **never set** from `applyDefeat`. Two tests assert both directions unreachable.
- `takeArrestRelease()` and `takeDefeatRelease()` are already separate flags in the same `Session::step()`; the court hangs on the first and a third flag (`executed()`) is read beside them. Nothing shares a line.
- On the screen the two ceremonies share primitives and **never a plate**: the epitaph fades out to the quay; the rope's plate never fades (§5).

---

## 5. The game over, staged

The death ceremony's primitives generalised, not copied: `armDeathCeremony`'s dip over `kPageEaseSteps` (8), the two bone lines in the plate register, `kDeathHoldSteps` (270). **The fade-out phase is removed. The veil holds.**

```
[THE ROPE] badge (inverted fill, held kJudgmentHoldSteps = 90)
  → the priest's last line (court.rope bark)
  → the one row: 1 - <confirm> — the player takes the drop themselves
  → silence. The frame dips to black over 8 steps. No one-shot: ThudHeavy is "every path to the floor" and this is not the floor.
  → the plate, 270 steps, no input accepted:
        HANGED AT THE SALTGATE POST.
        BY THE WARD. FOR CANNIC.            (a Skyrunner: BY THE WARD. FOR THE SECOND RUNG.)
        THE FOURTH DAY. 23:52.               (dim, the dateline, drawRouteCard's foot idiom)
  → the pending-decision zone rises under it, option-list idiom, inverted-fill selection:
        1 - A NEW MAN
        2 - LEAVE
```

The two rows ARM on the first press and take on the second (the QUIT pattern, and the plea rows' own): the armed row carries `-- SURE? <key>` on its tail, ESC/B or moving the cursor disarms it, and a leaned-on ENTER after the plate's hold cannot start a new man or leave the game (fix pass).

Register rule for the plate: name the place (K21 is the ward's authored gibbet), name the ward as the killer, name the reason. It reads as the ward's roll, not the game's message. `PUT DOWN IN ... / BY <killer>. BY <weapon>.` is the revive's grammar; `HANGED AT ... / BY THE WARD. FOR <reason>.` is the end's. They cannot be confused on screen.

**The machinery, costed off the seam scout:**
- **Sim**: `executed_` on `CrimeLedger` (hashed, codec v5). While set, `Tavern` refuses every world verb (movement, attack, cast, interact) the way `talking()` already gates them; `stepPlayerCombat`/`applyDefeat` are short-circuited by it. The player does not take `Activity::Dead` (there is no player roster actor to mark); the bit is the corpse.
- **Presentation**: `armRopeCeremony()` beside `armDeathCeremony()`, `composeRopeCeremony()` with no fade-out, the rows drawn under the plate once the hold expires. ~60 lines.
- **Exit**: `Session::runEnded_` + `runEndReason_` (`NewMan` / `Leave`) beside `quitRequested_`; `main.cpp:4827` reads it and stops the loop; `run_client` returns a distinct code; **`main()` wraps `run_creation_window → run_client` in a loop** that goes back to the creation window on `NewMan`. Both functions already init and quit their own SDL and destroy their `Session` on return (the seam is proven, `main.cpp:3831`), so the loop is ~15 lines and the shutdown ordering at :5026-5045 is already solved for a return path. LEAVE = the shipped quit. There is no save, so there is nothing to erase and nothing to load: **the run is genuinely over**, which is the honest Daggerfall outcome and the whole reason this is cheap. No "last save" row is offered because none exists; when a save file lands, the row is `1 - THE LAST SAVE` and it goes first.
- **Input**: ESC does nothing on the plate. Pause is refused. The only live input is the two rows.

---

## 6. Presentation within the laws

### 6.1 The hearing page

**The strip-card family, master/detail** (`Session::stripCard()` → `drawCreationPage` with `hasDetail = true`): the shape the chargen quiz already draws ("answer left, consequence right"), which UI-REFERENCE names as the informed-choice layout. Zero new primitives, ~120 lines across session/main. The conversation surface (Maell as `Speaker`) was considered and set aside for v1: it buys the authored attitude header at twice the cost and needs a `Leave`-less list builder; the priest's voice comes through bark rows either way.

```
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
!THE MISSION -- A HEARING                                                        |
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
!WATCHMAN CULL LAYS THE PAPER ON THE TABLE.                                      |
|THE WARD SAYS YOU PUT CANNIC DOWN IN THE GILDED GULL. THREE SAW IT.             !
!THE PAPER ASKS FOR THE ROPE.                                                    |
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
![1 - I DID IT.]           !A CONFESSION IS WEIGHED AS IT IS GIVEN. THE FLAME'S  !
| 2 - I DID NOT.           |ANSWER IS FIXED BEFORE YOU SPEAK IT. MERCY OR THE    |
! 3 - HEAR THE PAPER       !ROPE, AND NOTHING ELSE.                              !
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
```

Detail for row 2 on the rope tier: "A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT. TEN POINTS EITHER WAY. MERCY OR THE ROPE, AND NEVER SPARED."; on a cell or hand charge: "... DENIED AND DISBELIEVED, THE SENTENCE DOUBLES AND THE MISSION REMEMBERS THE LIE.", and row 1 there ends "NEVER DOUBLED, AND NEVER SPARED." -- each tier's literal names only what is on its table (fix pass). Row 3 swaps the detail for the sheet (§3.2). The panes hold their height (UI-REFERENCE: nothing jumps as the cursor moves). The header band carries the bouncer-alert slot like every page ("the one line that outranks a menu"). Prose panel and verdict badge get their own `EasedToggle`; the badge lands with an `ImpactPulse` at the shipped restraint.

**After the plea, the check block replaces the detail pane** (the four lines, stolen outright):

```
[THE PRIEST WEIGHS]
24 + 10 TONGUE + 17 THE DOOR - 10 TAKEN BEFORE - 30 BLOOD - 12 THREE SAW IT + 4 THE PRIEST IS A MAN = 3
THE LINE: 24 MERCY
[THE ROPE]
```

PAPER tier prints the lines the score was read against and only those: `THE LINES: 55 SPARED  38 FINED  14 HELD` on a denial, `THE LINES: 38 FINED  14 HELD` on a confession (SPARED was never on its table, §3.5). The arithmetic is drawn as whole terms -- a row breaks only between terms, so `- 20 THE WARD` never parts from its sign at any width (`packTerms`). A hearing with no plea (mercy once) weighs nothing, prints nothing and wears no `[THE PRIEST WEIGHS]` badge: the verdict leads. The officer's walking-in line stands under the rows only until the answer; a judged page has nothing of his on it. The badge is an inverted fill; `SPARED` is the longer badge on the larger margin, like `CRITICAL SUCCESS`. Then the consequence in prose from the bark table. The judgment's clock and coin are stated as numbers on the last row (`TWO NIGHTS. 17 ROYALS.`), the way `HELD HARD -- CUDGEL 14-18` states its span.

### 6.2 Device-aware prompts

Rows print their digits and digits pick what they print (`route_menu_key`'s `chooseVisibleTopic` shape); the confirm row on the plate composes `promptConfirmKey(promptDevice_)` exactly as `"QUIT -- SURE? " + promptConfirmKey(...)` does; pad `pageBackRemap` is gated through `pointer_page_open` with the new `courtOpen()` predicate. The plea row is armed on first press and confirmed on the second (the QUIT pattern) so a leaned-on ENTER cannot plead.

### 6.3 The grammar exception, stated

UI-EA law: the key that opened a page closes it; ESC backs out one layer. **The hearing has no opener and no back.** ESC disarms an armed plea and does nothing else; `dismissOverlays()` (the ten world verbs) must not include it; PAUSE still opens over it (a player can always quit the game) with the WAIT row hidden and refused ("THE PRIEST IS WAITING."). Daggerfall's court has no leave either. This is the build's first un-backable modal and it is declared here rather than discovered.

### 6.4 The tagged criminal on the HUD

`WANTED  HEAT n` (shipped) / `WANTED FOR BLOOD  HEAT n` (new) / `CONDEMNED  HEAT n` (shipped word, new meaning: rope passed and commuted) / `MAIMED` (shipped). `hud.cpp`'s promote rule extends to the new phrase. A bondsworn stretch prints nothing extra: the clock jump is the whole of it.

### 6.5 The priest's lines

**FIX PASS (2026-09-11): the openings are CHOSEN BY THE CASE and rotated only within the case.** Sixteen tables: `court.paper` (a thief the Mission has never seen) and `court.paper.door` (THE DOOR above zero), `court.blood`, `court.roofs.hand` (the ask is the hand) and `court.roofs.rope` (the second rung), `court.nothing`, `court.plead`, `court.taken`, the seven judgments, `court.lie`. Every row of a table is true of every hearing that table can open: no row names a gift, a rope, a corpse or a night the sheet may not carry, and the lie rows never name the sentence's shape. The learned word is never said to a layman, not even nearly (ruling 8 below is superseded). The full row list is in `docs/frames/justice/README.md`.

New bark tables keyed like the shipped `watch.*`: `court.paper`, `court.blood`, `court.roofs` (the reading), `court.spared`, `court.fined`, `court.held`, `court.bound`, `court.hand`, `court.commuted`, `court.rope`, `court.lie`, `court.nothing`. Three rows each, rotated on `hearings_`. **BUILT (BARKS & GATE lane, 2026-09-11): fourteen tables** -- the twelve above plus `court.taken` (the officer walking you in: his line under the rows, in the master pane, for the whole hearing) and `court.plead` (the priest pressing for the answer once a plea is armed, in place of his opening); three to four rows each, the two voices rotated together on `hearings_`. **This is a content append** (rows in `content/raws/barks/contract_barks.json` beside `watch.*`), and the combat build appending `watch.condemned` is the precedent; it is flagged for the owner (§9, ruling 9). Fallback if refused: code literals in the pause-row shape. The three shipped `watch.condemned` rows already read as the walk to the bench and stay as the officer's lines.

---

## 7. The legality composition

### 7.1 Sitting on combat's hooks without re-touching them

- **`CrimeLedger::arrest()` is split**, not rewritten: `charge()` (const, the ladder half: `sentenceFor` + `murderer_` → the tier, the sheet) and `sentence(const Judgment&)` (the mutation half at `crime.cpp:279-299`, now parameterised by the court's answer). The shipped `arrest()` becomes `charge` + `sentence(ladder default)` so every existing test and the `test_combat_action` murder→Condemned hook pass unchanged.
- **`Tavern::applyArrest`** keeps: the one draw, the impound seizure (Cull's job, "the one part of an arrest that happens whether or not there was paper"), `contracts().seizeFor`, the room reset, the officer back to Watching. It loses: the fine (now the court's), `skipHours` (now after the plea), `arrestRelease_` (now after the plea). It gains: `hearing_` (a hashed `HearingState`: sheet, draw, plea, judgment, the term list) and `hearingPending_`. Fined-tier (no paper) is unchanged end to end.
- **New sim surface**: `Tavern::hearingPending()`, `const HearingState& hearing()`, `Tavern::plead(Plea)` (a stepped input; the tape records it like any watch op), `Tavern::executed()`. `weighArraignment()` is a free function in a new `sim/justice.hpp` beside `watch.hpp`, rules-only, no room, no actor.
- **`markMurderer(std::int32_t witnesses)`** is added as an overload; the no-argument form stays and delegates with 1, so combat's call site is bit-identical until the build repoints it to pass the loop's count.
- **`kHeatAfterSentence`** is applied after the skip (§4.3). Its value does not change.
- **The Watch loop** (`tickWatch`), `kRecognisePermille`, `kCondemnedRecognisePermille`, `kWatchClosingSeconds`, `noticePermille`, `watchCause`, the witness rule, `Deed::Slew`, `kMurderHeat`: untouched.

### 7.2 Deference

`tickWatch`'s gate stands: a presented Wielder is never closed on, never taken, never tried. The court is reachable only while the player does not present as Wielder, which is the current play state (the Persona seam sets nothing yet). **The Flame's pierce at the bench** (the disguise seam: can the priest see the Wielder under the man and acquit him, or is the bench the one place shed immunity does not return?) is a **seam left open, not built** (§9, ruling 7). The `Speaker`/`HearingState` carries a `presentedWielder` bool read from the same seam so the day the ruling comes it is one branch.

### 7.3 The Watch in the street

Unchanged in v1: **only the Gull has a Watch** (Cull, 22:00–01:00). The K21/K34 patrol pair is `WardActor` population and the baseline `0x2646C1AAA2BA38DF` does not move. A wanted man on the Tarwalk is safe from arrest in v1 and that is stated, not hidden: the counterplay stays "do not be recognised in the Gull, or get out of his sight". Street recognition is the first named ceiling item (§10) because it is the one that costs a baseline move.

### 7.4 The two courts

Debt goes to the priest's civil hearing (§2.8, `weighPetition`, the duke petitions); crime goes to the priest's criminal hearing (this spec, `weighArraignment`, the Watch petitions). Same bench, same base 24, same jitter, two petitioners, two enums. Daggerfall tries loan default as a crime; Granadad splits it, and a convict who misses a quarter-day in a cell can be the tenant in the other one.

---

## 8. The build-lane plan

Three worktree lanes, disjoint footprints, SIM's headers first (they are the API).

| Lane | Footprint | Delivers |
|---|---|---|
| **SIM** | `sim/justice.{hpp,cpp}` (new), `sim/crime.{hpp,cpp}`, `sim/watch.hpp` (comments only), `sim/tavern.{hpp,cpp}` (the arrest split, `plead`, `executed`), `tests/test_justice.cpp` (new), `test_crime.cpp` codec v5, `test_combat_action.cpp` unchanged-and-green | `ChargeSheet`, `Judgment`, `Plea`, `HearingState` (hashed), `weighArraignment` with the term list, `apply` (coin, clock, ledger, mirror, temple), the `markMurderer(witnesses)` overload, `servedTallies_`, `heat := 12` after the skip, the two unreachability tests, the offering-buys-nothing test's criminal twin (two identical hearings, different purses, same judgment) |
| **PRESENTATION** | `render/session.{hpp,cpp}`, `render/hud.{hpp,cpp}` | `openCourt` on `hearingPending` (dip, arrival tile, `TAKEN TO THE MISSION.`), the strip-card hearing page + HEAR THE PAPER + the check block + badges, the release lines, `WANTED FOR BLOOD`, `armRopeCeremony`/`composeRopeCeremony`, the end rows, `runEnded_`/`runEndReason_`, `--court=` shutters in `SmokeRunConfig` (the `--eviction=` pattern: a real drive through the real page) |
| **INPUT/FLOW** | `client/main.cpp`, `render/controls.hpp` (nothing new expected) | the `courtOpen()` branch in `route_menu_key` (cloned from the pause branch), `pointer_page_open`, the ESC exception, PAUSE-over-court with WAIT hidden, `runEnded` read beside `quitRequested`, the distinct `run_client` return, the `main()` creation-window loop |

**Cross-lane contract (fixed):** `Tavern::hearingPending() const`, `const HearingState& Tavern::hearing() const`, `void Tavern::plead(Plea)`, `bool Tavern::executed() const`, `bool Tavern::takeArrestRelease()` (existing; a `releaseHere` field on `ArrestReport` for SPARED/FINED), `Session::courtOpen() const`, `Session::runEnded() const`, `Session::runEndReason() const`, `sim::kMercyLine`, `sim::kJudgmentHoldSteps`.

**Guards that stay green:** `--eviction`, `--eviction=refused`, `--case` beat counts; ctest floor; both gate halves byte-identical cross-toolchain; population baseline `0x2646C1AAA2BA38DF` unmoved (no `WardActor` edits, no map, no signs). **The tavern/gate-workload baseline:** the arrest keeps its one draw at the same stream position, so the draw schedule does not move; but `skipHours` leaving `applyArrest` changes `elapsed_` at the arrest tick. If the `--tavern` half's workload arrests (test_contract does; the seam scout doubts the twin-gate workload does), that is **one declared baseline move at SIM's landing**, the combat precedent. The build checks and says which.

**Ship criteria (drive, through real input, at 640x360, photographed):**
1. Get tagged: lift in Cull's sight until `WANTED  HEAT ≥ 60` shows; kill a patron in view and see `WANTED FOR BLOOD`.
2. Get taken: CLOSING, the demand line, taken at reach, the dip, `TAKEN TO THE MISSION. HH:MM.`, the page.
3. Plead both ways on a PAPER charge: I DID IT lands FINED with the check block showing `+ 6 CONFESSED`; I DID NOT lands SPARED once and a doubled HELD once (scripted draws).
4. Take a fine with a short purse and read the `BONDSWORN N DAYS` rider; walk out of the Mission door.
5. Serve HELD: the contract with a due night dies, `dayNumber()` moves, heat reads 12, the Tarwalk release line prints the day.
6. Land BOUND and see temple standing move the greeting.
7. A murder: a nobody → THE ROPE → the drop → the plate → A NEW MAN → the creation window → a fresh run with a clean ledger. A Shepherd → COMMUTED, `CONDEMNED` on the HUD, recognised at 120‰, taken again on a lift → a PAPER hearing with `- 16 THE ROPE ONCE`.
8. Determinism: a scripted arrest + plea run twice, byte-identical world hash after release; the same script with the other plea diverges at and only at the plea step; the twin gate green on whichever baseline the build declares.
9. The two unreachability tests (court ↛ quay, defeat ↛ `executed_`) in `test_justice.cpp`.

---

## 9. Open rulings for Eli (each with the recommendation)

1. **Who presides.** The Flame's priest at the Mission; the Watch petitions and hangs. *Recommend: yes* (§2.1).
2. **Does coin buy anything at the bench?** No offering row, no bribe; the fine is a sentence, not a purchase; THE DOOR (what you already gave the Mission) is the only coin the Flame reads. *Recommend: no, exactly §2.8's rule.*
3. **Which offences execute.** The ROPE tier only: a witnessed murder (killing a watchman included) and a Skyrunner's second arrest with paper. Not repeat violence, not unpayable-plus-violent, never theft. *Recommend: as stated; the alternative on the table is adding nothing.*
4. **Can a witnessed murder be SPARED?** No: COMMUTED at best (mercy line 24), and mercy once. *Recommend: no.*
5. **May the priest spare a Skyrunner's hand** (THE HAND tier at ≥ 38 → HELD)? A court that cannot overrule the sergeant is a formality. *Recommend: yes.* Alternative: the hand is canon and un-sparable, the plea only moves the nights.
6. **Jail lengths.** 1–3 days as ruled, doubled on a failed denial; BOUND 5; COMMUTED 12; shortfall 1 day per 4 Royals to 7. *Recommend: as tabled.* The one number most worth a second look is COMMUTED's 12 (long enough to lose every open contract; the price of a life).
7. **The Wielder at the bench.** Does the Flame's pierce acquit a disguised Wielder, or is the bench the one place shed immunity does not return? *Recommend: leave the seam, build neither* (§7.2).
8. **"Bloodletter" at a murder hearing.** The diction rule allows the priest the word. *Recommend: one row of `court.blood` nearly says it* ("You took a life in the dark, the way the thing we do not name to laymen takes them.") and no common-folk line ever does. **Superseded by the fix pass:** the accused is a layman, so the priest says nothing of it, nearly or otherwise; the row was rewritten (§6.5).
9. **Bark rows as a content append** (`court.*` in `contract_barks.json`, the `watch.condemned` precedent) versus code literals. *Recommend: the append.*
10. **Hearing timing.** Heard at once, any hour (Daggerfall), versus a night in the K34 cell and the bench at 08:00. *Recommend: at once* for v1; the night is ceiling.
11. **Does a served sentence clear heat?** Heat := 12 after the skip, paper off; COMMUTED clears `murderer_`; nothing clears `condemned_`. *Recommend: yes, as stated.* (This also makes `kHeatAfterSentence` live; today it is dead by ordering.)
12. **Resist/escape.** No surrender verb, no menu: run (12 s, the roofs) or swing (Lethal, and killing him is the rope). *Recommend: nothing new.*
13. **Ambiguities the scouts flagged**, resolved here unless vetoed: `Sentence` is not extended (a new `Judgment` enum, §4.1); the release point for a cell is the shipped Tarwalk, for a fine the Mission door (K02's gate is not a travel target and adding one is a roster edit for another sprint); the drop is silent (no dormant asset is wired for it, none is authored); the end rows are `A NEW MAN` / `LEAVE` and no save row is shown until a save exists; the priest's face is not in the hero panel (FACES-SPEC is not built in the C++ client; text only, no new art).

---

## 10. Register literals (for the veto pass)

`TAKEN TO THE MISSION. 23:40.` / `THE MISSION -- A HEARING` / `WATCHMAN CULL LAYS THE PAPER ON THE TABLE.` / `THE WARD SAYS YOU PUT CANNIC DOWN IN THE GILDED GULL. THREE SAW IT.` / `THE WARD HAS YOU FOR TWO LIFTS AND A CRACKED BOX.` / `THE ROOFS, A SECOND TIME.` / `THE PAPER ASKS FOR A CELL.` · `THE HAND.` · `THE ROPE.` / `1 - I DID IT.` `2 - I DID NOT.` `3 - HEAR THE PAPER` `0 - BACK` / `1 - I HAVE NOTHING TO SAY.` / `[THE PRIEST WEIGHS]` / `THE LINES: 55 SPARED  38 FINED  14 HELD` / `THE LINE: 24 MERCY` / `+ 6 CONFESSED` / `+ 4 THE PRIEST IS A MAN` / `[SPARED]` `[FINED]` `[HELD]` `[BOUND]` `[THE HAND]` `[COMMUTED]` `[THE ROPE]` / `TWO NIGHTS. 17 ROYALS.` / `BONDSWORN 5 DAYS.` / `TURNED LOOSE ON THE TARWALK. DAY 4. 23:40.` / `THE PRIEST IS WAITING.` / `WANTED FOR BLOOD` / the priest: `I have read what you gave at this door. It is the only reason we are talking.` (`court.paper.door`, only when THE DOOR is above zero) · `You lied to the Flame's face. The Mission will remember the lie longer than the sentence.` · `The Flame does not want you. The ward may have you.` / the arrest's beat: `Watchman Cull: There is paper out on you and I am the man holding it. Walk.` then `TAKEN TO THE MISSION. 23:40.` over black / the end rows armed: `1 - A NEW MAN -- SURE? <key>` / the plate: `HANGED AT THE SALTGATE POST.` · `BY THE WARD. FOR CANNIC.` · `BY THE WARD. FOR THE SECOND RUNG.` · `THE FOURTH DAY. 23:52.` / `1 - A NEW MAN` `2 - LEAVE`.

---

## 11. The ceiling, named (not v1)

In priority order: **the Watch in the street** (the K21/K34 patrol recognising a wanted face on the Tarwalk; a `WardActor` edit and therefore a declared population-baseline move); **the cell as a place** (wake in K34's nine steel cells, walk out of the Impound gate: a spawn tile and travel-roster entries for K02/K34); the night in the holding cell before an 08:00 bench; the Flame's pierce at the bench (ruling 7); a `grace` bribe at the CLOSING stance; NPC criminals before the same bench (the Java `HeldPolicy`/`ExecutedPolicy` shape, the K21 gibbet cage with a body in it); the priest's face in the hero panel when FACES lands in the client; a save file, and `1 - THE LAST SAVE` on the plate the day one exists; banishment, never.

---

## Appendix: the Daggerfall crosswalk, one line each

Crime slot → a ledger with priors (better, kept) · legal reputation → heat (direct) · guards spawn and shout → Cull recognises and CLOSES (direct, shipped) · arrest by touch, no menu → TAKEN at reach (direct) · the court screen → the hearing page (built) · the charge read → the sheet from the ledger (adapt) · Guilty/Not Guilty → I DID IT / I DID NOT + HEAR THE PAPER (direct, plus the informed choice) · Streetwise percentile → the shown weighing with the arrest draw's band (adapt to same-roll law) · failed plea doubles → doubled and the Flame remembers the lie (adapt) · fine in gold → Royals, `fineFor` (direct) · prison = days jump → `skipHours` with consequences (direct, widened) · can't pay → bondsworn days (adapt, the tenure ruling's own instrument) · banishment → dropped, BOUND in its slot · no execution → **THE ROPE, the owner's addition and the game's only end** · no repeat escalation → the shipped Skyrunner ladder, kept · Lift = pickpocketing, Burgle = breaking and entering, Smuggle = smuggling, RoofRun = vagrancy/trespass (a status crime, Daggerfall's own precedent), Fence and Extort have no analogue, `Deed::Slew` = murder, arrears = loan default but Granadad sends it to the other court.

*Assembled by the justice design lead from the LAW MAP, DAGGERFALL REFERENCE and SEAMS scout reports (2026-09-10), read against combat/build a50fa23. Read-only; no file in either tree was touched.*