# CHARGEN-DAGGERFALL-DRAFT — the creative content for the Daggerfall flow

**Status:** DRAFT FOR ELI'S LINE-EDIT. Content only. Nothing here is wired; nothing here
edits `content/` — every number graduates into `content/raws/chargen/*.json` only after
sign-off, in a later build. The implementation plan is task #92's; this doc fills the
slots that plan deliberately left as drafts, updated for the two fresh rulings (axes are
IDENTITIES, not places; the Daggerfall flow becomes the top level of the Origin screen).

**Engine facts this doc builds on (cited once, used throughout):**
- 19 designatable skills — `content/raws/skills/skills.json` minus `the_flame`, which
  `Chargen::designate` refuses (`native/include/granadad/sim/chargen.hpp:24-31`).
- Sheet shape 3 Primary / 3 Major / 6 Minor (`chargen.hpp:91-93`), starting levels
  30/15/5 (`chargen.hpp:103-105`), attribute pool 24, per-attribute cap 15
  (`chargen.hpp:123-126`), over base 40 per attribute
  (`native/include/granadad/sim/attributes.hpp:81`).
- Coin base 40 (`native/include/granadad/sim/tavern.hpp:250`, `kPlayerStartingCoin`).
- hpMax base 24 (`native/include/granadad/sim/actor.hpp:150`, `kActorHealth`).
- Heat 0–100, warrant at 60, lapses below 20 (`native/include/granadad/sim/crime.hpp:87-98`).
- Faction standing lives in `FactionLedger` over the five factions of
  `content/raws/factions/factions.json`; `addStanding` mirrors the halved opposite onto
  declared rivals (`native/include/granadad/sim/faction.hpp:210-212`); the only declared
  rival pair is watch ↔ skyrunners (`content/raws/factions/ranks.json:30,54`).
- Named-notable dispositions are `SocialLedger` state, scale −100..+100, with a
  `seed()` entry point built for exactly this ("the seeded starting standing an authored
  relationship implies", `native/include/granadad/sim/social.hpp:103-104,167-170`).

---

## 0. What needs Eli's decision

1. **Axis names** — pick one trio from §1 (or reject all three; the registers stand).
2. **Calling roster** — sign off §2, including two refinements from the prior plan's
   roster: BONDSWORN dropped to a biography question (it is a debt-state, not a trade —
   `DOCKS-GAZETTEER.md:188-196`), COPY-CLERK added to fill the letters register.
3. **Quiz** — per-question line edits on §3's ten; the tie-break rule in §3.3.
4. **Biography** — per-question number edits on §4's twelve; the zero-sum bookkeeping
   note in §4.1.
5. **Custom path** — §5's price list and the caps (3 advantages / 3 disadvantages, P
   clamp, multiplier line).
6. **The restructured Origin screen** — §6's mock.

---

## 1. The three axes — naming options (RULING 1)

What the axes measure is settled and does not move with the name:

| Axis | Register | Daggerfall analog |
|---|---|---|
| A | honest labor, directness, standing your ground | Warrior |
| B | indirection, timing, moving unseen, what the tide brings | Thief |
| C | letters, weighing, doctrine, the long game | Mage |

The rejected trio (THE QUAY / THE ROOFS / THE FLAME) named places. The ruling: name them
as what a person IS. Three options, each internally consistent, all drawn from words the
raws already use for people. Eli picks.

**Option 1 — THE HAND / THE MUDLARK / THE DISCIPLE.**
The ward's three childhoods. "Hand" is the ward's own everyday word for a working person
— hired hands, counter-hands, knife-hands, floury hands, all through
`content/raws/names/notables.json` (e.g. :83, :103, :265, :324). "Mudlark" is a way of
living on the tide-line, not an employer — Tarry Jek "the strand mudlark"
(`notables.json:68-73`), the mudlark kids of the wastrel dossier
(`DOCKS-GAZETTEER.md:606`). "Disciple" is a person the raws spawn
(`content/raws/actors/disciple_of_the_flame.json`) and the Temple ladder's first rung
(`ranks.json:44`). Case: these are dispositions before they are employments — you can be
a hand who never musters, a mudlark who never steals, a disciple who never takes orders.
Closest to Warrior/Mage/Thief in kind.

