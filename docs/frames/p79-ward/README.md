# #79 — the opening shot, and the ward you can talk to

Two things the owner asked for by looking at the game rather than at a number.

Every frame here was **looked at** before it was committed, and every one of them
was taken with the shipped `dist\granadad.exe` from the green build
`p79-e-1785946137`.

---

## A — the opening shot

**The complaint.** *"There were no people."* The population was 661 and correct;
the first frame was wrong. Measured off the shipped `.exe` at the S2 spawn
(152, 60, z19) facing 165, at 960x540:

| hour | `ward=` | `near=` | `seen=` | sky px |
|---|---|---|---|---|
| 08:00 | 661 | 7 | **0** | 6,712 |
| 20:00 | 661 | 14 | **3** | 6,712 |

Seven people within twelve tiles and none of them on screen. Walk the thirty
steps the capture harness walks and it seals completely — `sky px=0`, `seen=0`.
The spawn stood in a slot between two warehouse shoulders.

**The re-aim, measured.** 400 stands across the Tarwalk either side of the
Gilded Gull, four hours each, ranked by how much of the ward the frame actually
**draws** — then judged by eye. `docks::kSpawn` is now **(156, 63, z19) facing
265°**, and it is the only stand that carries all three things an opening shot
of this district owes:

* the **Tarwalk** open for sixty tiles with the ward's own traffic on it,
* the **Gilded Gull's** frontage on the left with its **door lamp** about 20°
  left of centre — the brightest thing in the 02:00 frame,
* the **harbour** and the finger piers readable on the right.

22% sky, and walking forward walks you down the street instead of into masonry.

### The four frames

**Taken from the true default spawn — no `--spawn`, no `--yaw`.** That is the
whole point: #78's frames came from a hand-picked camera, which is exactly why
they disagreed with what the owner saw.

```
.\dist\granadad.exe --smoke=30 --hold --width=960 --height=540 --scale=1 \
    --time=HH --screenshot=out.png
```

| frame | hour | `ward=` | `seen=` | `near=` | `seen=` at the old spawn |
|---|---|---|---|---|---|
| `p79-opening-0200.png` | 02:00 | 661 | **20** | 9 | 3 |
| `p79-opening-0800.png` | 08:00 | 661 | **18** | 7 | 3 |
| `p79-opening-1400.png` | 14:00 | 661 | **28** | 11 | 1 |
| `p79-opening-2000.png` | 20:00 | 661 | **30** | 13 | 3 |

**What to look at.** At **02:00** the Gull's door lamp is burning, a watchman of
the night seven stands in the foreground with his spear, and two figures cross
the lamp pool with a cat while the Eel-Pots glow further down the road — the
street is committed-dark and the light is the honest amount of it. At **08:00**
and **14:00** it is a working quay in daylight: a labourer and a cat in the near
field, more figures down the street, and the harbour with its piers on the
right. At **20:00** the day trades have gone home and the evening's are out.

### And `actors=` is `gull=`

The diagnostic printed `actors=1` one space from `ward=661`, which reads as *one
of the six hundred is on screen*. It is neither: it counts how many of the
Gilded Gull's own seventeen are inside K03, a different population in a
different building, and it never had anything to do with the frame. The HUD has
always called it `THE GULL n IN`; so does this now.

---

## B — the ward can be spoken to

`E` reached fourteen people, all inside one building. It reaches all 661 now,
and **none of the words are new** — nine job families x six attitudes x four
time bands were already authored in `content/raws/barks/barks.json` and this
build had no way to ask for them.

```
.\dist\granadad.exe --smoke=1 --hold --width=960 --height=540 --scale=1 \
    --time=13 --street=WHO --screenshot=out.png
```

`WHO` is `hand`, `watch`, `priest`, `disciple`, `keeper`, `fisher`, `sailor`,
`carter`, `wastrel`, `urchin`, `thief`, `drover`, `cat` or `dog`.

