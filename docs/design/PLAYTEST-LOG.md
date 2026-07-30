# PLAYTEST-LOG — the live-GL observer playtest record

Plain defect ledger for the scripted live-GL playtests (the `--script=` tape harness,
S4 CLIENT slice 1). One entry per defect, patch-notes style: `fix/was/now` for fixed
items, `open` for filed-not-fixed. Newest sprint first.

Provenance: the S4 playtest (5 tapes, 40+ screenshots, sergeant #371 driven down and up
the Saltgate Rise) filed its findings only in commit messages; the session's fuller
ranked list was never written to the repo and did not survive the session — flagged as a
review defect, paid here (S5 CLIENT, declared debt). Entries below are reconstructed
from the committed record (`a1740fd..96db847` commit bodies, in-code javadoc admissions)
plus the S5 re-verification pass. Anything the lost ranking held beyond these is
unrecoverable and is declared as such — this file is the durable record going forward:
new playtest defects get filed HERE, in the same shape, in the sprint they are found.

## FPV pass (the first-person view) — one round

Source: Eli, verbatim — *"I want a demo with the ability to switch between 1st person and
the top-down tile view... Make sure you don't make it jarring, interpolate when moving so
that you get a really smooth high fps... I want that, but also with the ability to have a
Z-axis like Daggerfall."* Tape: `content/playtests/fpv_tarwalk.txt` (10 screenshots, the
sim walking Ditta Pilchard #1 out to the quay under her own AI before she is taken over).

- **fixed: the selection reticle floated in mid-air in first person.** was: the inspector
  drew its gold tile-highlight frame at `MapCamera`'s screen position for the selected
  tile, in every mode. With no tiles on screen that position means nothing — it landed
  near the middle of the first-person frame and read as a targeting reticle nobody had
  built. now: `InspectorRenderer.draw` takes a `worldHighlight` flag; the character sheet
  and the event feed stay in both modes (they are panels), the world-space highlight is
  drawn only by the camera that has tiles on screen.
- **fixed: screenshots of a translucent frame composited a mirror of themselves.** was:
  `flipVertically` used `Pixmap.drawPixel`, which blends by default, so any pixel the frame
  left with alpha below 1 — pooled water, a mid-cross-fade first-person frame — mixed a
  vertically mirrored copy of the screen into the PNG. It looked exactly like a rendering
  bug and it was in the evidence, not the renderer. now: blending is switched off for the
  flip and restored afterward. Pre-existing; the first-person cross-fade is just the first
  thing that made a whole frame translucent.
- **open: you have to look down to see water at your own feet.** With a 72-degree
  horizontal field of view at 16:9 the vertical fan is about 44 degrees, so the harbour a
  band below the quay does not clear the bottom of a level frame until roughly a dozen
  tiles out. This is correct perspective and it is what the horizon shear is for, but it is
  worth a decision later: a wider field, or a small default downward bias when the eye is
  standing at an edge with air beyond it.
- **open: the ward's names are invisible in first person.** The place-sign hanging signs and
  the NES pop-up are drawn against the tile camera's screen positions, so they are skipped
  while the first-person frame is up and 39 named doors go back to being anonymous doors —
  which is the exact defect the S8 pass was opened to fix, reintroduced by a camera that
  cannot draw them. Not fudged with a screen-space guess: a sign belongs on the door, which
  means projecting it like any other surface, which is a real piece of work.
- **open: no top-down inset in first person.** The compass and the `eye z=` readout carry
  bearing and band, which is enough not to get lost, but the survey's suggestion of a small
  corner minimap is still the thing that would make the ward's verticality legible while
  you are inside it.
- **open: front and side of a building look identical.** Neither shipped art pack authors a
  per-face form token, so every wall face draws the plan-view `wall` region. The resolver
  already keys on a form token, so this is a pack change (`face`) with no renderer work —
  filed, not fudged.