**Option 2 — THE BONDSWORN / THE TENANT / THE DISCIPLE.**
The doorway rungs of three real ladders: Dockhands rank 1 (`ranks.json:10`), Skyrunners
rank 1 (`ranks.json:32` — also Finch's epithet, "the quiet tenant",
`notables.json:220`), Temple rank 1 (`ranks.json:44`). Case: the quiz's verdict is
literally the first rung you were always going to stand on, and the tally table maps
straight onto the faction ladders. Risk, stated: "bondsworn" is §2.8's word for indebted
labor (`DOCKS-GAZETTEER.md:188-196`) — as the HONEST-LABOR identity it reads as someone
who sold their years, which may not be the valor Eli wants on the Warrior slot.

**Option 3 — THE DOCKHAND / THE SKYRUNNER / THE ALMSBEARER.**
Each axis named in the vocabulary of its anchor faction: The Dockhands' own name for
their people (`factions.json:29-32`), the roofs' name for their best
(`DOCKS-GAZETTEER.md:74`, `ranks.json:35`), the Mission's name for the one who carries
(`ranks.json:45`). Case: the verdict reads as "the quays already have a word for you,"
and the three words advertise the three factions the axes feed. Risk, stated: Dockhand
is also a job title, which is adjacent to the ground the first rejection stood on.

---

## 2. The calling roster — nine trades

Prior plan roster: Dockhand, Netter, Deckhand, Stallkeep, Watch Runner, Roof-Tenant,
Almsbearer, Mudlark, Bondsworn. Two refinements against the registries:

- **Bondsworn OUT of the roster.** §2.8 defines bondsworn as a debt-state a house-owner
  falls into, not a trade anyone works by choice (`DOCKS-GAZETTEER.md:188-196`). It
  moves to biography question B2, where a family's bond can mark a character of ANY
  calling. It also frees the word for axis Option 2.
- **Copy-Clerk IN.** The letters register had one calling (Almsbearer) against three or
  four per other axis. The counting-row's copy-clerks are real registry people — Crell's
  "four hired clerks" (`notables.json:12`), Widow Sedge's lodgers, "counter-men and
  copy-clerks", and the vanished clerk Tam Sedge (`notables.json:412`). Honest flag: no
  clerk job exists in `content/raws/jobs/jobs.json` yet; the calling anchors to the
  Merchant Row and the nearest real job (`trade.trader`, `jobs.json:305`) until one does.

