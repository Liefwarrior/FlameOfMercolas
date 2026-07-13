# Locked Design Decisions — Granadad: The Darkstreets

Locked by Eli 2026-07-12 ("DECISIONS LOCKED — proceed to plan + build. Quality over speed, no shortcuts.")
Engine/sim decisions live in ARCHITECTURE.md; this file records the GAMEPLAY layer.

| # | Area | Decision |
|---|---|---|
| — | Title | **Granadad: The Darkstreets** — playing Gabri in Trojia, Ptolus-style urban dark fantasy |
| — | Canon | Read `..\LordOfTrojia-MVP` before inventing; placeholders clearly marked where the book is silent |
| — | MVP world | **ONE district: THE DOCKS** (Eli re-ruled 2026-07-12, rescinding a brief all-districts expansion the same day). The Docks + a 2-3 level starter dungeon beneath it is the entire playable MVP; the rest of Granadad is off-map narrative context (gates out, ships in, rumors). ptol.us referenced heavily for the Docks-archetype STRUCTURE (shop/NPC/street/building categories) — never names/text/content. Top-down tile grid, Moonring style. |
| — | Art register | **Luminous-on-black** (Moonring style): deep black base, glowing color — the light sim is the star. First real art pass adapts the Kenney 1-bit pack (tintable monochrome) through the art-swap seam. (Eli, 2026-07-12) |
| — | Companion | **Solo Gabri in the MVP**; Devin arrives post-MVP as a content beat. The combat screen's companion seam ships empty. (Eli, 2026-07-12) |
| — | Opening | **The game opens at THE DOCKS** — Gabri starts investigating there (Eli, 2026-07-12). Starter dungeon sits beneath it (smuggler cellars → sewer outfall → drowned crypt-edge structure). Harbor ward is canon-plausible (coastal peninsula capital) with specifics marked placeholder; investigation hook aligns with Gabri's canon breach-investigation arc (bloodletter sign inside Trojian territory, trail starts at the waterline). |
| 1a | Defense | **Hybrid per-part armor**: slots head/torso/arms/hands/legs/feet/shield/cloak/jewelry. All worn pieces contribute coverage-weighted to ONE aggregate AC used for hit/miss. On hit: roll body location; that location's piece mitigates damage and takes durability wear. Bare locations are weak spots. **Spec with worked examples + unit tests BEFORE implementation.** |
| 2b | Combat flow | Separate dedicated combat screen entered on encounter contact (Skald-style), decoupled from the exploration screen |
| 3a | Progression | Full Morrowind use-based skills — skills rise through use; this powers the weak→demigod systems-exploitation curve (north star) |
| 4c | Faces | Hand-authored text-art faces for named NPCs; a LITE version of Warsim: Realm of Aslona's layered-part face generator for generic NPCs, styled to our UI |

| — | Actors | **Streets of Rogue-style emergent agents** (Eli, 2026-07-12): many actor types with needs interacting in the simulated world, producing wild scenarios. Implementation mandate: an `Actor` base class extended by one subclass per type ("we can just keep adding on top" — add a type, watch what happens). Guardrails: subclasses stay thin (declare composed behavior policies + raws-driven stats); depth-2 hierarchy; all numbers in data files. Docks roster: Militia Watch, Serf, Wastrel, Priest of the Flame, Disciple of the Flame, Shopkeeper, Animal Keeper (always ≥1 Animal), Animal (always belongs to a Keeper). Spec before code (ACTORS-SPEC.md). This supersedes v0's "no agents" fence: actors become their own milestone right after F2. |

| — | Identity (FOR LATER — Play mode; seam required NOW) | (Eli, 2026-07-12) Actors carry **trueIdentity vs presentedIdentity** (Persona value object). PC builder gets `actAs()`, runtime `setActAs()` for disguises. ALL social systems (law, deference, faces, logs, witness reports) read the PRESENTED identity; disguise = voluntarily shedding Wielder immunity; Flame powers can pierce presented→true. Canon precedent: Gabri's deliberately plain features/hidden red hair; Bledhreft impersonating Senator Harris — the same mechanism powers demon impersonators, and the Flame's pierce is its counter. **Seam requirement for ACTORS-SPEC/F2.5: the Actor base's identity field must be Persona-shaped from day one** (two slots, presented defaulting to true); full disguise gameplay lands with Play mode. |

| — | Goals & Jobs | (Eli, 2026-07-12) **Every actor has a GOAL — a reason for being on-screen.** Goals come from a **Job taxonomy**: `Job` base class with hierarchical nested extension giving type-safe signatures — `Job.Serf`, `Job.Serf.Farmer`, `Job.FlameOfMerc`, `Job.Villain.Robber`, `Job.Villain.Cutpurse` — enumerated/bound from a .json file at startup. Reconciliation (per approved actor guardrails): Job classes define type identity + goal behavior; the JSON raws define data (spawn weights, goal parameters, schedules); startup binder validates the two 1:1, fail-fast on mismatch. Open for ACTORS-SPEC to resolve: exact Job↔Actor-subclass relationship (recommended: Actor subtype = what you are; Job = why you're here; secret jobs pair with presentedIdentity — a Cutpurse presents as a Wastrel). |

Spec documents (produced by the gameplay spec track, live in this folder):
- `COMBAT-SPEC.md` — defense math, hit location, durability, worked examples, unit-test list
- `PROGRESSION-SPEC.md` — skill list, use-XP formulas, attribute interplay, exploit seams
- `COMBAT-SCREEN-SPEC.md` — encounter transition, screen layout, action economy, UI
- `FACES-SPEC.md` — face file formats, generator part model, determinism, examples

Sequencing: specs first (this track), implementation lands as the G-milestones after the
sim-core F-milestones provide the world the game plays in. The "no gameplay in v0" scope
contract applies to the SIM core's F1-F8; the G-track builds the game layer on top of it.