## S8 pass (place labels: the NES sign box) — two rounds

Source: Eli exploring the shipped ward, verbatim — *"We also need to label buildings!
Let's use NES style black box pop ups with signs (just like in zelda 2 or in early final
fantasy games)"*. The defect underneath it: 40+ authored buildings with real names,
histories and notable residents, and nothing on screen said which was which.

- **fixed: the ward was anonymous.** was: a building's name existed only in
  DOCKS-GAZETTEER prose; the baked world knew its walls and its work anchor, never what
  it was called. now: a `place_sign` marker class in the district's own `markers` object
  layer (authored in `gen_docks_surface.py`, validated by `MarkerContractPass`, read by
  `PlaceSignsLoader`), carrying the door cell, the two lines a reader gets, and the site's
  FOOTPRINT rect. 39 places signed: K01–K34, K36, and the four Compounds.
- **fixed: nothing showed a place was worth approaching.** was: every door looked like
  every other door. now: a persistent hanging shop sign — bracket arm, hanger, plaque —
  over every named door, culled to the camera box and depth-shaded like the terrain
  under it. A pan across the district shows which doors are somebody's.
- **fixed: no way to read a name.** now: one NES pop-up — hard-edged pure-black field,
  crisp 2px bone border, square corners, opaque, no fade — snapping on over the tile the
  viewer is attending to. Observer reads the cursor at 3 tiles' reach; Play mode reads
  the driven actor at 1 tile (the doorway rule).
- **fixed (pre-empted): 40 boxes would have been worse than none.** The clutter law is
  structural, not tuned: signs are marks and never text, and the words are a strict
  singleton. There is no zoom at which a second box can appear — proved at zoom 1 over
  the whole district (~20 marked doors, one box).
- **fixed (pre-empted): a label floating over the wrong roof.** A place below the view
  plane draws only where the look-down resolves that column to its own z' — the exact
  `FishingSpotOverlay` rule. Under a roof you are looking at, sign and box vanish together.
- **fixed in round 2: streets and waterfront features carried no signs.** was: the
  Tarwalk, Saltgate Rise, Ropewynd, Herring Lane, the Gullet, the Long Quay, Pier Row,
  Wormwood Pier, the fishbone pier and its fingers, the Beaching Strand and the Outfall
  were all named in the gazetteer and all unlabelled — pan the ward and every road was
  anonymous. now: 26 `kind=way` signs, from the gazetteer's own §2.2/§2.3/§3.2 wording.
  A way's mark is a kerb FINGERPOST, not a hanging plaque (a plaque hangs over a doorway
  and a street has no door), and a long way carries several of them, one per segment
  rect, all saying the same words — six down the Tarwalk alone.
- **open: the box says nothing about who is inside.** The gazetteer binds named
  proprietors to real spawned actors (§3, the Forty Notables), and the pop-up does not
  read them. "The Gilded Gull / captains' tavern" could be "…/ Master Venn keeps it".
- **open: the pop-up is not a verb.** It names a place; it does not offer to enter,
  ask about, or note it. Whether a label should become an interaction surface is a
  design call, not a defect.
- **fixed in round 2: pointing at a place's own sign named a DIFFERENT place.** was: six
  of the 39 plaques handed the box to a neighbour when the viewer pointed at (or stood
  on) that place's own door — nearest-wins gave it to whatever rect the door cell sat
  inside, and the one gesture a player is certain to make returned a lie. now: an ordered
  tie-break, written down where the rule lives — a mark cell always names its own place,
  else nearest, else the more specific (smaller) footprint, else a building before a way,
  else authored order. Pinned by a sweep over EVERY signed place in the committed map,
  both modes: 0 lies, versus 28 with the mark rule switched off.
