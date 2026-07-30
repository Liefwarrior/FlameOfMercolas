# MAGIC-CANON — provenance backbone for Simple Magic (public-shelf craftings)

**Status:** reference dossier for review, in the shape `MATERIALS-CANON.md` §1.1 established:
canon facts with line cites first, then the sim derivation, then the interpretive steps flagged
for Eli. Every number in `content/raws/spells/spells.json` and every constant in
`com.trojia.sim.actor.spell` must trace back to a row here as *canon-derived* or
*invented-needs-blessing*.

**Sources & precedence** (repo canon rule, unchanged):
1. **Novel** — `C:\repositories\LordOfTrojia-MVP\Lore\Lord of Trojia (indexable).txt`, cited
   `(L<line>)`. The novel wins all conflicts.
2. **WorldBible** — `C:\repositories\LordOfTrojia-MVP\Documentation\WorldBible.md`, cited
   `(WB §<n>)`. Reliable summary; the novel still wins.
3. The `Lore\*.html` files are **NON-CANON** (AI fan summaries). One of them is cited exactly
   once below, and only to say what it is and that it was discarded — see §5.

**Two rulings landed on 2026-07-30 and are applied throughout this file.** Both replaced an
invention with something canon already supplies:

- **The seven is six Steward systems + the Flame of Mercolas — and the seventh is a SECRET**
  (§5). The Flame sits at the TOP of a hierarchy, has never died, and lends itself to Wielders
  rather than passing by descent; in-world the answer is **six** and nobody knows better. The
  HTML's "Seven Powers" table is discarded. **§5 is out-of-world knowledge — read §5.4 before
  writing any in-world string, and §5.5 before putting anything on the spell bar.**
- **There is no imperial library edict** (§0). Canon's public-issue vs restricted editions
  (L2472) carry the premise instead, and carry it better.

---

## 0. Why common magic is weak — the premise, in canon's own words

> "This particular gathering of books was issued to the public because it was overly general and
> skimmed over most of the battles and details" (L2472)

Gerik says that, and then climbs a chair to reach the real edition off the **top shelf**. That
scene is the whole premise of Simple Magic and nothing else is needed for it. Books come in two
grades. The public-issue grade is genuinely available and genuinely shallow. A docker who has
read it can open a link and move a little heat, and cannot do more, because *the edition they
learned from skimmed the details*.

**What was removed to get here.** An earlier draft of this pass invented an imperial edict
opening "the Libraries of Granadad" to any who can read. That edict appears nowhere in the
novel, and neither does a public library in Granadad. It is gone from the code, the raws, the
skill provenance and this dossier — not blessed as an addition, **withdrawn**.

**What canon's libraries actually are, left where canon put them:**

| Library | Where | Whose | Cite |
|---|---|---|---|
| **Library of the Runemasters** — "the most complete collection of knowledge anywhere on the earth" | *Mercia*, inside the Du Vron Dezdant rebel fortress | the Runemasters | L2478 |
| The monastery library | Granadad, inside the **Divine Light Cathedral** | Gabri's own order | L2412 |

Neither is relocated, renamed, opened to the public, or used as the source of the ward's
craftings. The shelf a docker learns from is the *grade of book*, not a building.

**What it buys mechanically.** `SpellDefinition.minLevel` is the literacy tier: the shallow rows
are readable by anyone, the deeper rows sit on the top shelf and show greyed on the bar with
their gate on them. And the balance argument becomes a lore argument — nothing off the public
shelf should be strong — which is why the two hard bounds in §2 are structural rather than
tuned.

---

## 1. What canon actually says about moving energy

Gerik's lecture to Eric is, almost line for line, a spell-cost model written as prose. It is the
single densest passage in the novel about how magic *works*, and everything in this pass is
built on it.

- **It is not fire-throwing, it is energy moving.** "I mean CRAFTING... it was changing nature
  not apart from it. Where do you think all that energy came from" (L443).
- **Heat is the working currency.** Gerik heals his own arm by "knitting the skin back together
  with heat energy from the enemies" (L445). Eric links into a campfire to refill himself
  (L505).
- **Harm is the same operation, applied badly.** "too much energy hurts... you're applying it to
  a small part of his body" (L448). Force is heat's sibling: energy into a body makes "a small
  part of his arm, or head move down" (L449); Eric shatters a log "by putting pressure on
  various parts of it" (L530).
