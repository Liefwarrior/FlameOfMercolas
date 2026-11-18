# Phase 4 (world geometry and materials): before and after

Same scenario, viewport (1280x720), clock and state in both sets. Only the world's geometry and materials differ.

| | Revision | World |
|---|---|---|
| `before/` | `main` at `13a2137b` (after phase 3) | tile boxes, each face wearing one 16-pixel tile stretched over it; flat roof decks; ramps and stairs as thin slabs; the district drawn to 64 tiles |
| `after/` | this PR | procedural two-metre surfaces on world-metre UVs, hipped roofs, wedge ramps and stepped stairs, trim, door posts, beams, quay detail, a far level of detail out to 160 tiles, the skyline ring |

Both taken on Linux (WSL Ubuntu), headless on Mesa llvmpipe (`llvmpipe (LLVM 21.1.8, 256 bits)`), with the stock checkout: no licensed art, so the Synty building kit draws nothing in either set. Each capture is the same command line on both builds:

```
granadad --smoke=0 --hold --time=8                                     01-street-morning
granadad --smoke=0 --hold --time=19                                    02-street-dusk
granadad --smoke=0 --hold --time=22                                    03-street-night
granadad --smoke=0 --hold --threshold=gull --time=20                   04-taproom
granadad --smoke=0 --hold --threshold=piers --time=9                   05-piers
granadad --smoke=0 --hold --time=8 --pitch=45                          06-look-up
granadad --smoke=0 --hold --time=8 --pitch=-45                         07-look-down
granadad --smoke=0 --hold --time=10 --spawn=140,55,19 --yaw=170       08-eaves-and-chimneys
granadad --smoke=0 --hold --time=10 --spawn=120,50,19 --yaw=180 --pitch=8   09-roofs-from-the-quay
```

`01`-`07` are the plan's standard set; `08` and `09` look at roofs, which the street set barely shows.

`surfaces-tiled-2x2.png` is every surface repeated two by two, so a seam would show (there is none: every period in the generator divides the surface). The single images and a contact sheet are in `content/art/oblivion/materials/`.

The `scene` hash printed by each capture differs between the two builds on purpose.
