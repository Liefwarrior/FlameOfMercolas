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
