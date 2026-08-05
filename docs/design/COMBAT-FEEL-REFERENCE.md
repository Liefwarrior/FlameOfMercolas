# Combat and movement feel — the Barony reference

Source: a 250-second capture of Barony v5.0.2 played by Eli on 2026-07-31, analysed frame by
frame at 28 fps (35 ms per frame). Eli named Barony as the reference for how Granadad's
first-person movement and combat should feel.

This document records **what was measured**, not what it felt like. Audio was not captured in a
form that could be analysed, and audio is a large part of Barony's punch — treat every timing
here as the visual half of a two-part effect.

---

## 1. The hit-feedback vocabulary

Five distinct channels fire on a connecting blow. They are layered, not alternatives, and that
layering is most of the "crunch".

| Channel | Onset after hit | Lifetime | Notes |
|---|---|---|---|
| Floating damage number | same frame | ~570 ms (16 frames) | Appears at full size and weight, then shrinks and fades. Barely drifts — it does **not** arc. |
| Blood particle spray | 1–2 frames (~35–70 ms) | ~300 ms | Small red quads bursting from the contact point. |
| Hit markers (yellow ✕) | ~2 frames | ~400 ms | A scatter of small ✕ sprites around impact. Reads as the "connect" tell. |
| Blood decal on floor | ~10 frames (~350 ms) | persistent | Large irregular red polygon. Stays for the rest of the level. |
| Target health bar | same frame | while engaged | Orange bar + target name, above the target. Not shown until first contact. |

**Damage numbers are colour-coded** — gold `10` and white `15` were both observed on the same
target, so colour carries a meaning (damage type or critical) rather than being decorative.

**On death**, the body is replaced by: a persistent red pool, a coloured gib blob, and dropped
loot as physical world objects (coins, a ring). Nothing vanishes. The aftermath of a fight stays
in the room and is still there when you walk back through.

### What this means for us

The crunch is **not** one big effect. It is five cheap effects stacked inside 400 ms, three of
which persist afterwards. That is buildable against a tile sim without touching the sim: every
one of these is presentation, driven off an existing combat event.

---

## 2. Movement

- **Continuous, not tile-snapped.** Barony's levels are grid-built but the player moves freely
  through them with collision.

  **RULED (Eli, 2026-07-31): continuous movement, Barony-style, not Grimrock-style tile
  stepping.** This supersedes the "it can still be tile-based movement" line in the original
  first-person brief.

  **The consequence that matters.** sim-core forbids float/double in state (ArchUnit-enforced) —
  that rule is what makes the world hash cross-platform and the goldens meaningful. Continuous
  movement must therefore be **sub-tile fixed-point, not floating point**: a position of
  `(tileX << 8) | subX` in Q8 gives 256 steps across a tile, which is far finer than the eye
  resolves at walking speed and stays exactly, reproducibly integral.

  Do **not** solve this by making the player's position client-only float state. That splits the
  player out of the simulation, and the moment combat, shoving, occupancy or line-of-sight needs
  to know precisely where the player is standing, the authoritative answer lives in the renderer
  — which is the bug we spent Sprint 7 paying for in a different costume.

  NPCs stay tile-stepped in the sim and interpolated at draw time (the existing stride-easing
  path); only the player gets sub-tile authority to begin with. Widening it to NPCs later is a
  resolution change, not an architecture change, which is the point of choosing fixed-point now.
- **KEYBOARD turn measured at ~60–70°/sec. THIS NUMBER IS ABOUT KEYBOARDS AND NOTHING ELSE.**
  Deliberate and weighty on the arrow keys. Mouse-look was **not measurable** — the game reads raw
  relative deltas that injected input does not produce — so this capture says *nothing whatever*
  about how fast the camera should turn in normal play.

  > **DO NOT GENERALISE THIS NUMBER. It has been generalised once already and it cost a sprint.**
  >
  > S5 read "60–70°/sec" as the reference turn rate for the game and set `sim::kTurnRate` to
  > 197 BAM (64.9°/sec). That is a 2015 roguelike's *keyboard fallback* — a control path most
  > Barony players never touch — promoted to a design statement for a first-person game whose
  > primary aim path is a mouse. Turning round in the Docks took five and a half seconds, and
  > when Eli played the build the verdict was "unnecessarily archaic".
  >
  > Task #77 fixed it. Mouse look is raw relative input with **no rate limit of any kind** and
  > never inherits anything from this line. Arrow-key turning is an **accessibility fallback**
  > and now runs at 140°/sec, where modern keyboard turning sits.

  (The original note about the Java build's 165°/sec reading as twitchy applied to *keyboard*
  turning too, and that comparison stands. 165°/sec on a keyboard is fast; 165°/sec on a mouse
  is a slow flick.)