Each sheet fills 3 Primary / 3 Major / up to 6 Minor (a short Minor row is legal and
unpenalized — Gabri's own sheet ships 5 of 6, `content/raws/companions/gabri.json:45`).
Attribute spends each sum to exactly 24 with no value over 15 (`chargen.hpp:123-126`),
over base 40. Axis codes below are (dominant/secondary) for §3.3's tally table.

| # | Calling | Axis | One line | Anchor |
|---|---|---|---|---|
| 1 | **Dockhand** | A pure | You carry what the ward eats and you are paid by dusk | Dockhands; `serf.laborer` (`jobs.json:21`, trains kit_keeping) |
| 2 | **Deckhand** | A/B | Honest hands on a hull that answers only to the tide | Dockhands; `maritime.sailor` (`jobs.json:257`, trains seacraft) |
| 3 | **Watch Runner** | A/C | You carry the post's paper and stand where you are told | Watch rank 1 "Runner" (`ranks.json:56`); `watch.patrol` (`jobs.json:193`) |
| 4 | **Netter** | B/A | The tide does the work; you are there when it does | Dockhands; the Netters' Company (`chapters.json:53-63`); `maritime.fisher` (`jobs.json:273`, trains fishing) |
| 5 | **Mudlark** | B/C | You walk the low water and you know what a thing is worth | Wastrel; `wastrel.streetlife` (`jobs.json:125`); Tarry Jek's trade (`notables.json:66-73`) |
| 6 | **Roof-Tenant** | B pure | A quiet lodger the Watch has never had a reason to write down | Skyrunners rank 1 "Tenant" (`ranks.json:32`); Finch (`notables.json:218-225`) |
| 7 | **Almsbearer** | C pure | You carry soup and doctrine to doors that distrust the garb | Temple rank 2 title (`ranks.json:45`); `clergy.acolyte` (`jobs.json:241`, trains channeling) |
| 8 | **Stallkeep** | C/A | A counter, a scale, and a price you will stand behind | Merchants rank 1 (`ranks.json:21`); `trade.stallkeep` (`jobs.json:289`, trains streetwise) |
| 9 | **Copy-Clerk** | C/B | You copy what the ward writes down and notice what it doesn't | Merchant Row; the counting-row (`notables.json:405-412`) |

### 2.1 The sheets

Skill ids are `skills.json`'s own. Format: Primary (start 30) / Major (15) / Minor (5);
attribute spend over base 40.

**1. DOCKHAND** — P: grit, kit_keeping, open_hand · M: heavy_arms, fieldcraft,
shieldwall · m: harness, streetwise, seacraft, fishing, lancework, mixtures ·
MGT +10, VIG +10, AGI +2, WIT +2 → 50/42/50/42 (MGT/AGI/VIG/WIT).

**2. DECKHAND** — P: seacraft, grit, kit_keeping · M: open_hand, fishing, skyrunning
(rigging is climbing, `skills.json:88`) · m: sidearms, heavy_arms, shieldwall,
streetwise, harness, mixtures · MGT +6, AGI +8, VIG +8, WIT +2 → 46/48/48/42.

**3. WATCH RUNNER** — P: kit_keeping (the Watch ladder's own skill, `ranks.json:52`),
streetwise (what patrol trains, `jobs.json:200`), shieldwall · M: lancework
(spear-dominant infantry canon, `DOCKS-GAZETTEER.md:591`), grit, harness · m:
open_hand, sidearms, heavy_arms, bladework, linkcraft, fieldcraft ·
MGT +8, AGI +2, VIG +8, WIT +6 → 48/42/48/46.

**4. NETTER** — P: fishing, seacraft, kit_keeping (net-mending — Grandmother Withy's
own craft, `notables.json:294`) · M: fieldcraft, grit, streetwise · m: sidearms,
open_hand, mixtures, skyrunning, harness, shieldwall ·
MGT +2, AGI +10, VIG +6, WIT +6 → 42/50/46/46.

**5. MUDLARK** — P: streetwise (black-market pricing is the skill's own text,
`skills.json:133`), skyrunning, fishing (reading water, `skills.json:169`) · M:
cracksmanship, mixtures, grit · m: sidearms, open_hand, seacraft, kit_keeping,
fieldcraft (sixth slot left empty — a mudlark's sheet is honestly thin) ·
MGT +2, AGI +10, VIG +4, WIT +8 → 42/50/44/48.

**6. ROOF-TENANT** — P: skyrunning, cracksmanship, streetwise · M: sidearms,
open_hand, grit · m: mixtures (nighthawk venom is roofs canon, `skills.json:118`),
harness, kit_keeping, bladework, linkcraft (sixth empty) ·
MGT +2, AGI +14, VIG +2, WIT +6 → 42/54/42/46.

**7. ALMSBEARER** — P: channeling, linkcraft (the Temple ladder's own skill,
`ranks.json:40`), mixtures (draughts for the sick) · M: streetwise, kit_keeping, grit ·
m: open_hand, fieldcraft (the Mission garden, `DOCKS-GAZETTEER.md:120-123`), fishing,
seacraft (two slots left empty — the Mission does not pad a sheet) ·
MGT +4, AGI +2, VIG +6, WIT +12 → 44/42/46/52.

**8. STALLKEEP** — P: streetwise, kit_keeping, linkcraft (two sets of books means a
lettered counter, `notables.json:103`) · M: mixtures, fieldcraft, grit · m: sidearms,
open_hand, seacraft, fishing, channeling, harness ·
MGT +4, AGI +4, VIG +6, WIT +10 → 44/44/46/50.

**9. COPY-CLERK** — P: linkcraft, streetwise, kit_keeping · M: mixtures, channeling,
cracksmanship (lockboxes and strongrooms are a clerk's furniture,
`DOCKS-GAZETTEER.md:92-94`) · m: sidearms, open_hand, grit, seacraft, fishing (sixth
empty) · MGT +2, AGI +4, VIG +4, WIT +14 → 42/44/44/54.

---

## 3. The quiz — ten questions

Voice per the approved sample: second person, concrete Trojian nouns, past-defining
moral dilemmas, three defensible answers each carrying its own rationale clause, each
scoring exactly one axis. Recurring fixtures imply one coherent Docks childhood (the
Daggerfall authoring rule, `ELDER-SCROLLS-REFERENCE.md:283-288`): the ground penny and
the Den Duke's man, the tide, the Mission's soup line, the notice board, the pay-night,
the roofs.

**Q1.** Quarter-day, and your family's ground penny is short by three Royals. The Den
Duke's man is at the gate. You —
- (A) meet him at the gate and say it plain: the penny is short, and your hands will
  make up the difference — a debt owned out loud is half worked off.
- (B) take the roofs to the strand. The tide is out, and three Royals of wrack lies on
  the mud for whoever beats the gulls to it.
- (C) fetch the household's rent-book and walk it to the Mission — the arrears are wrong
  by the roll's own arithmetic, and a priest will weigh a page over a fist.

**Q2.** The crane gang's boss pays you a child's wage for a hand's work, and both of you
know it. You —
- (A) put down the hook and tell him so, in front of the gang. A wage argued in the open
  stays argued.
- (B) take the wage and learn which nights the tally-clerk leaves early. Knowing a
  yard's habits is worth more than a day's difference.
- (C) count the crates yourself for a week and show him his own tally. A boss cheats a
  child who cannot add.

**Q3.** An older Gullet tough takes your basket at the Bottom link every dusk this
quarter. You —
- (A) stand once and take the beating that comes with keeping it. He needs to win every
  dusk; you only need to cost him one.
- (B) go home a different way each night. The Gullet has more roofs than he has evenings.
- (C) ask Gullet Mag what he answers to. Every tough in the Gullet pays respects to
  someone, and respects can be spoken to.

**Q4.** Old Cobb's best rat-dog slipped its cage and hangs snagged in the outfall grate,
and the tide is rising. You —
- (A) wade in and take her out now. Cold water forgives the quick and drowns the careful.
- (B) use what you know: the grate lifts on a pin the mudlarks keep quiet, and the tide
  gives you until the turn. So does the dog.
- (C) run for Cobb. A dog in a panic bites its rescuer; it will let its own man touch it,
  and you know exactly where he drinks.

**Q5.** Brann's counter-hand miscounts your mother's change, a half-Royal in your favor,
and no one has seen it. You —
- (A) hand it back across the counter before you have decided anything. What is done in
  the open cannot be held against you.
- (B) keep it. The counter has kept plenty of yours, and the sea does not hand things
  back either.
- (C) return it quietly at shutter-time. A chandler who trusts your family's tally will
  carry them through a lean quarter, and lean quarters come.

**Q6.** Pay-night at the Bilge, and your father is drunk with the whole packet on him.
A crimp is working the room. You —
- (A) walk in and carry him home yourself, past whoever objects. Let the room watch.
- (B) wait in the arcade shadow until the crimp picks his man, and be the reason your
  father is not it — out the back, before anyone chooses him.
- (C) pay Redda's hand a copper to bunk him upstairs till morning. The Bilge keeps what
  it is paid to keep, and a copper is cheaper than a wage.

**Q7.** The Watch nails a warrant to the notice board naming a Rows lodger you know was
at sea that night. You —
- (A) climb the Rise and tell the sergeant his paper is wrong, and give your name when
  he asks for it.
- (B) say nothing to the post. Word can cross the roofs and meet the man's ship before
  the Watch meets the berth.
- (C) copy the warrant's dates and walk them against the Weighhouse berth ledger. Paper
  argues best with paper.

**Q8.** A hungry quarter, and the Mission's soup line is long. You are in it, and a
bigger kid shoves in ahead of a smaller one. You —
- (A) put him back where he was. A line only works if someone is willing to hold it.
- (B) leave the line to those who cannot do otherwise — the strand feeds whoever works
  the low water, and the tide is out.
- (C) take the ladle end and serve. The last bowl always finds the one who served, and
  the priest's eye finds them too.

**Q9.** On the roof-decks you find a dropped purse by the crawl-gap: heavy, not yours,
and roof-money belongs to whoever asks about it least. You —
- (A) call "whose is this?" down the deck, and hold it up. A thing done in the open
  cannot be made into something else later.
- (B) leave it exactly where it lies and remember who comes back for it. What crosses
  the roofs matters more than what it weighs.
- (C) carry it to Gullet Mag and let the ruin's gray voice judge. A favor with the judge
  outlasts any purse.

**Q10.** You wake at black tide and see a hull berth with no lights and no bell. In the
morning everyone says no ship came in. You —
- (A) tell the Watch at first bell, and stand by it when they ask how you know.
- (B) say nothing — and be on the quay, unseen, at the next black tide.
- (C) mark what you could read of her name and look for it in the Weighhouse's posted
  list. A ship that is not on the list is a fact worth keeping.

### 3.1 Feedback while answering

Daggerfall brightens three constellations as answers land
(`ELDER-SCROLLS-REFERENCE.md:280-281`). Trojian skin: three identity cards along the
panel's foot — the chosen axis names of §1 — each filling a small meter as its answers
accumulate. Same charm, zero new widget: it is a text row in the same
`DialogueViewState` panel everything else draws through
(`native/include/granadad/render/creation.hpp:55-67`).

### 3.2 Verdict is never a trap

The tally's verdict card offers the calling; the player may accept or decline back to
the §2 roster list — Daggerfall's own rule (`ELDER-SCROLLS-REFERENCE.md:281-283`).

### 3.3 Tally → calling

Ten answers, one axis each: a triple (A,B,C) summing to 10.

| Condition | Calling |
|---|---|
| A ≥ 6 | Dockhand |
| A dominant (4–5), B ≥ C | Deckhand |
| A dominant (4–5), C > B | Watch Runner |
| B ≥ 6 | Roof-Tenant |
| B dominant (4–5), A ≥ C | Netter |
| B dominant (4–5), C > A | Mudlark |
| C ≥ 6 | Almsbearer |
| C dominant (4–5), A ≥ B | Stallkeep |
| C dominant (4–5), B > A | Copy-Clerk |

Tie for dominant (5/5/0, 4/4/2, etc.): the tied axis the player's Q10 answer scored
wins; if Q10 scored the third axis, fixed order A > B > C. Deterministic, and Q10 is the
question written to carry that weight. **Needs Eli's nod.**

---

## 4. Biography — twelve questions

One shared set for v0 (Daggerfall keys a set per class,
`ELDER-SCROLLS-REFERENCE.md:308`; per-calling sets are a later pass). Player may answer
all twelve or take RANDOM, Daggerfall's own option (`ELDER-SCROLLS-REFERENCE.md:259`).
Devin/Gabri never see these — their history is their raws' own.

### 4.1 The levers (all real, nothing else used)

| Lever | Base | Range used | Mechanism |
|---|---|---|---|
| Skill delta | calling sheet's start level | +2..+8 (ceiling +12 reserved for line-edit) | added onto the designated start before `SkillTrack::setLevel` |
| Coin | 40 (`tavern.hpp:250`) | −20..+15 | starting purse |
| Faction standing | 0 with all five | ±2..±8 | direct row writes at chargen — NOT via `addStanding`, whose rival mirror (`faction.hpp:210-212`) would double-count; zero-sum is enforced at authoring time instead: every answer's five-faction deltas sum to 0 |
| Notable disposition | 0 (a stranger, `social.hpp:148`) | ±5..±15 | `SocialLedger::seed` (`social.hpp:167-170`), scale −100..+100 |
| Heat | 0 | 0..+15 | starting heat; warrant at 60 (`crime.hpp:96`), so the worst bio start is a quarter of the way to paper |
| hpMax | 24 (`actor.hpp:150`) | −4..+4 | starting hpMax |

Daggerfall's own effect vocabulary is the model — Skill +6 typical, gold at
+100/200/500 against its base, Rep ±2..±10, one mandatory-malus question, some pure
flavor (`ELDER-SCROLLS-REFERENCE.md:309-315`). Coin deltas here are scaled to the 40
base. No item grants: no item lever exists in chargen, so none is used.

### 4.2 The questions

**B1. Who taught you your letters?**
- a) No one. The quay taught your hands instead. → kit_keeping +6, coin +5.
- b) The Mission taught you its catechism, between one bowl of soup and the next. →
  linkcraft +4, channeling +2; temple +4, merchants −2, skyrunners −2;
  seed Maell +10 (`notables.json:35-42`).
- c) A copy-clerk on the counting-row taught you, for half a Royal a month. →
  linkcraft +6, streetwise +2; coin −5; seed Widow Sedge +10 (`notables.json:405-412`).