- **Distance and magnitude both bleed.** "the more you transfer the more is lost to nature, also
  the further it travels the more is lost" (L452) — energy "always wants to go the easiest
  route, like a lazy recruit, always downhill."
- **Many small beats one big.** "it would be less taxing to do lots of tiny transfers than one
  big one" (L457).
- **A link needs a bridge.** "A crafting requires a link between the two entities, normally this
  would be an arm or a blade" (L459). Doing it without one takes "the gift," and "almost nobody
  can do this."
- **Everyone has some, and trained it is still small.** The royal mage at Eric's birth: "Everyone
  has 'magic' though without training it will stay passive, but even with training it is
  limited. 'Magic' can be expressed as unnatural intelligence, or in strength, or reflexes"
  (L98).
- **The ceiling for organised common magic.** At the Tora tower "the energy of twenty full
  crafters was linked to a torch that one of the guards held in his hand. The fire surged and
  caught the guard on fire" (L567). Twenty *trained military crafters*, pooled, make one
  already-lit torch flare. And a common crafter runs at roughly a thousandth of a Luxerne's
  throughput: "any other crafter would have to open a thousand links and kill fifty men to get
  as much energy as you or I have inside us" (L516).
- **Where the Luxerne tier starts, so we know what to stay under.** Heart-links stop hearts
  instantly (L560, L569); a head-link bursts vessels (L561); a pocket-to-torch link levels a
  stairwell (L567). None of that belongs to a docker.
- **Books are issued in two grades.** "This particular gathering of books was issued to the
  public because it was overly general and skimmed over most of the battles and details"
  (L2472) — Gerik then climbs a chair for the real edition off the top shelf. A commoner reading
  the public shelf and getting a real but shallow grasp is not an invention; it is the scene.

**LoT2 (tentative canon, WB §3.1)** renames the Source *Thectrochanics* and states the
underlying law: heat, light and force are interchangeable forms of energy the Trojjan can
translate between. That is the effect-axis table, author-supplied.

---

## 2. Sim derivation — what each canon line became

| Canon | Sim |
|---|---|
| L445 heat is the currency; L448 too much energy hurts | `EffectKind.TEMPERATURE` and `EffectKind.VITALITY` are the SAME operation with the endpoint changed, which is why they are separate axes and not separate spells |
| L98 "unnatural intelligence, or in strength, or reflexes" | `EffectKind.ATTRIBUTE` with a `param` of WIT / MGT / AGI — the three named expressions ARE the attribute roster, quoted |
| L445 mend vs L448 harm | one signed magnitude per component; the minus sign is the whole of opposition. `knit_the_skin` is `scald` with a plus |
| L452 distance bleeds | `SpellCost.RESIST_PER_TILE = 4` |
| L452 magnitude bleeds | `SpellCost.RESIST_PER_TRANSFER_POINT = 1`, over `EffectComponent.transferPoints()` |
| L457 many small beats one big | `EffectMode.OVER_TIME`, dosed every `ActiveEffects.OVER_TIME_PERIOD_TICKS`; a trickle pays for every dose it will deliver, so a long effect is honestly dearer |
| L459 a link needs a bridge; the gift is rare | `TargetShape.TOUCH` is the ordinary case; `TargetShape.RANGED` exists in the vocabulary, costs `SpellCost.RESIST_UNBRIDGED = 20` on top of distance, and **is authored on no spell in the shipped list** |
| L567 twenty crafters make one torch flare; L516 ~1/1000 of a Luxerne | `ActiveEffects.VITALITY_FLOOR = 1` — no crafting can put a body on the ground. Structural, not a tuning value: the only code that writes a hit point enforces it |
| L2472 public-issue vs restricted editions | `SpellDefinition.minLevel` — the literacy tier as a per-spell gate, with the deep rows visible but greyed on the bar. The skill the gate is measured in is `SpellDefinition.skillKey`, a raws field, not a hardcoded one: a crafting from another tradition would be gated, checked and grown by that tradition's own skill with no code change |
| L98 "even with training it is limited" | `SkillChecks.LINKCRAFT_CEIL_PERMILLE = 900`; mastery never buys certainty, the same contract every other check family in the file carries |
| L443 crafting is changing nature, not standing apart from it | the effects land on ordinary sim state — hit points, needs, the one attribute function — never on a parallel "magic" pool |
| L2472 the public edition is the shallow one | `ActiveEffects.ATTRIBUTE_MODIFIER_LIMIT = 2` — the second structural bound. A live attribute row is folded into `SkillTrackRegistry.attribute()`, the ONE function every check in the game reads, so an unbounded stack of nudges would be an unbounded multiplier on the entire skill system. Nothing off the shallow shelf may be strong, and that is enforced twice: the loader refuses an authored magnitude past the limit, and the live SUM is clamped to it, so stacking cannot walk past it either |
| L452 "the more you transfer the more is lost" — including over time | duration is priced on BOTH lingering shapes. A trickle pays per dose (`ActiveEffects.OVER_TIME_PERIOD_TICKS`); a hold pays per period it keeps the link open (`SpellCost.HELD_PERIOD_TICKS = 300`, deliberately coarser, because holding a link is not the same work as pushing fresh transfers through it). Before this, a held effect cost the same at one tick as at ten thousand |
| one operation, three endpoints (L445 heat / L448 harm / L98 the three expressions) | `EffectPairing` — the (axis × time-shape) table. Heat and tuning are **held** (`WHILE_ACTIVE` only: a body carries no thermal store and an attribute is recomputed from skills on every read, so there is nothing for a one-off to write). A wound is **delivered** (`INSTANT` or `OVER_TIME`: hit points are a written number and nothing reads a held vitality row). The other five pairings are refused at load, by name |

