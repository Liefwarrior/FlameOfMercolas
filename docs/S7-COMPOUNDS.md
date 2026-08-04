# S7 — Compounds

What changed, in the order it was built. Two halves: the four S6 review findings, then the
compounds themselves.

Gate: `docker compose run --rm --build build` → **exit 0**, `ctest knows about 394 tests (floor: 394)`,
`100% tests passed, 0 tests failed out of 394`.
Windows half: `scripts\verify-windows.ps1` → **exit 0**, both reports byte-for-byte identical.

---

## Part one — the S6 findings

### The watchman's eye was untested where it was wired

- **Was:** `tavern.cpp` called `noticePermille(sack.illicitWeight(), ...)`. Swapping
  `illicitWeight()` for `illicitUnits()` left all 371 cases green — the only case that tested the
  claim drove the pure function, and nothing drove the call site. Four jars of quayfire is 96 drams
  against four twists of dust at 12; under the mutation both are four and the good-choice tradeoff
  the whole contraband economy rests on evaporates silently.
- **Now:** *a watchman's eye is on the load AT THE CALL SITE, not only in the arithmetic* drives
  `Tavern::tickWatch` over three nights with each sack and requires the heavier one to be seen
  sooner. Under the mutation the two numbers are identical and the case goes red.
- `docs/S6-CONTRACTS.md`'s mutation table said this had been killed. It had been killed in the
  function, not on the wire. The table is corrected in place and says so.

### Condemnation was an amnesty

- **Was:** `Tavern::tickWatch` returned early on `crimes.condemned()` — stance idle, no watchman, no
  notice, no arrest, permanently. Two Skyrunner arrests bought the rest of the game at zero risk, so
  the ward's harshest sentence was mechanically its safest state.
