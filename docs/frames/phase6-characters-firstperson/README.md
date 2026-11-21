# Phase 6 (characters and the first-person view): before and after

Same scenario, viewport (1280x720), clock and state in both sets.

| | Revision | People | Hands |
|---|---|---|---|
| `before/` | `main` at `4401c32e` (after phase 5), plus the capture-only `--hands=` flag so a weapon can be photographed on both sides (no other change) | one box figure per kind of body, one pose | two box fists, box sleeves and box weapons |
| `after/` | this PR | a figure per person: its own build, skin, hair, garments and headgear, a 15-bone skeleton, idle, walk, talk, fight and fall clips | a hand with knuckles, fingers and a thumb, a cuffed sleeve, a studded club, a dagger, a cutlass |

Both taken on Linux (WSL Ubuntu), headless on Mesa llvmpipe, with the stock checkout: no licensed art, so the Synty rigs draw in neither set. Every capture is the same command line on both builds, `C="--smoke=0 --hold --width=640 --height=360 --scale=2"`:

```
granadad $C --time=12                                         01-busy-street
granadad $C --time=12 --yaw=100                               02-busy-street-east
granadad $C --time=19                                         03-street-dusk
granadad $C --threshold=gull --time=20                        04-taproom
granadad $C --threshold=gull --time=20 --talk                 05-taproom-talk
granadad $C --time=12 --block                                 06-hands-block
granadad $C --time=12 --punch                                 07-hands-punch
granadad $C --time=12 --hands=sword --block                   08-sword-guard
granadad $C --time=12 --hands=club --punch                    09-club-swing
```

`01`-`03` are the busy street at noon, from another side, and at dusk; `04` and `05` the taproom door and a conversation; `06`-`09` the hands, bare and holding a cutlass and a club (`--hands=` is a verification aid that puts a hand model in the hands, nothing in the sim reads it).

`after/10-sheet-clips.png` and `after/11-sheet-looks.png` (`--sheet=clips`, `--sheet=looks`, `--time=12 --nohud`) have no "before": a row of figures in front of the eye, one per animation clip (idle, walk, right punch, left punch, block, talk, flinch, struck down and sitting, slain) and one per kind of body, showing how the look varies. In the clip sheet the walker stands behind the door post at the left.

The `scene` hash printed by each capture differs between the two builds on purpose.
