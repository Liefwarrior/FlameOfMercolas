# Ship note — read this first

**EVICTION is real, and it was driven both ways.** The owner's third case —
his own brief, working title EVICTION — ships on `content/raws/quests/
eviction.json` + `eviction_letters.json` and the Session wiring that reads
them. A priest of the Flame (Father Maell, the Mission's own, who authored the
courier sheet) hires a good open hand off his evening table in the Gull to
serve a **writ of distraint** on a compound flat in the evening once the family
is home. **Participate** and the paper changes hands, nobody swings, and the
Mission pays off the charity shelf in a tool fit for the work — **The Evictor**,
oak and lead-wrapped, blunt, with a chance to knock a man cold with a blow to
the crown. **Disrupt** and you carry the writ back to Maell whole; he does not
call it mercy and does not send for the Watch, and the temple remembers whose
nerve went. Both paths close the book; only one pays a weapon.

The case sits on the deepest ruled machinery in the project (DECISIONS.md
"Trojian tenure"; DOCKS-GAZETTEER §2.8): the Flame holds the plot, a Den Duke
holds the charge, **rent is ground rent**, and **eviction is a priest's
adjudicated distraint with six possible answers** — the writ names the fifth
(a distraint) and says the other five were open. The word "lease" appears
nowhere in the paper; the ward's own vocabulary — charge, ground penny,
petition, distraint, pledged, bond — carries it, and a test pins the absence.
Rooftop lodgers "go with the house" and are a flagged dead-end, exactly as
§2.8 rules. The war lore is the owner's, spelled to canon: the **Mercian**
shipments stopped when the **Dezdant** war with **Trojia** took the sea lanes,
so the widow squeezes because she is squeezed.

**The Evictor** (`sim::Weapon::Evictor`) sits on the brawl's legal side by the
enum's own order, does cudgel-parity damage (7), and on a landed blow reads a
24-of-256 head band off the SAME roll — in-band crowns the target to zero
through the existing downed door, so the +1/s heal and the quarter-health
stand-up run untouched. Sheet line **`THE EVICTOR 7-9 IMPACT`**; the character
sheet grows **`IN HAND  THE EVICTOR 7-9 IMPACT`** once armed.

> **Combat direction, new this day (DECISIONS.md, f5e440b):** the owner
> scrapped the JRPG/paused-screen combat register for **Oblivion-style
> real-time action** — a spell button, swing, block, hold-to-swing-hard. This
> *vindicates* the Evictor's KO as built: the crown is a weighted draw on a
> landed **swing**, not a called shot behind a menu — it drops straight into
> the new action model with no aiming screen. A scout+design program is
> chartered to settle how the brawl's legal/social classification composes
> with in-world lethal combat; the Evictor's non-lethal crown is on the right
> side of that line already.

Gate: **green.** World hash **`0x2646C1AAA2BA38DF`** — regenerated, rebaked,
twin-gated twice, unmoved. Base tip this ship: **`f5e440b`** (the owner's
docs-only combat ruling landed mid-phase; native/ untouched, gate stamp still
names the tree).

## Run it

```
.\dist\granadad.exe                       # play it; M for the map, T travels a named place
.\dist\granadad.exe --eviction            # PARTICIPATE, end to end, headless (9 beats)
.\dist\granadad.exe --eviction=refused    # DISRUPT, writ carried back (8 beats)
.\dist\granadad.exe --eviction=writ       # shutter: the writ open on the Letters tile
.\dist\granadad.exe --eviction=gate       # shutter: the Netters' gate lead read
.\dist\granadad.exe --eviction=knock      # shutter: the door answered
.\dist\granadad.exe --eviction=served     # shutter: paper changed hands
.\dist\granadad.exe --case                # THE QUIET TENANT (courier) end to end
.\dist\granadad.exe --map-overlay --travel="The Ropewalk"   # the travel probe
```

Summary segment: `| evict beats=N/9 mask=… read=4/5 knocked=… served=… closed=…
weapon=fists|armed letters=3`. Both drives are byte-identical run-to-run.

**In the world:** hire Maell in the Gull (topic 8, THE MISSION'S WRIT — needs
open hand ≥ 15, and it names the bar and your number if you are short); read
the writ in your Letters (J, then the Letters tile); **M** the map, hover the
**Netters' Compound** and **T** to travel; walk to the Marrow door on the
Gullet lane and press interact to knock, again to serve; travel to the Mission
and walk in to close and be paid. To SEE a knockout: once armed, brawl a Gull
patron (attack key) and keep swinging — 24/256 of landed blows crown (~1 in 11).

## The gate, in full

| half | where | result |
|---|---|---|
| `docker compose run --rm --build build` | fresh worktree `granadad-ship-5b072dc` (four audio dirs real-copied, 374 files) | **exit 0**, stamp 2026-09-02T18:13:57Z |
| `scripts\verify-windows.ps1` | fresh worktree | **=== PASS ===** |
| `scripts\verify-windows.ps1` | main repo dist/, re-run | **=== PASS ===** |

