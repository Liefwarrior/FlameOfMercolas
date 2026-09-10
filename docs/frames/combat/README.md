# Combat v1 — proving it plays

The SHIP-LANE pass on `combat/build` (HEAD `b101c4d`). Every claim below is
backed by a real drive of a real feature — no bespoke harness — a live test
run, or a frame in this folder. Where a thing can't be photographed on this
machine it says so, out loud, and points at what proves it instead.

## What actually opened a window here

`scripts/drive-windowed.ps1` **opens the shipped `dist/granadad.exe`, takes the
foreground, and refuses if it can't** — that part worked (the window came up,
the harness reported the keyboard and mouse). What did **not** land was the
input itself: the boot now opens on the origin/creation screen (#80), the
`enter` presses never committed a character, and the game's own `F12` shutter
wrote no frame. That is the SendInput-to-a-foreground-SDL-window path failing on
a headless input desktop — the same class of "un-photographable windowed item"
prior ships have flagged. So the four-verbs-in-one-window shot is **not** in this
folder, and this note does not pretend it is.

Everything that drives the session **directly** — the scripted smoke lines and
the self-driven windowed directors that write the framebuffer straight to PNG —
works fine, and that is where the frames below come from.

## The frames

| Frame | The drive | What it proves |
|---|---|---|
| `casewatch/watch-6-down.png` | `--case-watch --case-watch-capture` (THE QUIET TENANT, self-driven, 8/8 beats) | **A brawl won with fists.** "FINCH DOWN, AND COMING WITH YOU." Say-row: *"A BRAWL IS THE HOUSE'S OWN LAW."* Top-right heat reads `SEEN DARK 2` — **no Watch drawn.** Legality, the lawful side of the line, in one picture. |
| `casewatch/watch-7-carry.png` | same run | The downed man is carried up — **he goes down, he does not die, the floor is intact.** |
| `casewatch/watch-8-delivered.png` | same run | The errand closes over the brawl. `beats=8/8`, and the replay "matched the drive, step for step." |
| `nemesis-01-end-promoted.png` | `--nemesis` (TARN WRENHALE, 7/7 beats) | **Lose the fight, live anyway.** The rival that was nobody is `TARN WRENHALE x3` in red (HUNTING), Foreman of a founded house holding THE GULLET — and the health bar is full, because the quay revive stood you back up. |
| `verb-cast.png` | `--cast` | The Cast verb routes through `Session::castEquipped()` — the default sheet holds no spell, so it lands on *"NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES."* The button works; the spell is a sheet you haven't earned yet. |
| `verb-punch.png` | `--punch` (1/1, post-SHIP) | The legacy smoke drive, taught to aim: it walks from the Tarwalk spawn into the Gull, puts the nearest man — Tarn Wrenhale, as it happens — dead on the crosshair and taps until the blow connects. The house minds at once: *"YOU HAVE HAD THE ONLY WORD YOU GET. THE DOOR."* |
| `verb-block.png` | `--block` (1/1, post-SHIP) | The same fight picked the same way, then the guard up through `Session::setBlocking()` and held until the room says a blow was softened — the `GUARD UP` row over a fight the guard actually worked in. |

## What a still frame can't hold — proven live instead

These ran green on `dist/granadad-tests.exe` on this Windows box **this pass**
(not from the gate's cache):

- **The swing state machine** — IDLE → CHARGING → RECOVERY, tap vs hard threshold,
  a down-edge dropped in recovery, a cast/page/conversation cancelling a raised
  charge for free. `test_combat_action`.
- **The sightline raycast** — "where you look is who you hit", and a body past the
  beam's half-width is **not** on the line (friendly fire is aim, not a species
  rule). `test_combat_action`, 2 cases.
- **Everyone can die** — a killing blow lands `Activity::Dead`, the corpse is a
  Downed that never stands, and **no roster actor is shielded**. `test_combat_action`, 2 cases.
- **Murder → heat → arrest** — a witnessed kill is instant paper: heat jumps by
  `kMurderHeat` (60), a warrant opens, the killer is marked, and the arrest returns
  `Sentence::Condemned`. An ordinary thief is only `Held`; the murder mark is what
  condemns. `test_combat_action`. (The Daggerfall court/jail/execution is the
  **next** build — veto ruling #4 — and is deliberately absent here.)
- **Intent-by-verb** — the first hard swing means Harm, and a bloodied man
  hard-swung goes Lethal **with no blade drawn**. `test_combat_action`, 2 cases.
- **The HELD HARD row and the reticle charge** — the charge shows on its row and
  drops the guard while the hand is busy; below the threshold the HELD HARD row is
  empty, at/above it reads e.g. `HELD HARD -- FISTS 6-10`. `test_tavern_render`.
- **The guard row** — blocking is the room's fact, and a conversation lowers it.
  `test_tavern_render`.
- **A landed tap** — "F throws a punch and the house minds": the wash, the reticle
  and the audio carry the blow now that the say-row diet cut the per-hit "HIT" line.
  `test_tavern_render`.
- **The death ceremony + quay revive** — `settleDefeat` lays the dip and the epitaph
  over the revive; the scripted nemesis arc plays it, not stages it.
  `test_tavern_render`, `test_nemesis`. (The mid-hold dip and the epitaph plate are
  a windowed-only animation — un-photographable here for the same input-desktop
  reason above; the nemesis stdout narrates each of the two death-ceremony defeats.)

Combined that's **32/32 combat-relevant cases, 97,282 assertions, 0 failed**, live.

## Determinism

- **A real scripted fight, byte-identical run to run.** `--nemesis` twice: trace
  IDENTICAL, frame `sha256 18cc76fb…` IDENTICAL.
- **The twin gate on the tavern fight workload** — the one that actually loses a
  fight: run A == run B == `0x0D83D18F00E2C6CE`, PASS.
- **The population baseline never moved** — `0x2646C1AAA2BA38DF` at hour 16 /
  7200 ticks, 18,772-byte report, byte-for-byte the recorded Phase C/D baseline.

## The honest gaps

1. **No windowed four-verbs shot.** Real-input windowed capture (SendInput +
   `F12`) doesn't land on this headless input desktop, and the boot opens on
   creation the automated keys couldn't clear. Tap / hard-swing / block / cast are
   each proven by test + (for tap and cast) a self-driven frame — but the single
   in-window fight showing reticle-retract, the HELD HARD row, the camera impulse
   and the steel wash together is not photographed.
2. **The lethal-win frame (corpse + Watch closing) is tested, not shot.** No
   self-driven drive has the player commit a witnessed murder, so the corpse-stays
   / heat-60 / arrest-condemns chain is proven in `test_combat_action`, not in a PNG.
3. **The death-ceremony dip and epitaph plate are un-photographed** (windowed
   animation), proven by `settleDefeat` in tests and narrated in the `--nemesis`
   stdout.
4. ~~**The bare `--punch` / `--block` smoke drives land 0/1 at the default
   Tarwalk spawn.**~~ **Fixed, post-SHIP.** They swung in place, and the sightline
   ruling retired the old radial-nearest pick — you have to be looking at the
   body. Both flags now throw the beat the nemesis and tenant lines throw: walk up
   to the nearest person, put them dead on the crosshair, tap, step the recovery
   lockout through, re-face, tap again — read as landed off the body's own hit
   points, not the say-row. From the same spawn both land 1/1 and exit 0, both
   byte-identical run to run (`verb-punch.png`, `verb-block.png`), the gate is
   green on both halves and the population baseline is still `0x2646C1AAA2BA38DF`.