**Not derived from canon, stated plainly:** the specific integers (15 deci-Kelvin of warmth, 600
ticks, base resists 0–14, cooldowns 200–700, the 96-row effect table, the 50-tick warmth→REST
cadence and its 5-deci-Kelvin-per-point exchange rate, the 300-tick held pricing period, the
±2 attribute limit). These are v0 balance numbers chosen so a laborer's crafting is a comfort
and never a weapon. The *ratios* are canon-shaped; the absolutes are invented.

**What the loader now refuses, and why it is a canon argument rather than a validation nicety.**
Nine (axis × mode) pairings existed; five of them loaded, resolved, charged their full computed
difficulty, toasted "the link opens; it takes" and changed nothing at all. Two more families did
the same without being illegal: a trickle authored under one dose period delivered zero doses,
and a magnitude of 0 consumed a persisted slot. All of them are now refused at construction with
a named error naming the exact `components[i]`. The rule the system can now state honestly is:
**a crafting that gets past the loader does something.** In a vocabulary built to be composed
from, that is the property everything else depends on.

---

## 3. Interpretive steps flagged for Eli (needs-blessing)

These are the load-bearing readings. Each is defensible; none is stated outright in the novel.

1. **That a commoner can open a bridged link at all.** L459 says making a link takes "the gift"
   and "almost nobody can do this." The reading that rescues the premise is that the gift is
   needed for an **unbridged** link, while an arm or a blade is the ordinary case — which is
   exactly what L459's own "normally this would be an arm or a blade" implies, but it IS a
   reading. Everything else here rests on it. **This one matters most.**
2. ~~**The imperial edict opening the Granadad libraries.**~~ **WITHDRAWN 2026-07-30, not
   blessed.** Eli ruled that canon's own mechanism carries the premise better than the invention
   did, so the edict is gone from the code, the raws and this dossier rather than flagged. See
   §0: the public-issue edition (L2472) is why common magic is shallow, and canon's two real
   libraries stay exactly where canon put them. Nothing is left to bless here.
3. **"Linkcraft" as a skill name.** Canon's word for the practice is *crafting*, which would
   collide with smithing in a skill list (and canon itself jokes about that collision at L442).
   LoT2's academic name for the field is *Thectrochanics*. "Linkcraft" is invented, anchored on
   L459's "link". Rename freely.
4. **"Kinematics and thermology" as in-world vocabulary.** The physics is canon (L448–457); the
   two words are not. The code and the raws use canon's own words instead — *crafting*, *link*,
   *transfer* — and leave Thectrochanics to the Runemasters.
5. **The warmth→REST coupling.** Being warm is restful and being cold is not; canon says nothing
   about it. **Resized 2026-07-30**: it used to pay one REST point per deci-Kelvin per 50 ticks,
   which made the shipped `warm_the_hands` worth 15 points per cadence against a serf's natural
   decay of 30 — half a body's whole REST drain, from a crafting documented as "a dry coat on a
   wet quay, nothing more". The exchange rate is now 5 deci-Kelvin per point, so the same warmth
   pays 3, about a tenth of the decay: felt over an afternoon, never a substitute for a bed. Any
   non-zero warmth still pays at least one point in its own direction, because a rounded-to-zero
   payment would be another silent no-op. Today it only ever touches the played actor, since no
   AI works craftings. Cut it freely if it is not wanted.