- **No tile snapping, no arrival easing.** Our `STEP_ARRIVAL_FRACTION` / `strideEase` machinery
  exists to smooth discrete tile steps. If movement goes continuous, that machinery is replaced
  rather than tuned.

---

## 3. Presentation

- **The HUD hugs all four edges and leaves the centre completely clear.** HP/MP as small chunky
  bars bottom-left, hotbar bottom-centre, XP bar top-centre, action panels and minimap
  bottom-right. Nothing floats in the play space.

  This is the direct answer to task #66 ("first person: the HUD eats half the view"). Our
  inspector sheet and craftings bar occupy the right half of the viewport; Barony's equivalent
  information is either pinned to an edge or opened as a modal panel that the player dismisses.
  Full-screen inventory/character panels *do* cover the view — but only while explicitly open.

- **Lighting is the atmosphere.** Torches are real point lights with warm falloff across nearby
  stone; everything beyond their reach falls to near-black, so architecture reads as silhouette
  against a cloudy skybox. The darkness is committed to rather than lifted for legibility.

- **Texture treatment is the identity.** Very low-resolution textures magnified with
  nearest-neighbour filtering, plus visible perspective warping on floor planes. Deliberately
  chunky at close range.

- **Billboarded sprites confirmed** — grass tufts are camera-facing quads. The same technique
  Eli specified for actors drawn over the authoritative tile sim.

---

## 3a. Audio-correlated combat rhythm (second capture, 2026-07-31)

Two further clips were captured **with audio** (116 s and 17 s). Audio transients were extracted
at 50 ms resolution and grouped, which locates every impact to the frame without needing to
watch the video. Technique worth keeping: `astats` peak-per-window → group above a threshold →
correlate with a 1 fps contact sheet.

Measured combat clusters in the 116 s clip (peak dB in window):

| Window | Transients | Peak | What was on screen |
|---|---|---|---|
| 41.0–44.7 s | 49 | −1.2 dB | dense rapid impacts |
| 55.9–59.6 s | 22 | −0.2 dB | destroying a **cart** |
| 61.2–64.2 s | 9 | −0.1 dB | cart breaks into pieces |
| 105.7–110.5 s | 59 | **0.0 dB** | skeleton fight, ends in death |

**Combat is loud and dense.** The fight clusters run at or near full scale (0 dB) against a
−16.5 dB median for the rest of the clip — roughly a 16 dB jump. Fights are the loudest thing
that happens, by a wide margin, and they are *busy*: 59 separate transients across 4.8 seconds
is ~12 distinct sounds per second. Crunch here is partly just **density of audio events**.

**Destructible props carry the full combat vocabulary.** A cart has its own name label, health
bar and damage numbers (`2`, `7`) and "breaks into pieces" with debris. Scenery is not scenery.

**Spell impacts reuse the melee feedback exactly** — magenta projectile, `12` damage number,
named health bar ("skeleton"), yellow ✕ markers. One feedback system serves both, which is the
cheap and correct way to build it.

---

## 3b. The melee swing — MEASURED (fourth capture, 2026-07-31)

Closed at last. A mace-and-shield build, captured swinging **at air** in the open (isolating the
animation from all impact effects) and then **connecting** in a dungeon. 28 fps, 35.7 ms/frame.

**Rest pose.** Weapon held to the RIGHT of frame, angled; shield occupies the bottom-LEFT corner.
Both are large. This is a very different composition from the centred two-handed pose — the
centre of the screen stays clear.

**The swing arc.** Not a poke, not a small wrist flick. The weapon sweeps in from the **lower
left**, arcs **up and across** to upper-centre, and **exits the top of the frame entirely**. It
crosses most of the screen height. During recovery the weapon is **completely off-camera** —
there is no visible return stroke, the arm simply is not there for several frames and then the
next swing begins.

| Beat | Frames | Time |
|---|---|---|
| Full swing-to-swing cycle | 32 | **~1.14 s** |
| Weapon fully absent (recovery) | ~8 | ~285 ms |

That off-screen gap is worth copying: it means the animation never has to solve a graceful
return-to-idle, and the absence itself reads as recovery.