| frame | who answered | authored key | what they said |
|---|---|---|---|
| `p79-talk-hand.png` | Lisbet Hempson, the Whistling | `greet.serf.neutral.day` | NO IDLING - THE CRANE LINE'S MOVING. |
| `p79-talk-watch.png` | Evrard Ropewynd, the Regular | `greet.watch.neutral.day` | MIDDAY POST. ALL QUIET, AND I INTEND IT STAYS SO. |
| `p79-talk-priest.png` | **Father Maell**, of the Mission | `greet.clergy.neutral.day` | THE MISSION DOOR STANDS OPEN, AS EVER. |
| `p79-talk-keeper.png` | **Pitch-Master Ulwer**, of Pitchfield | `greet.trade.neutral.day` | MIDDAY PRESS - QUEUE LIKE DECENT FOLK. |
| `p79-talk-wastrel.png` | Hasp, the Unseen | `greet.wastrel.neutral.day` | WATCH MOVES US ALONG AT NOON. KEEP SHUFFLING. |
| `p79-talk-fisher.png` | Ebba Tarwalk, the Elder | `greet.maritime.neutral.day` | THE CRANE WAITS FOR NO CONVERSATION. |
| `p79-talk-cat.png` | Cinder | `greet.beast.neutral.day` | (IT DOZES IN A STRIP OF SUN, ONE EYE ON THE TRAFFIC) |

**Six trades, six tables, six sentences, and not one of them written for this
task.** Read the wastrel's and the watchman's together: Hasp is being moved
along at noon and there is a watchman standing behind him in the same frame.

The **fourth column of each list is their trade**, out of the authored
`mastery.<skill>` tables: `FIELDCRAFT` for the rope-hand, `KIT-KEEPING` for the
watchman, `CHANNELING` for the priest, `FISHING` for the line-fisher,
`STREETWISE` for the wastrel. Nobody borrows anybody else's.

**The names.** Two of those seven are the owner's own: `notables.json` binds
Father Maell to `MISSION_BUNKS` and Pitch-Master Ulwer to `K09_PITCHFIELD`, and
`ward_roster.cpp` has been claiming a keeper for both of those sites since #78 —
the two facts had simply never been introduced. Twenty-nine of the Forty are out
there now, with their own `personal.<id>` tables and their own micro-histories:
Maell's topic list carries `2 SETHRA AND MAELL`, which is an authored feud
between two people who both live in this district.

Everybody else is named out of `names.json`. **A surname is a thing you have if
the ward keeps track of you** — Lisbet Hempson and Evrard Ropewynd carry two
names, Hasp carries one and a by-name, and that is how the owner's own wastrel
pool is written (Sniv, Tatter, Moll, Grib). The cat is called Cinder, out of the
twenty-name kennel pool. The mice are not named at all.

### The hour is audible

| frame | hour | authored key | what he said |
|---|---|---|---|
| `p79-talk-watch.png` | 13:00 | `greet.watch.neutral.day` | MIDDAY POST. ALL QUIET, AND I INTEND IT STAYS SO. |
| `p79-talk-watch-0300.png` | 03:00 | `greet.watch.neutral.**night**` | CURFEW'S NOT LAW HERE YET. DON'T MAKE ME WISH IT WERE. |

The 03:00 frame is nearly black, and that is the ward reading correctly rather
than a defect: he is at the guardhouse, the district is committed-dark, and the
only honest light after midnight is a lamp.

### And a topic

`p79-talk-hand-ward.png` — the same dockhand, asked `2 THE WARD`:
*"YOU HEAR THINGS ON A QUAY. WIND, MOSTLY. MOSTLY."*

### What a beast gets

`p79-talk-cat.png` — Cinder answers out of `greet.beast`, and the topic list is
**one row: `1 LEAVE IT BE`**. A cat with an opinion about the harbourmaster's
ledger, or a purse to pick, is a menu built by a machine that was not looking at
what it was talking to.

---

## One thing worth knowing before you read the topic grid

`3 THE VANISHED.` and `6 PICK THEIR POCK.` are the topic column doing what it
has done since S6: eighteen glyphs, a cut, and a mark to say it was cut. The row
the cursor is on gets a full-width line of its own above the grid for exactly
this reason. It is **not** new here and nothing in #79 changed it — but it is on
screen in these frames and is worth a look if the truncation is now judged worse
than the column.