---

## 4. What this pass deliberately did NOT do

- **No AI casting.** Only the played soul works craftings, so the ward's routes, the predators'
  supply and the justice pipeline are untouched by this pass. The verb is written as a shared
  resolution path (`SpellVerb.resolveCast`) precisely so an AI policy can be added later without
  a rewrite.
- **No deaths.** The vitality floor is structural. Magic cannot feed the death log.
- **No ranged craftings on the shelf.** The vocabulary supports them; canon gates them behind
  the gift; the content therefore withholds them. That gap is the system working as designed —
  a tier the engine can already express and the fiction has not yet granted.
- **No thermal system.** ARCHITECTURE §3 specifies `com.trojia.sim.thermal` and it is still
  unbuilt. Body heat here is an offset carried on the actor's own effect rows, not a tile-lane
  temperature, so nothing in this pass pre-empts that design or writes a world lane (actors
  never write lanes, ACTORS-SPEC §2.3).

- **No hit-point regeneration, and that is a disclosure rather than an oversight.** Nothing
  anywhere in this sim heals a body over time. Vitality damage is therefore permanent until a
  crafting mends it, which is why round 2 adds `close_the_cut` — `knit_the_skin` turned outward
  across a bridged link, so a body harmed by somebody else can be mended by somebody else.
  Without it the only mending row on the shelf was SELF-target and harm accumulated forever with
  no route back. What that costs today is very little: nothing outside this package reads hit
  points yet, so an unmended body is a number on the character sheet rather than a bleed with
  consequences — and `VITALITY_FLOOR` guarantees it can never become one. When something does
  start reading hp, natural regeneration is its problem to design, not magic's to fake.

- **No Flame of Mercolas — anywhere.** No spell row, no shelf entry, no button on the bar. The
  absence is the design and it is the most deliberate thing in this pass; the reasoning is §5.5,
  and it is out-of-world knowledge that does not appear in any in-world string.

- **No AI hits the room guard.** The effect table holds 96 rows and only the played actor casts,
  with cooldowns of 200–700 ticks and a longest hold of 900, so the ward cannot realistically
  fill it. The guard exists anyway because the alternative it replaced — silently evicting the
  soonest-expiring row, possibly another actor's live warmth — is a lie the moment it fires once.

---

## 5. The seven systems — RULED (Eli, 2026-07-30)

> ### ⚠ OUT-OF-WORLD KNOWLEDGE. THE WORLD DOES NOT HAVE IT.
>
> This whole section is **developer-facing**. Nothing in it may be said by an NPC, printed in a
> book or on a shelf, written on the masters board, put in a bark, a talk topic, an item
> description, a tooltip or the journal. **Every in-world surface says SIX.** A scholar in this
> ward would say six and be wrong, and that is the design. See §5.4 for the rule and the check.

### 5.1 The count is six in the world and seven in fact — and that is not a contradiction

Eli's ruling, verbatim: *"The public think it's six, even experts in Mercolas think it's six, but
no one knows that the Flame is the 7th and has never died, it shares its power with wielders."*

This **resolves** the six-versus-seven problem rather than overruling it. The novel says six and
the WorldBible says six *because the world believes six*. The count is not an error in canon; it
is canon reported from inside a world that does not know. Nobody knows — not the public, not
scholars, not Mercolan experts, not the Runemasters who grant degrees in the subject.

The project already models exactly this distinction — presented identity versus true identity,
and perception-gated readouts that show a played actor only what its own senses support. The
six/seven split is that same idea at world scale.

### 5.2 The seven are a HIERARCHY, not a set of peers

The Flame of Mercolas is **the strongest of the seven** and sits at the top of the list. The six
Steward systems below it are the peers; the Flame is not one of them.

