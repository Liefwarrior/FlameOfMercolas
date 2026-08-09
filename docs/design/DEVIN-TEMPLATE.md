# DEVIN-TEMPLATE — the secretive origin

**Status:** content-first, pre-loader. Companion piece to
`content/raws/companions/devin.json`, which holds the numbers this document
argues for. Written to resolve two items other specs left open by name:
PROGRESSION-SPEC.md §2's closing line — *"companions (Devin) get their own
aptitude rows when companion progression lands (post-MVP, placeholder)"* —
and COMBAT-SCREEN-SPEC.md §3.3 — *"Devin's stat line: entirely (placeholder /
needs-blessing)."* Both are still placeholder after this pass; what changes
is that a placeholder now exists to bless, where before there was only the
sentence saying one was owed.

**Binding constraints inherited:** DECISIONS.md's canon rule (novel wins,
`LordOfTrojia-MVP` is the read-only reference, `Lore\*.html` non-canon);
ARCHITECTURE.md's integer-only rule (nothing below is a float — skill levels,
attribute points and the bonus pool are all small integers already, inherited
from the engine files this doc targets); the standing quality bar (*"no
shitty english anywhere… stylized isn't the same as bad"*) — see §5 for why
that bar applies to a chargen sheet's flavor text same as any rendered line.

**Task framing (Eli, via DECISIONS.md's companion row and this sprint's
brief):** *"Devin (secretive) and Gabri (no-nonsense)"* as BG3/Divinity
Original Sin 2-style origin templates, alongside a fully custom path. The
ask this document answers: starting attributes/skills that express
*secretive* as **real mechanical choices**, not just a bio paragraph — what
does a secretive investigator actually start good at? — plus background and
flavor text in Devin's own canon voice.

---

## 1. A discovery that reshaped this pass

This document was half-written against the wrong target. The plan going in
was to invent a full 20-skill Favored/Trained/Neglected aptitude table for
Devin, mirroring how PROGRESSION-SPEC.md §2 gives Gabri one row per skill,
because that is the only mechanism the design docs describe.

Partway through, `native/include/granadad/sim/chargen.hpp` turned up in the
working tree — uncommitted, uncompiled, written by a concurrent session
*this same sprint*, and its own header opens with:

> *"It is the arithmetic a BG3/DOS2-style origin-template screen (Devin,
> Gabri, or a fully custom path) will eventually stand on top of, built and
> proved first so that screen has real numbers to show rather than numbers
> invented to match it."*

