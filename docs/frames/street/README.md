# Street senses, photographed

The street used to be a diorama. Swing on the Tarwalk and nobody moved, because no street
body had eyes and nobody's Safety was ever written, and the swing itself walked the Gull's
seventeen and nobody else. Two of the three legs are in now. Raise a blade on the quay and the
crowd gives you room. Serfs run, shopkeepers and priests stand their ground and watch you, and
it settles a minute later when you put it away. Throw the punch and it lands. A docker goes
down, lies his six seconds, gets up bloodied and runs. A sailor hits you back. Kill a man in
front of the fish market and the ward counts who saw it, and the priest reads that count off
the paper.

Every frame here is the real `dist\granadad.exe` on the gated Windows build, playing the real
verb. No harness. `--street-assault` stands you in the fullest stretch of the Tarwalk at
four in the afternoon. With no ending it puts steel in your hand and your hands up and throws
no blow, and the settle window does the scattering, so `--settle-steps=N` catches the panic N
sim-steps in. With an ending it swings, through the same Attack key you press.
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
| `street-blow-1280x720.png` | `--street-assault=blow --settle-steps=0` | Leg (b). FISTS UP, one tap, and it lands on a docker a tile in front of you. The same swing that hits a Gull patron, the same one roll, aimed down the same ray at the other roster. The crowd around him turns. |
| `street-down-1280x720.png` | `--street-assault=down --settle-steps=0` | Taps until he drops. QUENNA CREELMAN GOES DOWN. on the row, the man flat on the cobbles where he fell, the street breaking away from the pair of you. Nobody dies. It was fists. |
| `street-up-1280x720.png` | `--street-assault=up --settle-steps=0` | Six seconds later he is on his feet at a quarter of his sheet, bloodied, and running. The floor is the Gull's own floor rule on a street body. |
| `street-kill-1280x720.png` | `--street-assault=kill --settle-steps=0` | Steel, meant. Hard swings until he is a corpse on the quay. WANTED FOR BLOOD on the HUD, the ward counting who saw it. `ward=655` in the summary line, one fewer standing. |
| `street-hearing-1280x720.png` | `--street-assault=hearing` | The killing, then the Gull at ten with the blade still out. Cull takes you at reach, TAKEN TO THE MISSION, and the paper reads THE WARD SAYS YOU PUT QUENNA CREELMAN DOWN IN THE STREET. 24 SAW IT. The street's own count, weighed against mercy, THE ROPE, THE DROP offered. |

## The numbers

One consented move of the population baseline, re-blessed once per leg.

| leg | baseline | before | after | how |
| --- | --- | --- | --- | --- |
| (a) | population | `0x2646C1AAA2BA38DF` | `0xF493AF6F939D52D3` | `granadad-twin-gate --population --population-hour 16 --ticks 7200`, run A == run B, twice, 19,947-byte report |
| (b) | population | `0xF493AF6F939D52D3` | `0xED0CA90E26DB0F5B` | the same line, run A == run B, twice, 21,746-byte report. The docker the gate strikes goes down and gets up on the compared report |
| (a),(b) | tavern | `0x837E94019BC49C25` | `0x837E94019BC49C25` | unmoved both legs, `--tavern --ticks 900`, run A == run B, twice. No hashed tavern field touched |
| (a),(b) | nemesis | 7/7 | 7/7 | `granadad.exe --nemesis` reads `beats=7/7 mask=127`, Tarn Wrenhale still rising |

Gated trees. Leg (a) `native/` digest `dc56c116a6865b70ca6b4adedb9c5a3ce7ea3d0eb5d20266bbd939b3841af8ca`,
ctest 1249. Leg (b) `native/` digest `8d54a6a95cc3f42d96855f4496e784c7ce7d09b3197196bbda560d2992ab2b0c`,
ctest 1256. Both `verify-windows.ps1` PASS, both fingerprint reports byte-identical linux/gcc
vs mingw/windows.

## What moved, plainly

Leg (a). `WardPopulation` learned where you are (`setPlayer`, hashed, the mirror of the
tavern's own). Flee stopped being a random shuffle and became a step straight away from you. A
new Cower policy sits on the same fright gate for the types the gazetteer says stand rather
than run. The loiter shuffle stayed random on purpose, so people standing near you do not back
off just for being looked at. And the gate grew a scripted blow on the Tarwalk so the number
proves the behaviour instead of comparing a street nobody ever startled to itself.

Leg (b). Every street body carries the Gull's sheet now, twenty-four points, hashed. The
street's ray is the Gull's ray, the same integer projection, and the swing goes to whichever
body is nearer along it. The blow is the room's own swing, same head, same one roll, same
strike, same classifier, so a docker and a patron take a fist identically. Down under fists is
six seconds on the floor and up at a quarter. Down under steel is dead, and a killing seen is
murder with the count taken before the body dropped. A struck serf runs. A struck sailor or
thief comes at you and swings, one draw a swing on his own key, and it lands on your sheet
through the Gull's own guard and floor rules. The reading names him and says where.

## New copy for your redline

None yet. The panic and the blow are behaviour, not speech. No bark rows were authored and
`barks.json` was not touched. The street halt and the routed docker's line come with leg (c)
in their own new file, in the salted city-folk register. The row literals you already blessed
are reused as they are: `<NAME> GOES DOWN.`, `<NAME> DIES ON THE BOARDS.`, `FISTS UP`,
`STEEL UP`, `GUARD UP`, `WANTED FOR BLOOD`, `THE WARD SAYS YOU PUT <NAME> DOWN IN THE STREET.`
is the one new sentence, and it is the Gull's own with the place changed.

## Not here yet

Leg (c), the watch. The thirteen watchmen on the beats get eyes, close on a seen kill with a
halt line, and arrest at reach through the same hearing Cull uses. Its frames (a watchman
closing with his line, the arrest at reach on the street, TAKEN TO THE MISSION from the quay)
land with it. Named ceiling on (b): a street winner's nemesis rise, beasts on the street's ray,
a guard and a wind-up on a street body.
