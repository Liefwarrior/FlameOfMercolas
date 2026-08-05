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

| Frame | Hour | Ward roll | Drawn in this frame | Within 12 tiles |
|---|---|---|---|---|
| `p78-tarwalk-0200.png` | 02:00 | see `ward=` in the capture line | `seen=` | `near=` |
| `p78-tarwalk-0800.png` | 08:00 | | | |
| `p78-tarwalk-1400.png` | 14:00 | | | |
| `p78-tarwalk-2000.png` | 20:00 | | | |

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