- d) You taught yourself, off the public shelf. → linkcraft +8; coin −5.

**B2. The Quarter-day it went wrong. What did your family do?**
- a) They paid clean, every quarter, the way they always had. → coin +10;
  merchants +2, dockhands +2, skyrunners −4.
- b) They went bondsworn, and you worked your father's bond beside him
  (`DOCKS-GAZETTEER.md:188-196`). → grit +6, fieldcraft +4; coin −10; dockhands +6,
  watch −2, merchants −4.
- c) They were roofed — turned out of the house — and you grew up on a roof deck
  (`DOCKS-GAZETTEER.md:311-318`). → skyrunning +6, streetwise +2; hpMax −2;
  skyrunners +6, watch −4, merchants −2; seed Finch +5 (`notables.json:218-225`).
- d) They squatted the glebe and paid token alms instead of the ground penny
  (`DOCKS-GAZETTEER.md:245`). → fieldcraft +6, fishing +2; coin −5; temple +4,
  dockhands −2, merchants −2; seed Maell +5.

**B3. Where did your first wage go?**
- a) Every copper of it went into the household jar. → grit +2; coin +5; dockhands +2,
  skyrunners −2.
- b) Fenner took it across his counter, against your mother's pledge-paper. →
  streetwise +2; coin −5; seed Fenner +10 (he remembers punctual blood,
  `notables.json:116-123`).
