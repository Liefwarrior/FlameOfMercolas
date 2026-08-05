# #78 — the ward, at four hours

Visual **evidence**, and deliberately not a gate. The owner ruled that out
himself: "a frame must contain actors" is not an acceptance criterion, because a
cellar or a back lane at four in the morning is legitimately empty and a build
that went red over that would be lying about what it had proved.

So what is asserted lives in `native/tests/test_ward_actors.cpp` — the roll, the
per-type counts, the behavioural bars, and a small set of expectations each
naming a place, an hour and a reason. What is here is four pictures of the same
street at four hours, for a human to judge.

All four are the same camera: standing on the **Tarwalk** at world tile
(140, 64, z19), looking west along the working spine. Reproduce any of them with

```
.\dist\granadad.exe --smoke=30 --hold --width=960 --height=540 --scale=1 \
    --time=HH --spawn=140,64,19 --yaw=270 --screenshot=out.png
```

| Frame | Hour | Ward roll | Drawn in this frame | Within 12 tiles | In the Gull |
|---|---|---|---|---|---|
| `p78-tarwalk-0200.png` | 02:00 | 661 | 11 | 18 | 3 |
| `p78-tarwalk-0800.png` | 08:00 | 661 | 16 | 17 | 1 |
| `p78-tarwalk-1400.png` | 14:00 | 661 | 22 | 23 | 3 |
| `p78-tarwalk-2000.png` | 20:00 | 661 | 17 | 24 | 11 |

**What to look at in each.** At 08:00 and 14:00 the Tarwalk is a working quay
and the figure in the foreground is a labourer at a muster. At 20:00 it is a
different person in a different coat — the day trades have gone home and the
evening's are out — and the Eel-Pots are lit down the road. At 02:00 the street
is genuinely dark: eleven bodies are drawn in that frame and you can barely see
any of them, because the ward is committed-dark and the only honest light after
midnight is the night stalls'. That is the correct reading and not a defect.

The last column is the Gilded Gull's own `presentCount()` at that hour, which
belongs to the Tavern and not to the population. Eleven at eight in the evening
is the Gull filling up.

## And the one that is the whole point

`p78-ropewynd-0200.png` — the **Ropewynd** at two in the morning, looking east
from (110, 97, z19), `ward=661 seen=7 near=8`.

Two figures standing in a lamp pool in an otherwise black street. The Ropewynd
is the road every compound gate opens onto and it is where three of the night
seven walk; the light is the compound's own. This is the frame the complaint
asked for — *"even at night there should be people like guards urchins thieves
taverns"* — and it is also the frame that shows why the 02:00 Tarwalk capture
looks empty at a glance and is not. Seven bodies are drawn in it. You can see
two of them, because they are the two standing under a lamp.

```
.\dist\granadad.exe --smoke=30 --hold --width=960 --height=540 --scale=1 \
    --time=2 --spawn=110,97,19 --yaw=90 --screenshot=out.png
```

The three numbers are printed by the capture itself, on the `granadad:` line:

* **`ward=`** how many bodies the district holds, alive, right now.
* **`seen=`** how many of them put at least one pixel on this frame. It is
  counted in the renderer, when a billboard actually writes — not when one is
  submitted, because a figure standing behind a warehouse is submitted, sorted,
  projected and drawn nowhere.
* **`near=`** how many stand within twelve tiles of the camera whether or not it
  is pointed at them.

The three differing is the normal case and is the whole reason all three are
printed. A street with forty people on it and a warehouse in the way is a street
with forty people on it.