| # | System | Steward plane | Bearer, in canon's own word | Cite |
|---|---|---|---|---|
| **—** | **the Flame of Mercolas** | *none — this is the point* | a **Wielder** (canon: "Gabri **the Wielder of the Flame**") | WB L273, L407, L494 |
| 1 | the Source | Trojja | Luxerne / recognised descendant (Eric) | L442–459; L505, L516, L560–569; WB §3.1, L70 |
| 2 | flesh | Mercan | Luxerne (Ignis) | WB §3.2, L71 |
| 3 | lightstuff / aether | Vervan | Luxerne (Lief) | WB L72 |
| 4 | wards | Fran | Luxerne (Adelia) | WB L73 |
| 5 | will-shaping | Firra | Luxerne (Sarai) | WB L74 |
| 6 | creation | Rema | Luxerne (Revlin) | WB L75 |

Every one of the seven is named in canon and nothing here is invented. The six Steward planes are
the novel's own: six children of Mercola (L726, L2468), six in the room at L1479 ("a Trojian, a
Frandian, a Vervadian, a Mercian, a Fiorran, and a Reman"), and WB L742 records the audit
resolution "Six vs seven Luxerne → six (novel)". The seventh is canon's own seventh thing: WB
L273 names the Flame explicitly as "a religious office, **NOT** a Luxerne power. **Distinct
system**."

**The ranking itself is AUTHOR-SUPPLIED CANON** (Eli, 2026-07-30) rather than a line in the
novel — he is the author, so his word is canon, but the dossier should not pretend it is a quote.
The existing material does corroborate it, and specifically: the Wielder of the Flame "can purge
the Beast's blood-pact from a Y'marr with the Flame" (WB L407), and Ch. 33 is that happening —
Gabri burns the Beast's mark off Vallech's arm with the Flame of Mercolas, severing the pact, and
Vallech is free (WB L494). **A power that unmakes the Beast's own binding on one of the Beast's
own hands is not sitting mid-table.** Nothing else in the book undoes one.

### 5.3 The Flame has never died — so it has no Y'marr, and CANON MEANS SOMETHING ELSE BY THAT WORD

The Steward planes pass **by descent**: one bearer per plane, and when Trojia assassinated the
royal houses "the magic that is tied to the descendants went to the next person with the closest
tie to their land's steward" (L1481). The Flame does not work that way. It is **continuous** — it
has never died — and it **shares its power with Wielders**. That is why canon's word for Gabri is
*Wielder* and not *descendant*, *heir* or *Luxerne* (WB L273). **Frame the Flame as a persisting
power that lends itself, never as a seventh bloodline.**

**A correction, made deliberately and on the record.** The brief that commissioned this section
described Y'marr as "the bearer/descendant of a power," citing L1482. Canon says the opposite,
and the instruction was to follow canon and say so, so:

- **A Y'marr is the Beast's side, not Mercolas'.** "Just as the descendants are Mercolas' hands
  in the world, the beast has hands as well. **The Y'marr are the opposites to the descendants**,
  they sleep until a descendant enters their land and then awaken. They hunt down the descendant
  and kill them… One Y'marr, one descendant from each country." (**L1487**, not L1482 — L1482 is
  Revlin asking how anyone can tell he is a descendant.) The quoted sentence *distinguishes* the
  two roles; it does not equate them.
- **They are the corruption mirror of the bearers.** The Beast's "shattered heart broke into six
  fragments — one per magical plane. These are the Y'marr" (WB L120); the Luxerne emerged as the
  dimensional counter, "six mortals, one per plane… oriented toward creation rather than
  corruption" (WB L123). Pairing is **by dimension, not by territory** (WB L79).
- **There are six of them, and a Y'marr says so.** "I am U'mar, Y'marr of the bloodbinding coven,
  **I am one of six**, eternal enemy of the Vervadian descendant of Mercola" (L1170). U'mar is
  also styled "STRONGEST OF THE Y'MARR" (L1151), so canon already ranks that side of the ledger.

**Read through canon, Eli's second fact lands cleanly and gets stronger, not weaker.** "Its Y'marr
has never been seen before" means: *the Flame has no corruption-mirror on record.* The Beast's
heart broke into six fragments, one per magical **plane**, and the Flame is explicitly not one of
the planes (WB L273) — so there is no seventh heart fragment anybody has met, and U'mar's "one of
six" is a true statement made by someone who also does not know about the seventh. The count on
the Beast's side matches the count on Mercolas' side matches the count the world believes, all
for the same reason. **This is the single most load-bearing corroboration of the secret in the
whole dossier, and it falls out of canon rather than being asserted over it.**

*Author-supplied, flagged: whether the Flame's absent Y'marr is "not yet woken", "does not exist",
or "the Beast never got a fragment of it" is not decided anywhere. Left open on purpose.*