- c) You drank it at the Bilge in a single pay-night. → streetwise +4; coin −10;
  heat +5; seed Redda +10 (`notables.json:228-235`).
- d) It bought the knife you still carry. → sidearms +4; coin −5.

**B4. Why does the Watch know your face?**
- a) You ran messages up the Rise for the post. → kit_keeping +2; watch +6,
  skyrunners −4, dockhands −2; seed Sergeant Vess +10 (`notables.json:15-22`),
  seed Sergeant Brakk +5 (`notables.json:395-402`).
- b) They wrote your name down once, and you made them wrong. → skyrunning +4;
  heat +10; skyrunners +4, watch −4.
- c) They don't, and you have worked to keep it that way. → streetwise +4.
- d) Your father's name is on the Drowned-Name Wall, and Vess remembers him. →
  grit +4; temple +2, watch +2, skyrunners −4; seed Vess +5.

**B5. What has the sea taken from you?**
- a) A father. His hull went out and never berthed again. → grit +6; temple +2,
  merchants −2; seed Grandmother Withy +5 (the Arcade remembers every loss,
  `notables.json:288-295`).
- b) Nothing yet. You are careful with it, and it knows you are. → seacraft +4,
  fishing +2.
- c) A season's wages. The hull went over with you aboard. → seacraft +6,
  grit +2; hpMax −2; coin −5.
