# Phase 5 (lighting and atmosphere): before and after

Same scenario, viewport (1280x720), clock and state in both sets. Only the light differs.

| | Revision | Light |
|---|---|---|
| `before/` | `main` at `4afed672` (after phase 4) | light baked into vertex colour: a flat ambient by the day curve, a lamp's pool painted on each face, fog only as banded veil shells in bad weather, a vertex-coloured dome |
| `after/` | this PR | per-pixel point lights (the lamps and the lit windows), a sun and a moon from the clock, a shadow map, the sky shader, distance fog, the water shader, bloom and a grade |

Both taken on Linux (WSL Ubuntu), headless on Mesa llvmpipe (`llvmpipe (LLVM 21.1.8, 256 bits)`), with the stock checkout: no licensed art, so the Synty building kit (its lanterns, windows and halos) draws nothing in either set. The lamps still light the street in `after/`: a light does not need its lantern model. Every capture is the same command line on both builds:

```
granadad --smoke=0 --hold --width=640 --height=360 --scale=2 --time=8            01-street-morning
granadad ... --time=19                                                          02-street-dusk
granadad ... --time=22                                                          03-street-night
granadad ... --threshold=gull --time=20                                         04-taproom
granadad ... --threshold=piers --time=9                                         05-piers
granadad ... --time=8 --pitch=45                                                06-look-up
granadad ... --time=8 --pitch=-45                                               07-look-down
granadad ... --threshold=piers --time=22                                        08-piers-night
granadad ... --time=19 --yaw=100                                                09-dusk-towards-sea
```

`01`-`03` are the issue's morning, dusk and night; `04`-`07` are the plan's standard set; `08` and `09` look at the water and the sea horizon, which the street set barely shows.

The `scene` hash printed by each capture differs between the two builds on purpose.