- **fixed in round 2: the box painted over the UI.** was: drawn last with no zone
  awareness, so it sat on the hover nameplate systematically, and on the status/pulse/
  purse block and an open inspector sheet whenever the named place was under them. now:
  the box is handed the frame's committed zones and MOVES — above the tile by default,
  then below, right, left, and the foot of the screen, first clean placement wins.
- **fixed in round 2: the text was not blocky.** was: Eli's ruling asked for Zelda II /
  early Final Fantasy text and the box shipped libGDX's ANTI-ALIASED default `BitmapFont`
  — every proof PNG was painted with the repo's blocky 8×8, so the images that proved the
  look were not what the game drew. now: every letter is a quad from
  `PlaceSignArt.textQuads` (the 8×8 font at 2× with no filtering); `PlaceSignRenderer`
  holds no `BitmapFont` at all, and the raster proof calls the same emitter — so the PNGs
  and the frame cannot diverge again. A texel-exact test asserts the glyphs ARE the
  font's bits.
- **fixed in round 2: the box never snapped off inside a large site.** was: play-mode
  distance was measured to the FOOTPRINT, which is 0 across a whole 64-tile shed, so the
  pop-up was permanently on. now: in play mode a building speaks from its DOOR — on at
  the threshold, off two steps in. A way still speaks from its whole run.
- **fixed in round 2, three minors.** The marks take the day/night ambient like every
  other scene draw (65 plaques no longer burn at noon brightness at midnight); the lit
  mark is a signal cyan instead of the inspector gold that was byte-for-byte
  `NameplateRenderer`'s KIN tint; `distanceTo` is chebyshev, which is what its javadoc
  always claimed and what the sim's own work reach uses (a doorway's diagonal was
  silently excluded at reach 1); K13's second line reads "condemned; officially empty 9
  years" — the gazetteer's own wording, where the old line asserted as fact the fiction
  the site exists to disprove.
- **open: no live-GL tape covers the labels.** The look is proved by the headless raster
  (`PlaceSignLookTest`, `PlaceSignWardProofTest`, PNGs in
  `client-observer/build/place-sign-proof/`). Since round 2 that raster paints the
  renderer's OWN quads for every rectangle and every letter, so the divergence that used
  to sit here is gone — but nobody has yet watched the box on a real GL frame.
- **open: the keep-out zones are the frame's committed panels, not the world.** The box
  dodges the HUD; it can still land on an actor you were watching, or on the very
  building it names, when all five placements are crowded. There is no "point at me"
  arrow either — the lit mark is the only tie between the words and the door.
- **open: a way's segment rects are authored, not derived.** The Tarwalk's six posts
  carry six hand-typed rects read off the `frect()` calls beside them. A street re-routed
  in `gen_docks_surface.py` will not move its own signs; the generator's self-check only
  catches a sign that drifts off the rect it claims, not a rect that stopped being the
  street.

## S6 pass (the live-ops fix sprint: fishing, death, motivation legibility)

Source: the S6 observer diagnosis (60k-tick instrumented soak of the shipped seed —
DUTY district-collapse by day 1, 2.28M pushes, 32.8% guard-jam ticks, two commute
spikes, statue laborers, zero deaths) + the S6 CLIENT fix phase. Sim/World causes
landed in their own phases; entries here are the CLIENT surfaces.

- **fixed: fishing spots had no renderer.** was: nothing drew sim-side registries in
  world space — a surfaced spot was invisible from every z. now: FishingSpotOverlay
  (GL-free plan) + FishingSpotRenderer, z-order terrain -> water -> SPOTS -> actors,
  depth-vision-aware (the harbor surface is 1-2 z below the quayside view plane);
  size classes read apart (foam ring / teal ring / heavy gold ring + glint).
- **fixed: perception had no visibility split.** was: n/a (no spot rendering). now:
  observer god-view draws ALL live spots; Play mode draws only spots the played soul
  perceives, read from the sim's own `FishingSpots.visibleTo` — the client never
  rolls visibility itself.