```
native/ digest:   01f332514efbabac3c45e10edece315c18fe920404f9e4563b7ec2556aa27f57
ctest cases:      1041 (floor 537), 0 failed
comparators:      world-hash+sim 1791 bytes sha256 924F6EA6…B7B8468B, linux/gcc == mingw
```

## The world hash did not move — regenerated, rebaked, twin-gated

```
1. regenerate   python tools\scripts\gen_docks_surface.py
                docks_surface.tmx sha256 CCEDA566…237B4D1C, tree clean (reproduced)
2. rebake       gradlew :tools:run --args="import-map <abs>.tmx <out-file> --raws <abs>\content\raws"
                17,954 bytes sha256 E47DA3AE…E474C2AC == content/maps/baked/docks_surface.trojsav
3. twin-gate    granadad-twin-gate.exe --population --population-hour 16 --ticks 7200
                run twice: 0x2646C1AAA2BA38DF both, byte-identical; --tavern half identical too
4. content diff git diff --name-only 31f092e..HEAD -- content/  ->  only the two new quest raws
```

No tavern-hash constant was owed a re-bless (grepped: only mix64/salt golden
vectors and seed literals; the Edged 3→4 renumber reaches no gate workload —
verified). The evictor lane's own golden sweep is honestly counted: the sweep
multiplier is even, so landed is exactly 3072/4096, not the naive 7-in-8.

## Driving it, this phase — at 640x360

The full case both ways is photographed in `docs/frames/eviction1/` (the
integrator's deterministic `--eviction` shutters): **ev1** the writ (Annis
Netter, 130/260, "I gave the fifth"), **ev2** the gate lead, **ev3** the door
answered, **ev4** the writ served, **ev5** "THE EVICTOR IS YOURS", **ev6** the
disrupt yield, **ev7** the sheet reading **`IN HAND  THE EVICTOR 7-9 IMPACT`**
with all three papers in the Letters pane.