- d) Your nerve. You do not go out past the fishbone. → fieldcraft +4, streetwise +2;
  hpMax +2.

**B6. Who owned the roof over your family's bed? (`DOCKS-GAZETTEER.md:221-232`)**
- a) Your family did. You were the house-owner's child, and the lodgers' rent paid for
  your letters. → linkcraft +2; coin +10; merchants +2, dockhands +2, skyrunners −4.
- b) Another family did. You were the lodgers, and the quiet tenant next door taught you
  the crawl-gap. → skyrunning +6; heat +5; skyrunners +4, watch −4; seed Finch +10.
- c) Nobody did. You slept in the Rows, in a hammock rented by the night. → grit +4;
  coin −5; seed Keeper Vetch +10 (he remembers every face, `notables.json:278-285`).

**B7. Which of your debts is still open?**
- a) Fenner holds your paper: fifteen Royals against a start of forty. → coin +15;
  seed Fenner +5. (The repayment seam is a quest hook, not a chargen lever; the only
  mechanical effects are these two.)
- b) You owe Mother Sethra a secret, not coin. → streetwise +2; seed Sethra +10
  (`notables.json:25-32`).
- c) You owe the Mission a winter. They fed you through one, and you never paid it back.
  → temple +4, dockhands −2, merchants −2; seed Onna +10 (`notables.json:45-53`).
- d) Nothing. You pay as you go, and it costs you. → grit +2; coin −5; seed Master
  Gilt +5 (arithmetic respects arithmetic, `notables.json:208-215`).

**B8. The ward already knows one thing about you. What is it?**
(the mandatory-malus device, `ELDER-SCROLLS-REFERENCE.md:311-313`):
- a) A name for trouble. → heat +15.
- b) A soft chest — the wet cough every strand child knows. → hpMax −4.
- c) Empty pockets. → coin −20.
- d) A face the roofs remember badly. → skyrunners −8, dockhands +4, watch +4.

(B8's answers stay noun phrases on purpose. "What is it?" is answered by a noun in
modern English, they were never welded to the old prompt, and a "You have a…"
scaffold cost eleven of the topic grid's eighteen glyphs and collapsed a) and b)
into `1 YOU HAVE A NAME.` / `2 YOU HAVE A SOFT.`. Photographed, then reverted —
`docs/frames/voice-bio/v2-probe-B8.png`.)

**B9. Who fed you the winter the boats stayed in?**
- a) The Mission did, out of its night soup. → temple +4, merchants −2, dockhands −2;
  seed Onna +10, seed Maell +5.
