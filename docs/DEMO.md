# THE SCRIPTED DEMO

A curated route through the ward that plays itself, unattended, deterministically,
and ends on a card instead of dumping the player on a street corner.

## Run it

```
.\dist\granadad.exe --demo
```

Two minutes, start to finish, including the character screen. Press **ESC** or
close the window to stop it early; nothing else on the keyboard, mouse or pad
reaches the game while it is running.

To get the frames as well as the show:

```
.\dist\granadad.exe --demo-capture=docs\frames\demo
```

Twenty-two or twenty-three PNGs at the render size (640x360 by default), one per
named shot on the route. A trailer or a screenshot set falls out of the same run
that a person watches.

**`creation-origin.png` is captured by a race and is often missed.** The
character screen's shutter arms 40 frames after a step is entered, and the
route can leave the Origin step before those 40 frames are up, so the first
shot of the set is written on some runs and not others -- the SHIP pass got it
on three runs out of seven, from the same binary both ways. Only the shutter is
affected: `--demo` itself plays that beat every time, and the committed
`docs/frames/demo/creation-origin.png` is a real frame of it. The durable fix is
the guard the world's own director already has -- take the picture when the beat
ends if it has not been taken yet.

To iterate on one section without sitting through the whole thing:

```
.\dist\granadad.exe --demo=case
```

Sections, in order: `quay`, `saltgate`, `case`, `map`, `night`, `end`. The
character screen always runs first, because the character has to exist.

`--width`/`--height`/`--scale` work as they do everywhere else. **Judge it at
640x360**, which is what the shots in `docs/frames/demo/` are.

## The route

| # | section | what it shows |
|---|---|---|
| 1 | *(the character screen)* | the quiz door, three questions answered slowly so the four meters can be seen moving, the rest at a reading pace, the verdict, the sheet, BEGIN |
| 2 | `quay` | the authored spawn on the Tarwalk; then out on the piers, where the Mission's lantern-turret stands clear over the roofline |
| 3 | `saltgate` | the gate-house from nineteen tiles downhill, then fifteen, then ten, walked up the road's own centreline with the backdrop palace dead centre in the opening |
| 4 | `case` | the Mission of the Flame, the back-room clue read on foot; Crell's ledger at the Weighhouse; the casebook; the cursor on Harl's Yard; the lead's own commit verb putting the ward map on it |
| 5 | `map` | the ward map at three zooms, names sitting inside their own building shapes |
| 6 | `night` | eleven at night from the piers — the turret's beacon at the skyline over a black ward — then back into the lamplit Tarwalk |
| 7 | `end` | the end card, and the run closes itself |

## How it works, and what it is not

`native/include/granadad/render/demo.hpp` and `native/src/render/demo.cpp`. The
route is a flat table of beats; the director walks it one beat-frame per rendered
frame and drives the session through the **same public calls a keyboard makes** —
`examine()`, `toggleCasebook()`, `selectCasebookLead()`, `commitCasebookLead()`,
`toggleDistrictMap()`, `skipToHour()`, and a `sim::MoveInput` handed back to the
frame loop to be stepped. Nothing reaches into the simulation sideways.

**Deterministic.** The playhead is counted in FRAMES, never in seconds: the client
runs one simulation step per rendered frame while the demo is up, so demo frame N
is the same picture on a 60 Hz laptop, a 144 Hz desktop and inside a capture. Wall
clock is used for exactly one thing — a self-correcting frame deadline so the route
does not play at double speed — and it cannot change what is drawn.
`native/tests/test_demo.cpp` runs the whole route twice and asserts the two runs
end with the body on the same tile, the clock at the same second and the casebook
holding the same leads.

**Cuts between sections, walks inside them.** The quay, the head of Saltgate Rise,
the Mission and the Weighhouse are forty to ninety tiles apart. Every clue is
walked to, on foot, from the street outside its building, through the real door,
by the district's own breadth-first router — but the links between sections are
cuts, because a route that walked all of them would be twenty minutes of pavement.

**No new content.** Presentation and routing only. Every coordinate in the table is
either a constant the build already owns or a number read off content the ward is
generated from. No map data, no place names, no raws. World hash untouched.

## What is weak

- **The lead-opened plate ("3 NEW LEADS") is not on the captured frames.** It is
  armed — a headless drive of the route counts it live for 179 frames after each
  read — and `--trail=opened` photographs it happily at the same window size. It is
  simply not on the demo's own frames at 0.25 s or 1 s after the read, and the
  cause was not isolated before the deadline. The caption says what opened, so the
  beat still reads; the plate is a bonus it may or may not get.
- **Both clue rooms are dark grey interiors.** True to the ward and dull to look at.
  The establishing shots outside each building (`case-mission.png`,
  `case-weighhouse.png`) are the good frames of that section.
- **The quiz's master pane is largely empty at 640x360.** Known, flagged by the
  polish pass, a layout change and out of this pass's scope.
- **There is no camera motion during a Hold.** The demo cuts, walks and turns; it
  never dollies or drifts on a still. Stills hold dead still.