- **Now:** no early return. A condemned face is recognised at `kCondemnedRecognisePermille` (120,
  against a warrant's 45) whether or not paper is out, because the ward passed sentence on that face
  in public. Every subsequent arrest still empties the sack, takes the fine, kills the jobs those
  goods were for and jumps the clock. `Sentence::Condemned`'s own comment now states this rather
  than leaving it to be discovered.

### The topic row ate the authored name

- **Was:** `TAKE 4 SCALPS FOR KEEPER VETCH` in an eighteen-glyph column printed
  `8 TAKE 3 SCALPS.` beside `9 TAKE 4 SCALPS.` — two different jobs for two different people,
  distinguishable by one integer, with the proper noun the sprint existed to prove cut off the end.
  `7 SIGN ON: THE.` named nothing at all.
- **Now:** two changes.
  1. The patron leads: `CRUMB - 4 SCALPS`. Sixteen glyphs, fits the column with its number, and two
     bounties on one board are told apart by the first word. The verb lives in the brief.
  2. A **detail line** spells the picked row out in full, whatever its length. It sits in the TOP
     band under what the speaker said, and that is a measurement: at 1280×720 the bottom band is 154
     pixels between the exclusion rectangle and the frame edge — four grid rows and thirty spare
     pixels, two short of a fifth row. A detail line under the grid would have been clipped off the
     bottom of the frame, which is the same class of bug it exists to fix.
- `docs/frames/s7-topic-detail.png` is the proof: the grid reads `>7 SIGN ON: THE.` and the line
  above it reads `> SIGN ON: THE WATCH`.
- `--cursor=N` was added to the client so that frame could be taken at all.
- The S6 frame caption that quoted `8 TAKE 3 SCALPS.` as intended output is corrected.

### A HUD line ran off the frame

- **Was:** `hud.alert` was drawn centred, unwrapped and unclipped, clamped only at the left margin.
  `Session::say()` clips what it composes to a guessed 56 columns; the bouncer's 68-character
  warning was assigned into `hud.alert` straight off the tavern and never met that clip.
  `docs/frames/s6-skyrun-quiet.png` shows the last two words drawn off the right edge, mid-glyph.
- **Now:** `clipToWidth(text, pixels, scale)` in `hud.cpp`, applied where the frame width is
  actually known. Every alert from every source is safe by construction and no future caller has to
  remember a number. The case drives a 68-character warning at 320, 640 and 1280 wide.
- **Not photographed.** The overlong warning only appears while barred in the Gull and no scripted
  line reaches that state; the claim is proved by the case and not by a frame.
- While in there: the bouncer's final warning was a hardcoded English sentence in `tavern.cpp`, in a
  project whose stated discipline is that no proper noun in a system's output is chosen by a
  programmer. It is authored now, four rows, in `content/raws/barks/house_barks.json`.

### The board asked for goods the ward could not supply

- **Was:** two bales, three units, ONE good drawn per night out of three. A night landed at most six
  units of a good the player had a one-in-three chance of wanting, against a board asking 3-6 flower,
  1-3 dust *tonight* and 2-5 quayfire. Only Cull's scalp bounty — fed by the vermin, not by the boat
  — was reliably completable.
- **Now:** three bales, and the good is drawn PER BALE out of what tonight's own board asked for. A
  smuggler's boat lands what somebody already paid to have landed. The order of `postContracts` and
  the restock is now load-bearing and says so.

### The named FETCH object was a decoration

- **Was:** `contracts.json` authors "a christening cup with two names filed off it" and S6
  substituted it into prose and nowhere else. The stash held an anonymous `Artifact` count, so any
  strongbox settled any recovery job.
- **Now:** `Contract::thing` carries the object and `Contract::recovered` counts how many were
  lifted **for that job**. `ContractBoard::recoverPiece()` gives a cracked box to the earliest live
  recovery job that is still short and answers with which one; `turnIn` refuses an artifact contract
  that has not recovered its pieces. Pieces already in the sack before anybody asked are fenced
  goods, not somebody's christening cup.

---

## Part two — the compounds

`native/include/granadad/sim/compound.hpp`, `native/src/sim/compound.cpp`,
`native/tests/test_compound.cpp`, `content/raws/compounds/compounds.json`.

### The roll

DOCKS-GAZETTEER §2.8's own table, as data. Five plots: C1 The Quayward (charged, Ceffa Quayward),
C2 The Netters' (pledged to Fenner), C3 Saltgate Terrace (charged), C4 The Gullet (vacant) and the
glebe (45 hovels, Church ground). Every `denDuke`, `pledgedTo` and `priest` id is refused at load
unless `notables.json` has it — the same gate the contract board puts on its brokers.

Three tiers and nobody owns the earth: the Flame holds the plot, a Den Duke holds the charge and
pays a charge-rent, an owner owns the house outright and pays a **ground penny** on the earth under
it. A **rooftop lodger rents from the house-owner beneath them, not from the Duke**, so every
house-owner is a petty landlord. **Wastrels** own nothing and owe nothing.

Household sizes are not invented: they are drawn from `content/raws/actors/household.json`'s own
weights `{1:20, 2:35, 3:25, 4:15, 5:5}` — the canon-derived distribution §2.5 cites. That produces
**131 households, 315 heads** across the five plots.

### The farm

Each plot has a courtyard of beds and a crop. A bed ages; hands turn it over; at harvest the yield
is what was put into it. `bedYield(full, tends, needed)` is the whole rule and it is a public
function so a case can drive it: full yield for a bed worked every third day all season, a
proportional share for one worked less, and **nothing at all** below a third of what the crop wanted
— that bed is weeds and the season is lost, not thin.

Hands come from `farmHands(plot)`, and that one function is the join between the two systems.

### The bond is the pipe

§2.8: *"while the bond runs, the bondsworn's wage is the bondholder's and their work is the
bondholder's yard."* `farmHands` counts a bonded household in the **bondholder's** yard and not in
the one it sleeps in. So:

- a house-owner who cannot find the penny leases themselves to their Duke — but only if that Duke's
  yard is actually short of hands, because he owes keep for every pair he holds;
- a Guild buys the paper on a rival's hands (`transferBond`) and the labour moves with it;
- the compound it left grows less, which the case *buy the paper on a compound's hands and its
  courtyard comes up thin* proves by running two identical wards for 1,400 days and comparing the
  victim plot's harvest;
- a court-ordered bond cannot be sold on to anyone else;
- **and none of it reaches the glebe.** §2.8: *"a Duke who wants labour cannot get it from the
  glebe."* Church ground is never let to anyone, including the people on it — which is why the
  Mission's alms traffic is the only thing between a hovel-row wastrel and a hungry quarter.

### The hearing

A Den Duke cannot turn a family out. He petitions, and Father Maell weighs — years kept on the
ground, actors under the roof, labour already given under a former bond, whether the ward is in a
hungry quarter, the arrears proved by the roll, how long the Duke has been offering terms, and the
Duke's own conduct. Six outcomes and only one is eviction: dismissed, stay, abatement, bond ordered,
distraint, charge revoked.

**The offering is not a term in that sum.** §2.8: *"it is an offering and not a fee, and it does not
buy the verdict."* `weighPetition()` is public and pure so the case can ask the same question on the
same morning with offerings of 0 and 400 and require the same verdict and the same weight out of
both. The coin leaves the Duke's purse either way.

A stay is a real quarter of grace — the penny does not fall and the arrears do not grow — rather
than a comment saying so.

**The player is not exempt.** A Duke petitions against whoever is behind on his ground.

### The player's verbs

Every one is a thing an NPC household already does.

| verb | what it does |
|---|---|
| `leaseRoof(plot)` | takes a roof deck for a quarter. The rent goes to the house-owner beneath, never to the Duke. C1 refuses: her house-owners chose not to let their roofs. |
| `buyHouse(plot)` | the house is yours outright; the ground under it never will be. You owe the Duke a penny each quarter and you become landlord of the roof huts above you. |
| `petitionForCharge(plot)` | a vacant charge is a prize. Refused on a plot somebody holds; refused on the glebe, which is not on offer to anybody, ever. |
| `collectRent()` | a quarter's roof rents and, as Duke, ground pennies. |
| `petitionAgainst(id, offering)` | only about your own ground. |
| `goBondsworn()` | lease yourself. Refused with nothing owed: a serf is a house-owner who had a bad quarter. |
| `transferBond(id, plot)` | buy the paper on somebody's hands. |

---

## The soak — the sprint's acceptance

`granadad-twin-gate --ward-soak 730`, and `granadad.exe --ward[=DAYS]` prints the same report. It is
a **ctest entry**, so the balance bar is enforced by the build: the run exits non-zero if starvation
passes `kStarvationBarPermille` (50 ‰ — the Java build held serf starvation at or below 5%) **or**
if every courtyard larder sits at its cap, because an economy with nothing scarce in it is not
balanced, it is switched off.

730 days, seed `0x4752414E41444144`, run natively on Windows:

```
the compounds of the Docks -- 730 days
  plots        5   households 131   heads 315

  THE LAND
    THE QUAYWARD      charged  beds 20  hands 3   larder 848   heads 36   bonded 4  starving 0  harvests 292  failed 0
    THE NETTERS       pledged  beds 16  hands 4   larder 843   heads 49   bonded 0  starving 0  harvests 293  failed 0
    SALTGATE TERRACE  vacant   beds 14  hands 4   larder 0     heads 58   bonded 0  starving 0  harvests 254  failed 0
    THE GULLET        vacant   beds 8   hands 4   larder 84    heads 56   bonded 0  starving 0  harvests 197  failed 0
    THE HOVEL ROWS    glebe    beds 24  hands 9   larder 0     heads 116  bonded 0  starving 0  harvests 354  failed 0

  THE FOOD
    grown        86753
    imported     142350
    eaten        225390
      courtyard  71077
      market     142385
      keep       522
      alms       10837
    over cap     13871
    stored now   2236  (market 195)
    harvests     1390   beds failed 0
    bed-tends    19301 of 21170 wanted

  THE GROUND
    quarters     8
    pennies      73441 paid, 6559 short
    roof rent    1883
    charge-rent  71874
    bonds        42 taken, 39 discharged
    petitions    19
      dismissed   2
      stay        5
      abatement   2
      bond        6
      distraint   3
      revoked     1
    distrained   3 houses, 1 lodgers turned out with them

  THE BELLY
    head-days    229950
    hungry       7728  (3.3%)
    STARVING     2369  (1.0%, bar is 5.0%)
    worst day    46 heads
```

**Food produced:** 86,753 rations off the land plus 142,350 landed by the quay = 229,103 minted.
**Consumed:** 225,390 eaten — 31.5% straight out of the courtyards, 63.2% bought at a counter, 4.8%
the Mission's night-soup, 0.2% a bondholder's keep. **Stored at the end:** 2,236, of which 195 is
the market's counter and the rest is on larders and shelves. 13,871 rations went over a larder cap
across two years and are accounted rather than quietly dropped.

**Starvation: 1.0%** of head-days, against a bar of 5.0%. **Hunger** (missed a meal on a given day,
which is not the same thing) 3.3%. Worst single day 46 heads.

Three things worth reading out of that table:

1. **The land is doing real work and is not a faucet.** 19,301 bed-tends of 21,170 wanted — the ward
   is 91% worked, which is why nothing failed. C1 is the one compound genuinely short of hands
   (20 beds, 36 heads) and it is the only one taking bonds.
2. **Saltgate Terrace reads `vacant`, and it is not authored that way.** The Flame revoked his
   charge during the run: his charge-rent outruns what his tenants pay, exactly as §4.4 says
   ("mansion-poor and tenant-rich… himself in arrears to the Flame"). That is the sixth verdict
   firing on its own, and it leaves a second vacant charge on the board for a player to petition for.
3. **All six verdicts occur.** 19 petitions: 2 dismissed, 5 stays, 2 abatements, 6 bonds ordered,
   3 distraints, 1 charge revoked. Three houses passed and one roof household went with the family
   below them, having been party to nothing.

`granadad-twin-gate --ward --ticks 90000` runs the ward twice in one process — 90,000 ticks, which
is the number it takes to cross a day boundary at 86,400 seconds to the day — and both runs agree:
combined hash `0xE95257B603A1F71B`, 118,227 bytes of report, identical.

---

## Verification

| | |
|---|---|
| `docker compose run --rm --build build` | exit 0, 394/394, floor 394 |
| `scripts\verify-windows.ps1` | exit 0, both cross-toolchain reports identical |
| `granadad-twin-gate --ward-soak 730` | exit 0, starvation 1.0% |
| `granadad-twin-gate --ward --ticks 90000` | exit 0, both runs identical |
| `granadad.exe --smoke=40 --screenshot=…` | exit 0, `docs/frames/s7-smoke.png` |
| `granadad.exe --contract` | exit 0, `work beats=6/6` |
| `granadad.exe --skyrun` | exit 0, `stages=9 arrests=0` |
| `content/` | 2 files ADDED, 0 modified, 0 deleted (`git diff --name-status ebc02e8 -- content/`) |

Frames: `s7-smoke.png` (the authored spawn, 27 lamps, `art=custom`), `s7-work-offered.png` (the
board with the patron leading each row), `s7-topic-detail.png` (`>7 SIGN ON: THE.` in the grid and
`> SIGN ON: THE WATCH` spelled out above it), `s7-skyrun.png` and `s7-skyrun-quiet.png`.

---

## What S7 does NOT do

Marked in the code as well, at the foot of `compound.hpp`.

- **Nobody walks.** A household is a row on the roll, not a body on a tile. No household here is an
  `Actor`, none is one of the fourteen people in the Gilded Gull, and no compound in
  `docks_surface.trojsav` is bound to a plot by coordinate. The ward's economy is simulated; the
  ward's geography is not joined to it.
- **The wage is a faucet.** Coin enters at a flat rate per head per day and leaves at the counter and
  the Duke's gate. The FOOD supply is conserved and accounted; the money is not.
- **The market has no shopkeeper.** Rations appear on the ward's counters daily and are sold to
  whoever has the coin. Which counter, whose it is, and whether a household can walk to it are not
  modelled.
- **A petition is brought, never attended.** There is no room, no queue, no conversation and no bark
  spoken at a hearing. The barks are authored in `house_barks.json` and only the report reads them.
- **No crop failed in the soak.** The failure rule is proved directly by the `bedYield` case and
  causally by the comparative harvest case; it did not fire on its own in 730 days because the ward
  is 91% worked. That is the balance being right, not the rule being untested — but it is not the
  same as having seen it happen in the wild.
- **The overlong HUD alert is not in a frame.** See above.

Carried forward and still open, correctly marked in code: nothing casts (`spellforge.hpp`); a leap's
parabola is uncollided; there is no cell, only a clock that jumps (`watch.hpp`); the ward the
contract briefs name is scenery (`contract.hpp`).