- b) Herdis did, with her goats and her silence. → fieldcraft +4; seed Herdis +15
  (`notables.json:356-363`).
- c) The tide-line did. You worked the strand and nobody chased you off. → fishing +4,
  streetwise +2; seed Tarry Jek +10 (you worked his strand and he let you,
  `notables.json:66-73`).
- d) No one. You went hungry and you remember it. → grit +6; hpMax −2.

**B10. A ship came in dark. You were young. What did you do?**
- a) You told the Watch, and you stood by it. → watch +4, skyrunners −4; seed Brakk +5.
- b) You kept it to yourself. You still know which berth. → streetwise +4; heat +5;
  seed Sethra +5 (she knows you know something).
- c) You sold what you saw at the Wrackhouse. → coin +10; heat +5; skyrunners +4,
  watch −2, temple −2; seed Dagny +10 (`notables.json:76-83`).
- d) You saw nothing. You were asleep. Everyone was asleep. → hpMax +2. (Pure flavor,
  and a lie the biography records.)

**B11. What do you do at the Drowned-Name Wall? (`DOCKS-GAZETTEER.md:355`)**
- a) You tend a name there. → grit +2; seed Withy +5.
- b) You scratch nothing. The sea is not owed your grief. → grit +4.
- c) You read the new names before the bodies turn up, and you think hard about who
  wrote them. → streetwise +4, linkcraft +2.
- d) You leave a candle for a name nobody else tends. → temple +4, dockhands −2,
  merchants −2; seed Maell +5.

**B12. Dawn, and you answer to nobody but yourself. Why today?**
- a) The muster bell rang. Work is work. → coin +5.
- b) The tide is at slack low an hour after dawn, and slack water is a door. →
  fishing +2.
- c) A letter came, and you can read it. → linkcraft +2.
- d) Someone is dead on the strand, and everyone is lying about it. → streetwise +2.
  (The investigation's own hook, planted in the player's past.)

---

## 5. The custom path — advantages, disadvantages, the dagger

The one currency, Daggerfall's own design: a scalar P shown as a dagger on a gauge,
advantages push it up, disadvantages refund, and the net buys your leveling speed
(`ELDER-SCROLLS-REFERENCE.md:290-304`). Restricted to levers that exist; the honest-lever
analysis from task #92's plan governs — no vaporware.