That is this task, named almost verbatim, from the other side. A real
Chargen calculator already exists: **Primary / Major / Minor** skill slots
(3/3/6, Daggerfall's own shape) over a starting-level ladder
(30/15/5), plus a 24-point attribute bonus pool (cap 15 per attribute) spent
on top of a base of 40, clamped to [10, 100]. It reads skill ids straight out
of `content/raws/skills/skills.json` through the same `SkillTrack` the rest
of the game already uses — "no second skill list anywhere in this file," its
own comment says — and it refuses to let a fresh arrival designate **the
Flame**, because that track's raw `aptitudeTier` is `Flame`, not one a
character sheet hands out.

So §3 below targets that engine, not an invented parallel one. The
Favored/Trained/Neglected table this draft almost shipped is kept in §4 as
design rationale and as Devin's own companion statline for the (also
pre-implementation) combat screen — a different, legitimate use, since
"Devin fights beside you" and "you can choose to start the game *as* Devin"
are two separate features that happen to want the same numbers — but it is
explicitly **not** wired to per-character XP-cost ratios, because
`chargen.hpp`'s own header says that wiring is deliberately left alone this
pass (it would change the grind rate of every already-live skill for every
existing save and test — see §4's closing note for the exact quote).

**Caveat, stated once and meant throughout:** everything in native/ this
document cites (`chargen.hpp`, `chargen.cpp`, `attributes.hpp`,
`attributes.cpp`) was uncommitted work from another session as of this
writing — not in `CMakeLists.txt`, not compiled, no test file. Its constants
(`kPrimarySkillSlots`, `kAttributeBase`, etc.) could move before it lands.
What should survive that is the *shape* of this template — which three
skills are Primary, which is left deliberately at zero — not the specific
numbers, which are quoted from the header as read and should be re-checked
against it rather than trusted blind.

---

## 2. Canon basis

Devin appears in the novel across three scenes (`LordOfTrojia-MVP\Lore\Lord
of Trojia (indexable).txt`, L2340–L2977). What they establish, plainly:

- **Eighteen years old**, dark-skinned, not born on Mercolas — carried
  across the sea as a boy by two monks (L2406). Prejudice followed him into
  the monastery for exactly that reason (L2413).
- **White hair, not yet red.** The Flame's touch turns a Wielder-adjacent
  monk's hair red over years; Devin's hasn't finished the change (L2406).
  He is early in whatever he is becoming, which the character sheet's
  starting numbers should read as low, not zero and not high.
- **Calls Gabri "Reshi"** — a teacher-honorific, used even mid-crisis
  (L2408, L2420, L2717). Never once calls him anything else on the page.
- **Fights at range, backs it with a mace.** Enters the crypt with the
  repeater "nocked and held at eye level" (L2703); "two quick clicks" is the
  weapon's own sound signature (L2713); ends the Bledhreft fight with one
  mace blow to the jaw, described as happening "almost before any of them
  had blinked" (L2976). Gathers his own spent bolts to reuse them (L2715) —
  economical, not wasteful.
- **Afraid, and says so with his body, not his mouth.** Nausea, a stammered
  "M-m-master, what were those things?" (L2716), pissing himself at the
  worst moment (L2708) — and keeps shooting and swinging through all of it.
  Fear is real and it never once stops him from acting; that is the
  character, not a contradiction in it.
- **Notices what nobody else does.** A gleam in a dead girl's eye before it
  turns out she isn't dead (L2708); recognizes Vallech's face from a
  two-years-past royal proclamation the instant it matters (L2724, L2732) —
  a detail nobody, including the reader, was primed to expect him to have
  kept.
- **Dressed plain.** On the one page that puts all three of them
  side by side — "In the lead was Gabri, with his snow white cloak trailing
  behind him and his black leather armor making squeaking sounds… To either
  side was Devin, wearing the gray robes of an apprentice monk, and Vallech
  suited up in all black chainmail that clinked" (L2969) — Devin is the one
  member of the trio wearing no armor at all. Nothing on him clinks or
  squeaks. That is either an apprentice's poverty or a secretive man's
  choice, and this template reads it as the second without denying the
  first.

Nothing in the novel calls Devin *secretive* in so many words — that framing
is Eli's, naming the origin-template archetype for the game, not a
description he found already written. §3–§4 below are this document's
argument for why the canon material above cashes out as *secretive*
specifically, skill by skill, rather than assuming the label and reverse-
engineering support for it.

---

## 3. The chargen preset (the mechanical answer)

Full data in `content/raws/companions/devin.json`'s `chargenPreset` block;
this section is the arithmetic shown, the way PROGRESSION-SPEC.md §9 shows
its own worked examples.

| Tier | Slots | Start level | Devin's picks |
|---|---:|---:|---|
| **Primary** | 3 | 30 | Sidearms, Skyrunning, Cracksmanship |
| **Major** | 3 | 15 | Heavy Arms, Streetwise, Kit-Keeping |
| **Minor** | 6 | 5 | Grit, Mixtures, Linkcraft, Open Hand, Seacraft, Fishing |
| **Untouched** | — | 0 | Bladework, Lancework, Dire Bows, Shieldwall, Harness, Channeling, Fieldcraft |
| *(excluded)* | — | — | The Flame — `Chargen::designate` refuses it outright; not a choice on this sheet for anyone |

That is 3 + 3 + 6 = 12 designated, 7 left at zero, 1 (the Flame) never on
the table — 19 accounted for, the full non-Flame skill list, no gaps and no
duplicates (cross-checked against `content/raws/skills/skills.json`'s own
id list before this landed).

**Why these three are Primary, together, and nothing else is:** a repeater
crossbow that ends a fight before it closes (Sidearms), the sneak/climb/
takedown skill whose own coverage text is a definition of not being seen
(Skyrunning), and getting through what's locked without anyone hearing you
do it (Cracksmanship). Three skills, one throughline: never in the open,
never up close unless the plan already failed. Compare Gabri's own three
non-Flame Favored skills (PROGRESSION-SPEC.md §2: Skyrunning, Channeling,
Streetwise) — they share exactly one entry, Skyrunning, and for opposite
reasons. Gabri's rooftop access is a *privilege* his static, maxed social
power buys him (PROGRESSION-SPEC.md §2 row 10's own note: rooftops are
"socially unseemly for everyone but the presented-Wielder… whose maxed
social immunity is exactly what exempts him"). Devin has no such immunity.
If he's on a roof, he actually has to not be seen up there — the same skill,
earned for the opposite reason, which is the cleanest single proof this
template found that "secretive" and "no-nonsense" are different builds and
not just different paint on the same one.

**Attribute spend:** MGT +0, AGI +12, VIG +4, WIT +8, off a 24-point pool
(cap 15/attribute), applied on top of `kAttributeBase = 40`:

```
MGT = 40 + 0  = 40
AGI = 40 + 12 = 52
VIG = 40 + 4  = 44
WIT = 40 + 8  = 48
```

AGI and WIT ahead of the base line; MGT untouched. The same "built to move
unseen and read a room, not to stand in the open and take a hit" read the
skill picks already make, restated in the pool. `attributes.hpp`'s
`AttributeBlock` carries exactly these four values and nothing else — see
§6 for why that matters more than it looks like it should.

**On the two thinnest picks.** Not every Minor slot has a citation the way
Sidearms or Heavy Arms does, and this document would rather say so than
paper over it. Open Hand has no textual anchor at all — a monastery's
plain-hands discipline is a plausible small competence for someone raised
in one, and nothing more than plausible. Seacraft and Fishing lean on one
fact ("not born on Mercolas, but in the lands across the sea," L2406) and a
few years living in a harbor district since — background, not calling.
Six Minor slots had to be filled for `Chargen::skillsComplete()` to accept
this as a finished sheet, and these three are the most honest way this
document found to fill the last of them. Flagged now so nobody mistakes
"filled" for "equally earned."

---

## 4. The Favored/Trained/Neglected table (design rationale, not a second engine)

The table this section keeps is what a full per-skill aptitude row for
Devin would look like, matching PROGRESSION-SPEC.md §2's own vocabulary and
column shape for Gabri. It is **not** wired to anything — `chargen.hpp`'s
header is explicit about why:

> *"the Java reference engine ties aptitude to a use-XP cost ratio
> (PROGRESSION-SPEC.md section 1); wiring that same ratio into
> `usesForLevel()` would change how fast every ALREADY-LIVE skill grinds…
> for every existing save and every existing test that times a grind
> against it… That is a live-tuning change with its own blast radius and
> its own verification pass, not a chargen mechanic, so it is left alone
> here, stated rather than silently skipped."*

Kept anyway, for two honest reasons: it is the more legible read of
"secretive, mechanically" for a document rather than a save file (Primary/
Major/Minor plus a flat starting number says *what he starts able to do*;
Favored/Trained/Neglected says *how fast he gets better at it*, which is
the other half of "a real mechanical choice"), and it doubles as Devin's own
companion statline once the (also pre-implementation, `SPEC-INDEX.md`'s own
"DO NOT ACT ON THIS YET") first-person combat screen needs one — the same
COMBAT-SCREEN-SPEC.md §3.3 gap this document opened against.

| Skill | Gov | Gabri's aptitude (PROGRESSION §2) | Devin's aptitude | Chargen tier | Why |
|---|---|---|---|---|---|
| Sidearms | AGI | Trained | **Favored** | Primary | signature weapon, ranged (L2703, L2713, L2715) |
| Skyrunning | AGI | Favored | Favored | Primary | same tier, opposite reason (§3) |
| Cracksmanship | WIT | Trained | **Favored** | Primary | covert access; invented, no citation (skills.json: "canon [SILENT]") |
| Heavy Arms | MGT | Neglected | **Trained** | Major | his mace, canon-anchored (L2714, L2976-77) |
| Streetwise | WIT | Favored | Trained | Major | outsider's education, real but not sharpest |
| Kit-Keeping | MGT | Trained | Trained | Major | reuses his own bolts (L2715) |
| Grit | VIG | Trained | Trained | Minor | real fear, pushed through (L2707-16, L2975) |
| Mixtures | WIT | Trained | Trained | Minor | quiet/indirect tool, no direct citation |
| Linkcraft | WIT | Trained | Trained | Minor | studious, reads what's on the shelf (L2408) |
| Open Hand | AGI | Trained | Neglected | Minor | no citation; thinnest pick, named as such |
| Seacraft | AGI | Neglected | Neglected | Minor | crossed the sea as a boy (L2406) |
| Fishing | AGI | Neglected | Neglected | Minor | paired with Seacraft; thinnest of the twelve |
| Harness | VIG | Trained | **Neglected** | untouched (0) | no armor at all, on the page, on purpose (L2969) |
| Channeling | WIT | Favored | **Neglected** | untouched (0) | held below an ordinary disciple's 12 (Onna, notables.json) |
| Bladework | AGI | Neglected | Neglected | untouched (0) | Gabri's public, burning signature weapon; never Devin's |
| Shieldwall | VIG | Neglected | Neglected | untouched (0) | a shield declares you intend to be seen standing and fighting |
| Lancework | MGT | Neglected | Neglected | untouched (0) | formal cavalry weapon, no tie |
| Dire Bows | WIT | Neglected | Neglected | untouched (0) | different weapon family from his own repeater |
| Fieldcraft | MGT | Neglected | Neglected | untouched (0) | no tie, urban companion |
| The Flame | — | ×4 (Flame) | **not applicable** | excluded | not a Wielder candidate; the title is Gabri's alone |

Two rows are worth reading twice because they are downgrades from Gabri's
own row, not just differences: **Harness** and **Channeling**. Both are
deliberate statements, not gaps. Harness because the novel puts him in
robes next to two armored men, on the page, without comment — the comment
is this document's, not the book's, and it reads that silence as the plain
practical fact that armor makes noise and Devin is trying not to. Channeling
because it is the one register this template refuses to touch at all: the
Flame's visible, loud, unmistakably-Gabri's kind of power stays entirely on
Gabri's side of the ledger. A secretive companion whose magic (what little
he starts with, via Linkcraft) is the bookish, public-shelf, nobody-notices
kind is a sharper contrast than one who is merely "a weaker version of the
Wielder."

---

## 5. Voice — background and self-introduction

The environment brief for this sprint names concrete Daggerfall UI
vocabulary worth building the *systems* behind (never the specific text): a
"Tell me about" topic tree, and a tone selector — POLITE / NORMAL / BLUNT —
that visibly changes what an NPC says back. Devin is a useful test case for
that shape, because his canon voice already has range: formal and devout
under an honorific ("Reshi"), stammering under acute fear, and dryly
understated the moment danger passes ("That wasn't difficult at all,"
grinning, L2977). None of the copy below is implemented dialogue — no
tone-selector or topic-tree system exists in native/ yet — it is written to
be ready the day one does, and to prove the voice holds up in more than one
register before anything is built on top of it.

**Bio** (matches `content/raws/companions/devin.json`'s `bio` field, and the
prose register of `content/raws/names/notables.json`'s existing bios —
concrete nouns, no melodrama):

> Eighteen, apprentice of the Divine Light, carried across the sea as a boy
> by two monks who found more use for him than his own shore did. His hair
> has not caught the Flame's copper yet — that comes later, same as
> everything else about him that isn't finished. He walks a half-step back
> and to the flank with a repeater crossbow nocked and a mace at his hip for
> when the range closes, and he has learned to read a room's exits before
> its exits matter to anyone else in it. What Reshi says, he does. What he
> notices, he mostly keeps.

**Self-introduction** (a first-person line, the register a chargen screen's
"who is this" panel would want):

> Devin. Reshi's second — Gabri's, I mean. I watch what he doesn't have
> hands enough to watch, and I'd rather you didn't see me doing it. Ask me
> what I saw before you ask me what I think of it. I'll answer the first one
> straight.

**Three tone-selector sample lines**, all answering the same imagined
question — *"Tell me about yourself"* — to show the register holds without
becoming a different character:

| Tone | Line |
|---|---|
| POLITE | "Devin, if it please you — I keep Reshi's flank and his ledger of what he'd rather not carry himself. I'm better used watching a door than standing in one." |
| NORMAL | "Devin. I watch what Gabri can't watch himself, mostly from a few paces back. That's the whole of it." |
| BLUNT | "Devin. I'm the one behind him with the crossbow. Ask what you need and let me get back to the door." |

The stammer (L2716) and the dry post-fight line (L2977) are both **state-
gated**, not tone-gated — they belong to a fear/relief system, not a
politeness dial, and are named here only so a future dialogue pass does not
try to fold them into the tone selector by mistake.

---

## 6. Open items

1. **PRESENCE has no home for a non-Wielder.** PROGRESSION-SPEC.md §5 sets
   Presence STATIC 100 as a fact about *holding the title*, and
   `attributes.hpp`'s real `AttributeBlock` — the chargen engine's actual
   attribute container — confirms that reading structurally: it holds
   exactly MGT/AGI/VIG/WIT, no fifth slot at all. This template does not
   invent a Presence value for Devin, on the reasoning that a made-up number
   would either quietly imply parity with the Wielder's static 100 (wrong —
   his canon reception is prejudiced, not deferred-to, L2413) or invite a
   guess at a system nobody has designed yet. Left open on purpose.
