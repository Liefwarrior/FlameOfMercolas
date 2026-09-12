# The PULL pack, on the street

Branch `lane/pull`. Every frame here came off the gated `dist\granadad.exe`
(docker gate green, `verify-windows.ps1` PASS, both baselines unmoved) on the
Windows host, through the shipped shutter. The drives are the game's own
verbs. The trail walk is real collision, FOLLOW is the page's own F key,
the block is the guard held through a real fight, the leap is the leap.
Nothing here is staged and nothing reaches into the sim sideways.

960x540 is `--width=960 --height=540 --scale=1`. The two strip pins are the
same drive at `--width=320 --height=180` and `--width=1920 --height=1080`.

## What landed

- The street says where next. One line under the compass ribbon reads the
  ONE lead you chose to FOLLOW, bearing and paces, the place in the sign's
  own words. `NW 53  THE WEIGHHOUSE`. The paces run down as you walk. It is
  the same arithmetic the casebook page prints, out of one function, so the
  book and the street never disagree.
- Ticks on the ribbon for discovered named places. Bone for a place you have
  stood in or a heard lead has named, amber and wider for the followed
  lead's own site, so you can line the fixed mark up on it without reading.
- FOLLOW on the casebook page, F on a keyboard, X on a pad. Press it again to
  let the lead go. The default is the authored next lead, so a returning
  player is still told where they were going. A lead you have stood over
  lapses on its own.
- A CASES shelf, the third view of the book. Three books and every started
  questline, where each stands, READ IT to front one. This is the switcher
  the code flagged as missing twice.
- The skill-up toast. `SHIELDWALL RISES TO 1` in the free corner, mid-fight,
  no pause. `SkillTrack::use()` has returned `levelled` since S17 and every
  caller threw it away. The render side diffs the levels once a step now.
- Every book change is felt. The two silent hears (TAKE HIM UP, the served
  writ), a questline stage, an errand taken, all land on the one plate with
  one cue, `SoundId::CaseNews`. Sites that already announce themselves are
  never said twice.
- The honest sheet. Skill rows come off the track itself, any skill the
  world has moved prints, with what the next level costs off
  `scaledUsesForLevel`. MGT AGI VIG WIT with the held delta beside them.

## The doctrine

Owner ruling D8, kept by construction. The line names a PLACE, never the
witness and never the clue. The ticks are bearings on a strip of sky, not an
arrow in the world, and nothing is drawn through a wall. Nothing is drawn on
the map plan. It is a bearing, not a route. The Docks is still one lap on
foot and the signs still have to be read.

## Outside every hash

`followedLead_`, the fronted case, the toast and the ticks are render state.
`test_pull.cpp` runs two sessions through the same sim acts, follows a lead
in one, fronts a case, walks the shelf and draws frames, and compares the
engine's combined hash, the tavern's own section and the three books. Byte
identical at every checkpoint. The gate agrees.

| baseline | number |
|---|---|
| population, `--population --population-hour 16 --ticks 7200`, twice | `0x2646C1AAA2BA38DF` |
| tavern twin, `--tavern --ticks 900`, twice | `0x837E94019BC49C25` |
| `--smoke=40 --time=20 --screenshot` scene | `0xA47D544D1681F761` |

## The frames

| frame | drive | what it proves |
|---|---|---|
| `ribbon-follow-960.png` | `--trail=mission --follow=weighhouse-ledger --follow-end=street --time=9` | The trail walks to the Mission and reads the body, three leads open, the page follows the ledger and goes down. The ribbon reads `NW 53  THE WEIGHHOUSE`, the amber notch sits under NW, the row says `THE RIBBON FOLLOWS THE WEIGHHOUSE.` |
| `ribbon-follow-walk-960.png` | the same, `--follow-walk=150` | A hundred and fifty steps of real walking later. `NW 47  THE WEIGHHOUSE  BAND 19`. The paces came down and the walk carried the body up a level, so the line says which plane the lead is on. |
| `ribbon-weighhouse-960.png` | `--trail=weighhouse --follow=harls-yard --follow-end=street --time=10` | Four leads read, standing at the Weighhouse counter following Harl's Yard. `E 73  HARL'S YARD`, bone notches for the King's Bond and the Outfall on the ribbon, the amber one at the east rail. |
| `ribbon-follow-320.png` | the first drive at `--width=320 --height=180` | The strip pin. The line fits whole under the ribbon at the smallest window the game runs at. |
| `ribbon-follow-1920.png` | the first drive at `--width=1920 --height=1080` | The other pin. Same line, same place, the biggest window. |
| `follow-taken-960.png` | `--trail=mission --follow=weighhouse-ledger --time=9` | FOLLOW taken, the book still up. `4 THE LEDGER  FOLLOWING` on the list, `FOLLOWING` on the badge, `SHOW IT (NW 53)` at the foot, the same NW 53 the ribbon carries. The flagstones row went back to naming its place. |
| `shelf-960.png` | `--trail=mission --case-tab=cases --time=9` | The CASES shelf. `THE BLOODLETTER  READING`, the courier's and the eviction's books `NOT YET`, `1/3 IN HAND`, the facts under it, `READ IT (THIS ONE)` at the foot. The nav band at rest is keycaps only, F among them. |
| `toast-fight-960.png` | `--spawn=158,68,19 --block=5 --time=20 --settle-steps=40` | The guard held through five softened blows in the Gull, which is what shieldwall's first level costs. `SHIELDWALL RISES TO 1` top left, `FISTS UP` and `GUARD UP` under it, the bouncer's warning on the row. In situ, nothing paused. |
| `toast-climb-960.png` | `--roofs=leap --time=10 --settle-steps=20` | Up the stair, over the north wall, across the alley. The leap's landing is the third use of skyrunning and the toast reads `SKYRUNNING RISES TO 1` over the lead. The ribbon reads `S 43  MISSION OF THE FLAME  BAND 19` from up there. |
| `plate-take-960.png` | `--case=taken --settle-steps=30` | TAKE HIM UP. The courier case hears its close lead with no plate of its own. The watcher says `1 NEW LEAD - J` on the plate, `CARRYING FINCH` in the corner. |
| `plate-stage-960.png` | `--flame=away --settle-steps=40 --time=20` | The Priest of the Flame line played to the end. `THE DISCIPLE'S OATH IS DONE - J` on the plate, and `LINKCRAFT RISES TO 3` in the corner from the forge, the two corners never fighting. |
| `sheet-960.png` | `--roofs=leap --character --time=10` | The honest sheet after the roofs. `SKYRUNNING LV 1  NEXT 3`, the three other always-shown skills with their own NEXT, `MGT 40 AGI 40 VIG 40 WIT 40` under them, then the ladders. |

## Not in this folder

- A pad frame of the book. The virtual-pad windowed drive did not get a
  character past the creation screen on this box. The pad wording is
  pinned instead. `test_pull.cpp` switches the session to the pad and reads
  `X` for FOLLOW and `A` for the commit off the page state, and draws both.
- A frame of a punch levelling OPEN HAND. The strike does not train a skill
  in this tree yet. That is roadmap item 12, a sim change in the SIM-FIGHT
  lane, not this one. The toast fires on any level the track reports, so it
  will read `OPEN HAND RISES TO 6` the day that lands. Mid-fight it is
  proven on shieldwall, the one fight verb that trains today.
- The fight frame's vantage. The `--punch` and `--block` drives walk up to
  Tarn at his table and end facing a plaster wall at that spot in the 3D
  build, on this branch's tip as much as on this lane. The frame here spawns
  a few tiles east so the fight is on camera.
