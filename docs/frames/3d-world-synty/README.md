# 3D world -- the Synty building kit on the Docks

Every frame here is `dist\granadad.exe` (the docker gate's mingw build, branch
`3d/build`) shot on the Windows host with a real GPU through the shutter:
`--smoke=40 --screenshot=... <vantage>`. 1280x720, the 640x360 render upscaled
2x, the terminal HUD composited over the 3D world. Nothing is staged: the
placements are a pure function of the tile map and the catalogue
(`content/raws/world3d/docks-pieces.json`), the people are where the sim has
them, the light is the day curve plus the baked lamps.

Re-shoot any of them on this commit with the command in the table. `--hold`
keeps the body on its spawn tile (no walk), `--pitch=DEG` is new in this lane
(look up or down without a mouse), a narrow `--fov` is the zoom.

| frame | vantage | command (after `dist\granadad.exe --smoke=40`) |
|---|---|---|
| 01 | the Tarwalk at street level, from the authored spawn, looking west | `--spawn=156,63,19 --yaw=265 --hold --time=10 --screenshot=01.png` |
| 02 | the same, forty steps down the street (the default drive) | `--time=10 --screenshot=02.png` |
| 03 | the Gilded Gull's door, from the street, looking south | `--spawn=153,61,19 --yaw=180 --pitch=4 --hold --time=10 --screenshot=03.png` |
| 04 | the quay: the wharf edge, the water, the piers | `--spawn=150,59,19 --yaw=300 --pitch=-10 --hold --time=10 --screenshot=04.png` |
| 05 | the rooftop: on the Gull's roof, the game's own `--roofs` drive | `--roofs --time=10 --screenshot=05.png` |
| 06 | overview: the Gull's roof looking north-west, down | `--spawn=153,72,21 --yaw=300 --pitch=-20 --hold --time=10 --screenshot=06.png` |
| 07 | inside the Gilded Gull at eight in the evening | `--threshold=gull --time=20 --hold --screenshot=07.png` |
| 08 | the Long Piers | `--threshold=piers --hold --time=10 --screenshot=08.png` |
| 09 | the Netters' compound gate, looking south into the yard | `--spawn=171,96,19 --yaw=180 --hold --time=10 --screenshot=09.png` |
| 10 | the Tarwalk at nine at night, the door lamp on the Gull | `--spawn=156,63,19 --yaw=265 --hold --time=21 --screenshot=10.png` |
| 11 | the King's Bond (brick) from the Tarwalk, looking east | `--spawn=120,64,19 --yaw=100 --pitch=-8 --hold --time=10 --screenshot=11.png` |
| 12 | the Gull's door zoomed (45 degree field) | `--spawn=153,58,19 --yaw=180 --pitch=-6 --fov=45 --hold --time=10 --screenshot=12.png` |

## What is placed

The chunk mesh stays (it is the collision-true building and the placeholder
every test draws); the pieces are a skin over it, placed by
`native/src/render3d/static_pieces.cpp` from the tile grid:

- **walls** -- every exposed face of a WALL cell, grouped into runs, the kit's
  2.5 m brick wall stretched to whole-tile runs (`SM_Bld_Base_Wall_01`);
  brick out of doors on brick buildings, the kit's plaster side (tinted) on
  rendered stone buildings (granite, Reman concrete) and on every indoor
  face; the plank quad (`SM_Bld_Base_Floor_01` stood on edge) on timber
  (oak, trudgeon: hulls, sheds, the storey over a stone ground floor); the
  plaster side tinted as canvas on cloth and leather.
- **corners** -- a brick wall tile with exactly two exposed adjacent faces
  gets `SM_Bld_Base_Wall_Corner_01`; other convex ends extend by the wall's
  thickness so the two faces' pieces close the corner.
- **windows** -- one outdoor masonry piece in three, by tile hash
  (`SM_Bld_Base_Wall_Window_01`, its glass drawn dark).
- **doors** -- a two- or three-tile gap in a wall line, roofed on one side
  and open on the other, with the wall running on past both jambs:
  `SM_Bld_Base_Wall_Door_Double_01` in the facade plane, fitted to the gap,
  the reveals behind it.
- **cornices** -- `SM_Bld_Base_Wall_Trim_01` along every outdoor masonry face
  with sky above it; the wall heads capped in the wall's colour.
- **floors** -- cobbles (`SM_Env_Path_Cobble_01`, 2x2 blocks) on brick and
  granite streets over a flat fill, planks (any rectangle up to 4x4) on oak
  and trudgeon, the flat fill (the kit's plaster ceiling laid face up,
  tinted) on dirt; boards on the thatch roof planes.
- **ceilings** -- the plaster quad under every floor slab that has a room,
  a street or the water beneath it, tinted per the floor's material.
- **water** -- `SM_Generic_Water_Plane_01` (Dungeon Realms) over the
  harbour's surface in rectangles up to 10x10.
- **props** -- one floor cell in eleven that stands against exactly one wall
  gets a barrel, a crate or a sack, pushed to the wall, by tile hash.
- **lamps** -- every baked lamp: a fire is a brazier, a lantern beside a wall
  (or on a doorstep, beside the door) is a wall lamp on that wall, a lantern
  in the open is a lamp post; all drawn as their own light.
- **chimneys** -- one roof-edge cell over a wall in forty-three.

Scene hash on this build: `--smoke=40 --time=20` twice as separate processes
gives `0x739643261DFF3337` both times with byte-identical PNGs.

## Honest gaps

- Roofs are flat. The sim's roof planes are walkable (the Skyrunners run
  them), so a pitched kit roof would be a picture of a wall the body walks
  through; the planes wear boards, a parapet and a chimney or two instead.
- Props are render-only: the sim knows nothing of a barrel and a body walks
  through it.
- Windows look into the chunk face behind them (the glass is drawn dark for
  that reason); there is no interior behind a window that the sim does not
  already have.
- The kit has no ship: a moored hull is its oak walls in the plank quad, a
  brown boarded box with a deck.
- Cobbles are 2x2-tile blocks; a one-tile strip along a frontage shows the
  flat fill instead.
- A run's light is the light at its two end cells, blended along the piece
  (a GL 3.3 vertex shader); the software rasterizer (which never has the
  licensed files) would draw the average.
- Some crowd bodies render white in daylight (a rig's embedded texture does
  not load: raylib warns `IMAGE: Data format not supported` for each rig);
  that is the rig export, not this lane.
