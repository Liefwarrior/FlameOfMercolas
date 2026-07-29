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
   once below, and only to say what it is — see §5.

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
| L2472 public-issue vs restricted editions | `SpellDefinition.minLevel` — the literacy tier as a per-spell gate, with the deep rows visible but greyed on the bar |
| L98 "even with training it is limited" | `SkillChecks.LINKCRAFT_CEIL_PERMILLE = 900`; mastery never buys certainty, the same contract every other check family in the file carries |
| L443 crafting is changing nature, not standing apart from it | the effects land on ordinary sim state — hit points, needs, the one attribute function — never on a parallel "magic" pool |

**Not derived from canon, stated plainly:** the specific integers (15 deci-Kelvin of warmth, 600
ticks, base resists 0–14, cooldowns 200–700, the 96-row effect table, the 50-tick warmth→REST
cadence). These are v0 balance numbers chosen so a laborer's crafting is a comfort and never a
weapon. The *ratios* are canon-shaped; the absolutes are invented.

---

## 3. Interpretive steps flagged for Eli (needs-blessing)

These are the load-bearing readings. Each is defensible; none is stated outright in the novel.

1. **That a commoner can open a bridged link at all.** L459 says making a link takes "the gift"
   and "almost nobody can do this." The reading that rescues the premise is that the gift is
   needed for an **unbridged** link, while an arm or a blade is the ordinary case — which is
   exactly what L459's own "normally this would be an arm or a blade" implies, but it IS a
   reading. Everything else here rests on it. **This one matters most.**
2. **The imperial edict opening the Granadad libraries "to any who can read."** No such edict
   exists in the novel, and neither does a public library in Granadad. Canon has the **Library
   of the Runemasters** in *Mercia*, inside the Du Vron Dezdant — "the most complete collection
   of knowledge anywhere on the earth" (L2478) — and a monastery library inside the Divine Light
   Cathedral in Granadad (L2412), which belongs to the player's own order. The *public-issue vs
   restricted edition* mechanic IS canon (L2472). Recommend treating the edict as a
   **GAME-CANON-ADDITION** in the chromatis mould, with a blessing line.
3. **"Linkcraft" as a skill name.** Canon's word for the practice is *crafting*, which would
   collide with smithing in a skill list (and canon itself jokes about that collision at L442).
   LoT2's academic name for the field is *Thectrochanics*. "Linkcraft" is invented, anchored on
   L459's "link". Rename freely.
4. **"Kinematics and thermology" as in-world vocabulary.** The physics is canon (L448–457); the
   two words are not. The code and the raws use canon's own words instead — *crafting*, *link*,
   *transfer* — and leave Thectrochanics to the Runemasters.
5. **The warmth→REST coupling.** Being warm is restful and being cold is not; canon says nothing
   about it. It is small (one REST point per deci-Kelvin per 50 ticks) and today it only ever
   touches the played soul, since no AI works craftings. Cut it freely if it is not wanted.

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

---

## 5. The "seven systems" question

Eli's brief said "the 7 systems all have great ideas." The only document on disk listing seven
magic systems is `Lore\SECRET-MATERIALS-PROPER-NOUNS.html` §7 "The Seven Powers" (Energy Weave,
Flame Dominion, Air Fury, Earth Resilience, Mind Veil, Reflex Storm, Shadow Corruption), each
paired to a named natural law. That pairing *is* a good idea — and it is the reason the brief
reads "kinematics and thermology".

It is also non-canon, and this project already ruled on it. The WorldBible header classes the
`Lore\*.html` files as AI-generated and "preserved only as a source of catchy ability-name
suggestions... they do NOT inform plot, character, or magic-system facts"; WB L188 says the
seven-school framing "is non-canon and has been discarded"; WB L742 records the audit
resolution "Six vs seven Luxerne → six (novel)". The novel names six children of Mercola (L726,
L2468) and puts six in the room at L1479–1482.

**If a seven is wanted, canon supplies one for free:** the six Steward planes plus the **Flame of
Mercolas**, Gabri's religious office, which WB L273/L407 call explicitly "NOT a Luxerne power...
Distinct system." That framing invents nothing.

**None of this pass depends on the answer.** Simple Magic is built on the Source alone — the one
system canon specifies well enough to build from — and the effect vocabulary is deliberately
plane-agnostic: a ward, a lightstuff jump or a will-shaping would each be new rows and new
axes, not a new engine.