- **fixed: no fish verb.** was: the played soul could not cast. now: R arms
  `setPlayerFishIntent` (EatInput pattern); outcome toasts + the catch check's
  visible dice (`[Fishing 0 vs deep water 40: 10% -- CAUGHT]`); tape verb `fish`;
  R on the play-mode HUD legend. Skill-up toasts ride the existing SkillUpTracker.
- **fixed: death was invisible on every surface.** was: an EXECUTED/DEAD soul drew as
  a living standing sprite; feed printed the debug shape
  `reason X -> EXECUTED_SECOND_OFFENSE`; the sheet showed live-looking needs/goal and
  raw hex status; nameplates identical to the living. now: corpse treatment (squash +
  blood-gray dim, both render passes, no fade — the ward keeps its dead in view);
  DeathFeedTracker names every DeathLog row ("<name> has died -- starvation",
  hangings on the CRIME lane); the sheet reads `*** DECEASED ***` with goal/reason/
  needs suppressed; nameplates carry `(dead)`; a starved corpse is no verb target
  while the gibbet keeps its authored `mood.dead` talk surface.
- **fixed: status printed as hex.** was: `status: 0x100`. now: words
  (`status: EXECUTED, DEAD`), for the living too; MAIMED transitions get an authored
  sentence instead of the enum line.
- **fixed: motivation collapse was illegible.** was: a bottomed DUTY bar looked like
  any low bar; the promised clergy FAITH relabel was unimplemented; a district-wide
  collapse required clicking 638 souls. now: depleted bars read alarm-red with " !";
  clergy DUTY reads FAITH; the HUD carries the district pulse line
  (`pulse: 692 souls  working N  duty-out N  starving N  held N  confined N  dead N`).
- **fixed: the twin-run report missed the S6 pathologies.** was: DocksActorsMain
  printed no DUTY/movement-wave/jam/statue numbers — the diagnosis needed a throwaway
  instrument. now: S6 MOTIVATION / MOVEMENT WAVES / GUARD JAMS / STATUE CENSUS /
  FISHING / DEATH sections in the twin-compared report, deterministic ascending scans,
  with the diagnosis baselines printed beside each metric.
- **open: ground items still don't render.** A landed fish on a quay, dropped goods —
  items exist only as the sheet's `carries:` line. Declared cut this sprint (again).
- **open: the sheet's DECEASED banner names no cause.** The feed line carries the
  cause off the DeathLog; the sheet just says deceased — wiring the log into the
  sheet is a small follow-up.
- **open: spot markers are procedural rings, not pack art.** The overlay draws from
  the shared white pixel; a TileArtResolver-style seam could hand the look to the art
  pack when one ships spot ripples.
- **open (carried from S5):** no save/load verb; skill-count frame guard invalidates
  old saves (now 18->19 too — the S6 fishing raw); journal/masters pane has no
  scroll; feed-filter state invisible outside the feed header.

## S5 pass (tapes 9/9b/10, this sprint)

Tape 10 (tape10-s5-surfaces.txt) exercises the sprint's new surfaces live: the masters
board at boot (seeded masters, no climbers) and after a working morning, the GROWTH
feed lane through the L filter, the deepened sheet on sergeant #371, and the played
full roster (screenshots S5-1..S5-5).

- **fixed: depth-vision GL evidence gap (S4 EPIC debt).** was: tape9-depth-vision.txt
  was scripted but never run — all depth evidence rested on the Python compositor
  replica, not the shipped Java renderer. now: tape 9 run against the live sim
  (18,010 frames, docks); GL1-GL4 screenshots captured from the real GL pipeline
  (overlook day z21, band-B day z20, close overlook, overlook night). Playtest
  find on the tape itself: its 6000+ shot times catch the commuters already AT
  their anchors — empty streets under the overlook; tape 9b re-shoots the same
  framing at commute time (tick 1500, plates held) for the actor-through-air
  payoff (GL5-GL6).