**The impact burst.** On connect, a large **radial starburst of yellow-gold rays with a dithered
checkerboard core** fires at the contact point. Screen-space large — roughly a quarter of the
frame width. Measured lifecycle:

| Beat | Frames | Time |
|---|---|---|
| Appears (already sizeable, not from zero) | 1 | 36 ms |
| Expands to peak | ~4 | ~140 ms |
| **Fragments** and fades | ~6 | ~215 ms |
| Total | ~10 | **~357 ms** |

Two details that make it read as pixel art rather than as a generic particle effect: it **pops
in already large** rather than scaling up from nothing, and it dissipates by **breaking into
fragments** — the rays snap into separate chunks that thin out — instead of fading uniformly in
alpha. Both are cheap and both are the difference between "crunchy" and "soft".

**VERIFICATION GAP (combat-ref):** the starburst was captured in a fight and correlates with
0 dB audio transients, but the target was not clearly visible in the dark frames. It is
*probably* the weapon-impact effect; it could be a strike against terrain. The **timings above
are measured and reliable**; the **attribution is inferred**. Confirm against a lit fight before
building a damage-type distinction on it.

**Still not measurable:** hitstop. The 60 fps capture setting did not take (the clip is 28.6 fps),
so a 2-frame freeze cannot be told from a dropped frame.

---

## 4. The death and restart loop

Eli, 2026-07-31: *"I want the ease of quickly restarting when you die that Barony has with the
awesome and amazing depth and rich lore that my setting and engine provide."*

Barony's pause menu carries **Restart Game** as a first-class item beside Settings and Quit.
The death sequence itself was captured and measured:

| Beat | Timing | What happens |
|---|---|---|
| Kill | t+0 | Camera drops and **tilts** — you fall to the floor and keep watching from there. |
| Desaturation | t+0 | A heavy grey wash over the whole frame. **No fade to black.** |
| Hold | ~4.5 s | You lie there. The room, the thing that killed you, the loot you'll never reach. |
| Panel | t+4.5 s | The tombstone modal appears. |
| Spectate | indefinite | **The camera keeps drifting** around the room behind the panel. |

**The death screen is a designed artifact, not a system dialog.** It is a carved tombstone
crowned with a gargoyle and lit by a green flame lamp, reading:

> **You have died.** · *[character name]* · Killed by: *[killer]* · "They sleep in memory…" ·
> You placed at position *N* in local high scores

**This is the lesson, and it is not "make restarting fast".** Restarting is cheap *because* the
death is treated as an occasion — named, attributed, memorialised, scored. Ceremony at the end
is what buys you a frictionless beginning. A death screen that is just a button reads as a
failure state; this reads as an epitaph, and you press Restart willingly.

The 4.5-second hold before the panel matters too. It is not a stall — it is the beat where you
work out what killed you, which is the entire feedback loop of a roguelike.

**RULED (Eli, 2026-07-31): no persistent ward from the start.** The proposal that the Docks
survives your death and you return as someone else who inherits a world your last life shaped
was considered and deliberately **deferred** — verbatim: *"Let's not do persistent ward from the
start, it's easy to change that later."* Build the ordinary restart first. The persistent-ward
model stays recorded here as a known option, not a commitment, and nothing should be architected
to depend on it.

---

## 5. Verification gaps in this reference

- ~~MELEE SWING UNMEASURED~~ — **CLOSED 2026-07-31** by the fourth capture. See §3b.
- **Impact-burst attribution is inferred, not confirmed** — see the VERIFICATION GAP note in
  §3b. Timings are solid; what exactly was being struck is not.
- **No hitstop measurement.** Whether Barony freezes frames on contact still cannot be
  determined — all four captures came out at ~28 fps and a 2-frame freeze is indistinguishable
  from a dropped frame at that rate. Needs a genuine 60 fps capture; the Windows Game Bar frame
  rate setting did not take effect on the attempt.
- **Audio was correlated, not characterised.** §3a locates impacts precisely in time via
  transient analysis, but says nothing about *timbre* — whether hits are thuddy, metallic,
  wet, layered. Loudness and density are measured; sound design is not.
- **Mouse-look sensitivity and acceleration unmeasured** — the game reads raw relative deltas
  that injected input does not produce, so this cannot be measured through automation at all.
  It needs a human report.
- **Footstep cadence not separated** from the general transient stream.

The audio-transient technique in §3a is the reusable part: it finds every impact in a clip
without watching it, and it should be the first step on any future capture.