This phase drove the case **live in the windowed build with real keyboard and
mouse** (SendInput + the game's own F12 shutter, fresh-default controls) —
`docs/frames/ship-eviction/`: **se2** the hire off Maell with the brief's
texture line on the row (*"A POOR FAMILY AND AN IMPOSSIBLE ASK. DO IT ANYWAY.
ORDER KEPT IS WHY THE CITY IS SUFFERED TO STAND."*) and topic 8 flipping to
GIVE THE WRIT BACK as the case goes live; **se3** the writ, fully authored;
**se4** the four-pane menu (character / chart / letters / casebook); **se1/se5**
the ward map with the Netters and the Mission as named travel targets; **se6**
the arrival plate (*"WALKED TO THE NETTERS' COMPOUND. 20:01."*); **se7** the
Netters' gate lead read (case → 2/3); **se8** the Marrow household present at
the compound at dusk (Frieda Marrow, a household actor). Map travel is
**hover-to-select then T** (a click faces-and-closes; only hover keeps the map
open for the T verb).

**The windowed KO was not photographed live** and stands where the integrator
left it: proven headlessly by the **4096-roll golden sweep** (every landed
Evictor blow crowns iff bits 8-15 < 24, crown ⇒ down-at-zero, damage never
leaves cudgel parity) and the **twin-room 30-swing trace** (replays draw for
draw), both under the green gate, plus the armed sheet on **ev7**. Reaching an
armed brawl by hand means a clean five-leg case run and a Gull patron in reach;
the recipe above is exact, and any harness can arm in three lines
(`session.tavern().grantPlayerWeapon(sim::kEvictorWeaponId)`) for a faster
windowed swing. Flagged, honestly, as the one beat the window has not yet held.

## The brief, item by item (his 12)

1. **Working title EVICTION** — kept, the case and its title.
2. **The spark — show/participate/disrupt an eviction** — both paths built,
   one close each, no soft-lock.
3. **Who asks — a priest of the Flame** — Father Maell, the ward's only
   priest and §2.8's required arbiter. *[veto: recast = one constant.]*
4. **The ask — good open hand to evict from a compound flat** — the hire gates
   on open hand ≥ 15, names the bar and your number if short.
5. **Nearest compound** — C2 The Netters', nearest by the street (both doors
   on Ropewynd); C3 Saltgate is nearer by ruler but an 80-tile climb. *[builder's
   call, flagged.]*
6. **Evening, once home** — 18:00–24:00, the rota's home hours; the door does
   not answer outside them.
7. **Systems — break-in & locks, brawl/subdue, dialogue, clock & travel** —
   dialogue, clock and travel exercised fully; **break-in plays as the knock +
   the served paper** (no compound door has a lock yet — a later sprint);
   **subdue is provided-for, not exercised** (the good hand is hired so nobody
   swings — the brief's own texture). *[both flagged; see MUST-NOTs below.]*
8. **The leads — Mercian shipment failed, Dezdant war with Trojia** — hearable
   in the gate lead and the three letters, spelled to canon. *(Note: Dezdant
   is canon in docs/lore/, not COMBAT-SPEC as the brief's pointer said — the
   spelling is correct regardless; the ambient rumor rows for histories.json /
   rumors.json are authored but NOT pasted, owner-canon files.)*
9. **Paper — the lease agreement** — mapped to the ward's real instruments:
   the writ of distraint (handed), the widow's petition, the served notice.
   "Lease" appears nowhere (test-pinned).
10. **Texture — the impossible choice** — the heart of it, and it lands: Maell
    speaks the brief's own line to your face at the hire, and the family is
    four named-only heads three quarters behind on the penny.
11. **Must / must-not (builder's call)** — decided and flagged: **must-not**
    build the first door-lock in this lane; **must-not** cast a resisting body
    at the door (punch can't reach ward population — a room-system misplacement);
    **must** keep the roll unmutated (the distraint is adjudicated fact, not a
    live `Ward::apply`); **must** close on both paths.
12. **The payoff — The Evictor, blunt, head-KO** — built and granted through
    the weapon lane's own seam; the KO is the whole forging.

## Rulings awaiting the owner's veto — all built, all working

- **Casting/naming** (all recastable): priest = **Father Maell**; compound =
  **C2 The Netters'**; petitioner = **Widow Annis Netter**; family = **Bram
  Marrow "the Steady"**, an existing actor given a home for the first time
  (household is "four heads", no wife/child named — three names = three more
  vetoes). **[NEEDS: your blessing on Bram, or a different tenant.]**
- **The Evictor copy** (none load-bearing): register name "the evictor"; sheet
  `THE EVICTOR 7-9 IMPACT`; sheet row `IN HAND`; KO line `CAUGHT <NAME> ACROSS
  THE CROWN. OUT COLD.`; grant id `the_evictor`. Damage 7 (cudgel parity — the
  KO is the reward); raise to 8 = one number + one test string.
- **The two closes** — participate pays the weapon; disrupt pays nothing and
  moves temple regard down through the existing ledger (`WalkedOut`), not a
  bespoke penalty.
- **The rumor rows** — Mercian/Dezdant ambient entries for histories.json +
  rumors.json authored in the lane reports but NOT pasted (owner-canon files);
  knower list flagged.

## Standing gaps (not this ship's to close)

- **open_hand is not trained by the brawl yet** (damage rides MGT): the hire
  gate reads chargen-set levels only. The gate is real; the training path
  behind it is the progression system's named gap.
- **playerWeapon_ is not save-serialized** (hashed, not persisted): the grant
  is per-run. If save/exit persistence lands, the case should re-fire the grant
  off completed-case state.
- **The subdue at the door and the compound door-lock** are specified, not
  built — a Gull-roster actor at the Netters plus a punch-target widening, and
  the district's first real lock. Both are their own sprints.

## The verdict — three cases now, and the flywheel is turning

The standing bar is "clean and near ready for early access," and the standing
blocker was **content alone**. This is the honest movement: **the third case is
the first that is not a system demo.** The courier taught the clock and the
break-in; travel taught reach; EVICTION is a *choice* — it leans on the deepest
canon in the project (the tenure machinery, six-answer distraint, ground rent),
it reuses the courier's own priest and door and pays continuity forward, and it
ends two different ways with real consequences. It also lands a reusable
reward: a weapon on the brawl's legal side with a clean KO the new action-combat
ruling welcomes.

**Still not near ready, and the reason is unchanged: three authored cases is an
evening, not a game.** But the shape of the answer is now visible — the machinery
(reach, dialogue, clock, tenure, a casebook that reads as master/detail, a
weapon system with room to grow) is carrying content that is *about* something,
and a second and third case of this weight would start to fill the well rather
than demonstrate the bucket. The flywheel is turning; it is not yet fast. Do not
round up.

## The three things to do next

1. **More cases of this weight — still the whole remaining distance.** EVICTION
   proves the content can be a choice and not a demo; the district needs a
   spread of them pulling toward the Docks' agent-of-evil arc (DECISIONS.md,
   districts-as-chapters). Nothing else blocks early access.
2. **Answer the combat scout+design charter** (f5e440b): how the brawl's
   legal/social classification composes with Oblivion-style in-world action,
   the determinism model for real-time swings, and what "scrap" removes of
   COMBAT-SCREEN-SPEC. The Evictor's crown is already on the right side of it.
3. **Close the two flagged case gaps if the door should bite:** a resisting
   body at the Marrow door (a roster actor + punch reaching ward population)
   and the district's first compound lock — so break-in and subdue are
   exercised, not provided-for. And wire open_hand to train by use so the
   hire gate reads a lived skill, not only a chargen one.

## Still open, unchanged

Fast travel and the courier case stand as last phase left them (travel is
minutes not hours, the zero-paces re-travel wart, the options page still on a
HUD strip). The windowed harness cannot drive keyboard character-creation blind
(the pad path covers it); `--travel=NAME` and `--map-place=NAME` want the
roster's exact mixed-case name. New this phase: `drive-windowed.ps1` learned the
whole digit row (topic lists select up to 9; the hire is topic 8).