**Proposed arithmetic (draft):** P starts 0, clamp [−8, +12]. XP-rate multiplier
= 1.0 + 0.05 × P → 0.6×..1.6× on `usesForLevel`. Flag, stated not smuggled: wiring any
multiplier into `usesForLevel` is the live-tuning change `chargen.hpp:33-46` deliberately
deferred (it retimes every existing grind and `lockpick.hpp`'s pinned probe count); the
task #92 plan owns that hook. The prices below are the content.

**Caps (draft):** max 3 advantages, max 3 disadvantages (Daggerfall allowed 7 and
shipped exploitable gaps, `ELDER-SCROLLS-REFERENCE.md:297-304`).

### Advantages (cost, +P)

| Advantage | Effect | Cost |
|---|---|---|
| Stout | hpMax +2 (over 24) | +2 |
| Hale | hpMax +4 (excludes Stout) | +4 |
| A Fuller Purse | coin +10 | +1 |
| A Full Purse | coin +25 (excludes Fuller) | +2 |
| A Season's Savings | coin +60 (excludes both above) | +4 |
| A Name (pick one faction) | standing +6 with it | +3 |
| A Friend (pick one: Sethra, Fenner, Withy, Redda, Vess, Mag) | seed that notable +15 | +2 |

### Disadvantages (refund, −P)

| Disadvantage | Effect | Refund |
|---|---|---|
| Unlettered | linkcraft undesignatable, locked at 0 — kills the reading-gated magic track entire (`skills.json:176-183`: "WIT governs because the gate is READING") | −4 |
| Never Armed (per weapon family: sidearms, bladework, lancework, heavy_arms, dire_bows; max two) | that skill undesignatable, locked at 0 | −1 each |
| Landlocked | seacraft AND fishing locked at 0 | −2 |
| Unchurched | channeling locked at 0 | −2 |
| Tender | hpMax −4 | −3 |
| Poor | coin 15 instead of 40 | −2 |
| Destitute | coin 0 (excludes Poor) | −3 |
| Marked | heat +20 at start (a third of the way to the warrant line, `crime.hpp:96`) | −3 |
| Ill-Spoken (pick one faction) | standing −6 with it | −3 |

**Mutual exclusions (the Daggerfall lesson, enforced not hoped):** Unlettered excludes
designating linkcraft; a Never Armed skill cannot be designated; Poor/Destitute exclude
every purse advantage; Tender excludes Stout/Hale; A Name and Ill-Spoken cannot target
the same faction. Pricing is by utility, not lore — locking streetwise or skyrunning is
not offered at ANY refund, exactly as Daggerfall priced Steel −10 against Daedric −2
(`ELDER-SCROLLS-REFERENCE.md:301-302`).

---

## 6. The restructured Origin screen (RULING 2)

The old screen was three doors (DEVIN / GABRI / CUSTOM, `creation.hpp:98-115`). The
Daggerfall flow becomes top-level; Devin and Gabri stay — canon characters, never
removed — as quick-starts. One screen, five rows, two groups, drawn through the same
`DialogueViewState` panel every page already uses (`creation.hpp:55-67`):

```
+----------------------------------------------------------------------+
|  A NAME FOR YOURSELF                                                 |
|  The ward had work for you before you had a name for it.             |
+----------------------------------------------------------------------+
|                                                                      |
|   MAKE YOUR OWN                                                      |
|   > TAKE A CALLING        the ward's nine trades, picked by eye      |
|     ANSWER FOR YOURSELF   ten questions, and be told what you are    |
|     WALK YOUR OWN PATH    every skill, every point, and the dagger   |
|                                                                      |
|   QUICK START                                                        |
|     GABRI                 NO-NONSENSE   Wielder of the Flame         |
|     DEVIN                 SECRETIVE     the Wielder's second         |
|                                                                      |
+----------------------------------------------------------------------+
|  UP/DOWN choose    ENTER take    ESC back                            |
+----------------------------------------------------------------------+
```

The hovered row's own line speaks in the top band, exactly as the three cards' epithets
do today (`creation.hpp:299-303`): a calling's one-liner, the quiz's opening words, the
dagger's warning, or Gabri's/Devin's own `selfIntro` from their raws
(`content/raws/companions/gabri.json:11`, `devin.json:11`).

**Flow after the pick:**

```
TAKE A CALLING      -> roster list (9 cards, sheet preview in top band)
                       -> BIOGRAPHY (12 Qs, or RANDOM)  -> REVIEW
ANSWER FOR YOURSELF -> quiz (10 Qs, meters filling)
                       -> verdict card: ACCEPT calling, or DECLINE to roster list
                       -> BIOGRAPHY (12 Qs, or RANDOM)  -> REVIEW
WALK YOUR OWN PATH  -> the existing custom sheet + the section-5 advantage shop and dagger
                       -> BIOGRAPHY (12 Qs, or RANDOM)  -> REVIEW
GABRI / DEVIN       -> the existing fixed-sheet customize screen, unchanged.
                       No quiz, no biography: their history is the raws' own.
```

REVIEW is the existing customize screen (`creation.hpp:212-232`: NAME, LOOK, skill
rows, attribute rows, BEGIN) with the calling/quiz/custom result pre-designated into
the same `sim::Chargen` — all three doors converge on one screen and one arithmetic,
and `CreationResult` (`creation.hpp:146-153`) needs no new shape. Biography effects
that outrun `Chargen` (coin, heat, standings, seeds, hpMax) ride the confirmed result
to whoever boots the Session — the task #92 plan's wiring, not this doc's.

### Quiz screen mock (Option 1 names shown for illustration)

```
+----------------------------------------------------------------------+
|  THE WARD ASKS -- 4 OF 10                                            |
|                                                                      |
|  Quarter-day, and your family's ground penny is short by three       |
|  Royals. The Den Duke's man is at the gate. You --                   |
|                                                                      |
|  > meet him at the gate and say it plain: the penny is short,        |
|    and your hands will make up the difference.                       |
|    take the roofs to the strand -- the tide is out, and three        |
|    Royals of wrack lies on the mud for whoever beats the gulls.      |
|    fetch the rent-book and walk it to the Mission -- the arrears     |
|    are wrong by the roll's own arithmetic.                           |
|                                                                      |
|  HAND  ###....   TENANT  ##.....   DISCIPLE  #......                 |
+----------------------------------------------------------------------+
```

Answer order is shuffled per question at display time, Daggerfall's own guard against
pattern-marking (`ELDER-SCROLLS-REFERENCE.md:280`).
