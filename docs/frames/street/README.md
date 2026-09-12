# Street senses, photographed

The street used to be a diorama. Swing on the Tarwalk and nobody moved, because no street
body had eyes and nobody's Safety was ever written. This is leg (a) of fixing that. Raise a
blade on the quay now and the crowd gives you room. Serfs run, shopkeepers and priests stand
their ground and watch you, and the whole thing settles a minute later when you put it away.

Every frame here is the real `dist\granadad.exe` on the gated Windows build, playing the real
verb. No harness. `--street-assault` stands you in the fullest stretch of the Tarwalk at
four in the afternoon, puts steel in your hand and your hands up, and throws no blow. The
settle window does the scattering, so `--settle-steps=N` catches the panic N sim-steps in.
Reshoot the set with

    powershell -ExecutionPolicy Bypass -File .\scripts\shoot-street.ps1

Every line below is `granadad.exe --smoke=0 --hold --width=1280 --height=720 --scale=1 <the
line> --screenshot=docs\frames\street\<frame>`. The script keeps each run's summary line in
`shoot-street.log` beside the frames, and a run that fell short cannot pass as one that did.

## The frames

| frame | line | what you see |
| --- | --- | --- |
| `street-panic-0-1280x720.png` | `--street-assault --settle-steps=0` | The blade just up on the Tarwalk, the crowd still there. STEEL UP and GUARD UP on the stance rows, a serf named on the crosshair, eighteen bodies in sight. Nobody has moved yet. |
| `street-panic-180-1280x720.png` | `--street-assault --settle-steps=180` | Three seconds in. The crowd has turned and is breaking away from the blade, spreading down the quay. |
| `street-panic-360-1280x720.png` | `--street-assault --settle-steps=360` | Six seconds in. Backs turned, fanned out, the street opened up in front of you. E - LOWER HANDS on the crosshair now. Put it away and a minute later they are back at work. |

## The numbers

One declared move of the population baseline, re-blessed for the leg.

| baseline | before | after | how |
| --- | --- | --- | --- |
| population | `0x2646C1AAA2BA38DF` | `0xF493AF6F939D52D3` | `granadad-twin-gate --population --population-hour 16 --ticks 7200`, run A == run B, twice, 19,947-byte report |
| tavern | `0x837E94019BC49C25` | `0x837E94019BC49C25` | unmoved, `--tavern --ticks 900`, run A == run B, twice. No hashed tavern field touched |
| nemesis | 7/7 | 7/7 | `granadad.exe --nemesis` reads `beats=7/7 mask=127`, Tarn Wrenhale still rising |

Gated tree `native/` digest `dc56c116a6865b70ca6b4adedb9c5a3ce7ea3d0eb5d20266bbd939b3841af8ca`,
ctest 1249 cases, `verify-windows.ps1` PASS, both fingerprint reports byte-identical linux/gcc
vs mingw/windows.

## What moved, plainly

`WardPopulation` learned where you are (`setPlayer`, hashed, the mirror of the tavern's own).
Flee stopped being a random shuffle and became a step straight away from you. A new Cower
policy sits on the same fright gate for the types the gazetteer says stand rather than run.
The loiter shuffle stayed random on purpose, so people standing near you do not back off just
for being looked at. And the gate grew a scripted blow on the Tarwalk so the number proves the
behaviour instead of comparing a street nobody ever startled to itself.

## New copy for your redline

None this leg. The panic is behaviour, not speech. No bark rows were authored and `barks.json`
was not touched. The bark rows the street will want, a routed docker's line and a street halt,
come with legs (b) and (c) in their own new file, in the salted city-folk register.

## Not here yet

Leg (a) is panic only. The rest of the crowd ladder needs the next two legs, declared and
re-blessed the same way on this branch.

- (b) street bodies. A blow lands on a docker, he goes down and gets up, a street kill is
  murder with the witnesses counted. Needs a WardActor combat sheet and the player swing to
  see `people_`.
- (c) the watch. The thirteen watchmen on the beats get eyes, close on a seen kill, and arrest
  at reach through the same hearing the Gull's Cull uses.

Those frames (a struck docker down and up, a watchman closing with his halt line, the arrest,
a kill's witness count on the charge sheet) land with their legs.