### 5.4 THE HARD CONTENT RULE — every in-world surface says six

| Surface | What it says |
|---|---|
| This dossier, `BLESSING-QUEUE.md`, code comments, `provenance` fields | the truth: seven, the Flame at the top, never died, shares with Wielders |
| NPC dialogue, barks, talk topics, book and shelf text, item descriptions, the masters board, journal prose, tooltips, sign text — **anything a citizen could utter or a player could read as in-world text** | **six** |

No NPC hints at it. No bark carries a knowing wink. The secret's value is that it is total, so a
scholar in this ward is confidently, articulately wrong.

**Checked, not claimed.** `content/` and every `*.java` string were grepped for "seven" and
"7 powers" on 2026-07-30. There is no in-world text about the count of magic systems anywhere in
this build — the only hits are developer comments about unrelated sevens (seven world lanes,
seven shop guard posts, seven S8 trade goods). Nothing had to be removed; the rule is recorded
here so the next content pass does not walk into it.

### 5.5 THE PROHIBITION — the Flame ships nothing, and the absence IS the design

**The Flame of Mercolas does not go on the public shelf, does not get a spell row, and does not
appear on the spell bar.** Not in this sprint and not by accident in a later one.

Everything shipped here is the **Source**: the weak, learnable, public-issue-edition magic a
literate docks laborer can pick up off a shallow book (§0), which is exactly *why* it is weak.
The Flame is the opposite of that on every axis — strongest of the seven, unprecedented, not
learnable from a watered-down edition, and the player's own religious office rather than a
Steward plane. A Flame effect sitting next to `warm_the_hands` in a button column would flatten
the single most important asymmetry in the setting.

It is also the project's north star stated in canon's own terms: **social power maxed from the
start, physical power grown through exploitable systems.** Gabri already holds the office and
"full lawful immunity except killing the emperor" (WB L407) on day one — that is the maxed social
power, and it is canon's, not ours. What he does not have is a spell for it, and he should not.

Leave the seam open. Do not fill it. In-world there is nothing to leave out at all — the ward has
never heard of a seventh.

### 5.6 The HTML seven is DISCARDED, and here is exactly why

The only document on disk listing seven *magic systems* is
`Lore\SECRET-MATERIALS-PROPER-NOUNS.html` §7 "The Seven Powers" (Energy Weave, Flame Dominion,
Air Fury, Earth Resilience, Mind Veil, Reflex Storm, Shadow Corruption), each paired to a named
natural law. The pairing is a genuinely good idea and it is the reason Eli's original brief reads
"kinematics and thermology". It is also non-canon on two independent grounds this project had
already ruled on:

1. This repo's canon rule classes every `Lore\*.html` as an AI fan summary; the WorldBible header
   says they are "preserved only as a source of catchy ability-name suggestions… they do NOT
   inform plot, character, or magic-system facts."
2. WB L188 discards the seven-school framing by name: it "is non-canon and has been discarded."

Note that its seven is not this seven either — it lists seven *peer schools* with the Flame
demoted to one of them ("Flame Dominion"), which is precisely backwards from §5.2. So the *names*
on that table may still be mined for flavour; the *framing* is not canon and is not used. Nothing
in this build refers to the Seven Powers.

### 5.7 What the shipped craftings sit on

The **Source** — Trojja's, i.e. the player's own land, and the only one of the seven canon
specifies in enough detail to build from (L442–459 is a cost model written as prose). The effect
vocabulary is deliberately plane-agnostic, so a ward, a lightstuff jump or a will-shaping would
each arrive as new rows and at most a new axis, never a new engine. Adding a system does not mean
adding a system.

### 5.8 BOOK 2 — UNPUBLISHED AUTHOR INTENT, NOT YET CANON

Eli's plan for book 2 is that **the Flame is absorbed entirely by Gabri or Devon**. Recorded here
so a future sprint does not design something that contradicts it. (*Spelling note, no more than
that:* the WorldBible spells the apprentice **Devin** — "Gabri's apprentice… white hair, a Flame
side-effect", WB L408. Eli wrote "Devon". Same person; canon's spelling is used everywhere else
in this repo.)

**Build nothing toward it. Leak nothing of it into content. It constrains no sprint, including
this one.** It is not canon, it is not published, and it must not appear in any in-world surface
or in any design that ships before the book does.