- **fixed: the growth torrent (pre-emptive, S5 slice 1).** was: every non-played
  level-up was one feed line into a 30-entry log — the awards wave would have evicted
  every crime/quest line within a day. now: per-phase growth digest + named milestones
  + L feed filter + 120-entry log (commit `2c10e79`).
- **open: no save/load verb.** A session's world dies with its window; the two-press
  ESC guard (S4 fix2) narrows the accident, nothing persists. Needs the sim's TROJSAV
  chunk behind a client verb — cross-lane, unscheduled.
- **open: skill-count frame guard invalidates old saves.** 16-skill-era TROJSAVs
  refuse to load against the 18-skill raws (loud fail by contract, PROGRESSION-SPEC
  §2). Documented, deliberate, still a sharp edge a save-verb feature must own.
- **open: the journal/masters pane has no scroll.** MastersBoardText is 25 lines and
  fits at 1280x800; a taller roster (more skills, longer quest logs) will clip below
  the viewport bottom. Filed when the M pane landed (S5 slice 3).
- **open: feed filter state is invisible outside the feed header.** The `[GROWTH]`
  tag is the only tell; a filtered-empty feed looks like a dead ward. Consider a
  louder empty-lane line ("no growth yet — L cycles").

## S4 playtest (5 tapes, sergeant #371, the Saltgate Rise)

Top-5 fix pass, landed as commit `d128322`:

- **fixed 1: no on-screen legend for any social/play verb.** was: HUD listed
  pan/zoom/z/time/ESC only; P/I/T/G/N/C/J/E appeared nowhere. now: third HUD legend
  line on populated fixtures — observer: click select / C follow / P play as / N names
  / J journal (+ M masters / L feed since S5); play: T talk / G pickpocket / E eat /
  Up-Down climb / I disguise / J journal / P release.
- **fixed 2: ESC insta-quit.** was: one keystroke closed the observer, no confirm, no
  save verb exists. now: two-press confirm on populated fixtures (QuitGuard, 2s
  window); tavern keeps instant exit; talk-panel ESC ownership unchanged.
- **fixed 3: toast lifetime ignored text length.** was: a 100-char check line got the
  same 3s as "Grit increased to 5". now: +0.05s/char past 40 chars, capped 8s
  (ToastQueue.lifetimeFor); short toasts byte-identical to the old contract.
- **fixed 4: follow-cam ate manual z-scrub every frame.** was: the scrub registered
  and reverted one frame later — input silently dead. now: edge-triggered snap
  (FollowZSnap) on retarget/follow-on/band change only; the level peek survives.
- **fixed 5 (the playtest find): climb exit dumped the climber off its road.** was:
  connectorFrom returned the first baked link — usually a ramp WEST exit; tape 3
  proved a 170-tick stall at (103,128,20) pushing a wall. now: deterministic
  preference straight-column stair > facing-matched exit > baked order; the identical
  tape climbs the full Rise z19->z21.

Filed and fixed in the other S4 slices:

- **fixed (defect c): played-actor starvation.** was: PLAYER_CONTROL (2000) outscores
  SEEK_FOOD (~1305) — a driven soul could never eat and starved on any long session.
  now: E arms the sim's own eat chain; outcome toasts (commit `abd311a`).
- **fixed: climb keys silently eaten.** was: Up/Down always scrubbed the CAMERA z, and
  the follow-cam snapped it back — the played soul could not climb the new stairs.
  now: in play mode Up/Down are climb keys; camera z-scrub gated off (commit `93f5ce0`).
- **fixed: the talk panel was a bark viewer.** was: T = one frozen greet, no
  player-side choice; 56 authored story tables unreachable. now: numbered topic rows,
  1-9/0 ask keys (commit `679faaf`).

Unrecoverable: the ranking beyond the entries above (the "top-5" phrasing implies more
were graded). Lost with the S4 session; declared, not silently dropped.