2. **This targets uncommitted engine code.** See §1's caveat. If
   `chargen.hpp`'s slot counts or per-tier levels change before it lands,
   re-derive the numbers in `content/raws/companions/devin.json`'s
   `chargenPreset` from the new constants; the tier *assignments* (which
   three skills are Primary, etc.) are this document's actual argument and
   should survive a constant change untouched.
3. **No preset-loading system exists.** `Chargen` takes individual
   `designate()`/`spendAttributePoints()` calls; nothing reads a file like
   `devin.json` yet. Whoever builds an origin-template screen needs a loader
   that walks `chargenPreset.primary/major/minor` into `designate()` calls
   in that order (each tier's array length matches its slot cap exactly, so
   nothing overflows) and `attributeBonusSpend` into `spendAttributePoints()`
   calls.
4. **The Favored/Trained/Neglected table (§4) is unwired by design**, per
   `chargen.hpp`'s own stated reason (its blast radius touches every
   already-live skill's grind rate). If and when per-companion aptitude
   ratios do land, §4's table is the intended input.
5. **Gabri's own origin-template preset is out of scope here** — this
   document only speaks for Devin. A sibling pass presumably owns Gabri's
   own Primary/Major/Minor picks and attribute spend under the same
   `chargenPreset` schema; nothing in this file assumes what those are.
6. **The dialogue-voice samples in §5 are unimplemented.** No tone selector,
   no topic tree exists in native/ for any NPC yet. They are written to be
   ready, not to claim readiness.
