# 3D world, the Synty kit on the Docks, third pass

Every frame here is `dist\granadad.exe` (the docker gate's mingw build, branch
`3d/build`, commit `624164ad`) shot on the Windows host with a real GPU through
the shutter. 1280x720, the 640x360 render upscaled 2x, the terminal HUD on top.
Nothing is staged. The placements are a pure function of the tile map and the
catalogue (`content/raws/world3d/docks-pieces.json`), the people are where the
sim has them, the light is the day curve plus the baked lamps.

Reshoot any frame on this commit with the command in the table. `--hold` keeps
the body on its spawn tile, `--pitch=DEG` looks up or down, a narrow `--fov` is
the zoom. Run them from the repo root.

## The critic's six

The second cut scored 7/10 with two blockers and four minors. Where each one
landed. Code is `native/src/render3d/static_pieces.cpp` unless said otherwise.

| # | the defect | what changed | see |
|---|---|---|---|
| 1 | a 0.25 m pixel band round the Gull's ceiling line wherever a wall stands over the room, and a hole over every pillar | a wall's underside quad hangs at the slab plane now, the same plane the floor ceilings round it hang at, so there is no recess and no slab side to see (`ceilings()`, the underside pass). The ceiling pass also takes a floor over a WALL that stands inside a room, a pillar, the bar, the hearth (`overRoomWall()`), so the room's ceiling is one plane. | 13, 21, 22, 23 |
| 2 | every rowboat a capsized dome, six to a frame | the boat is upright. Its glTF says so (normals down at the keel, up on the boards, the winding agrees, the cleats on the gunwale at Y 1.09); it read as a dome because a flat unlit colour is a dome and a bowl alike. Point pieces are drawn shaded by their own normals now (`kDrawShaded`, the four-tint shader in `rl_backend.cpp`: faces that look down to half, faces that look up lifted an eighth, the sides as they were), so the inside is bright, the outer hull dark and the rim between them reads. And `boatEvery` 7 to 14. | 08, 17, 18 |
| 3 | hard-edged squares behind every lantern by day, bright rectangles at night, a tan square through the brazier | a flame's quad is drawn as a halo (`kDrawHalo`): its alpha falls off radially over the quad to nothing at the edge. By day it keeps an eighth of its alpha, not a third (`kFlameDayAlpha`). The lantern wears a warm tint; it has no glass submesh (one atlas material), so the whole lamp is its own warm light. The brazier is 0.6 and its tray and flame are placed at the stand's own scale. | 03, 15, 16, 25 |
| 4 | the quay wall below the lip chunk atlas, dark chevrons under the quay, a sliver of atlas at a hull's waterline | the chevrons were the cornice's dentils on the harbour band; the cornice stays above it now. A masonry face on the harbour band with the water beside it is a quay wall and wears the flag piece stood on edge (`QuayWall`, `SM_Env_Path_Stone_01`) from the coping down into the water. The sliver was the wedge a leaning quad opens at every corner of a hull, and no rectangle closes a wedge, so the lean is a knob and ships at zero: the hull stands plumb with its gunwale beam and its lines. | 24, 08, 17 |
| 5 | light steps at the Gull's jamb at night, a lighter hairline at every plaster cut by day, no window beside the door | a run's cut at a jamb is lit between the jamb cell and the gap cell, and the door frame is lit across the gap from the same pair, so the two meet at one value (`runLight()` with `beyondA0/A1`). Thin plaster quads overlap whatever they meet by half a centimetre and stand a hair proud, staggered, so no hairline of the chunk shows at a cut. The frame's end returns stay inside the wall's own thickness (the frame's trim is thicker than the wall but stops short of the ends; proud of it, the return's edge was the hairline). A run that ends at a jamb is cut from the door, so the door's bay is a whole bay and the window rhythm starts on it: a window beside every door. | 03, 16, 25 |
| 6 | flags fitted 3 m over 2 tiles as crazy paving, a brick top wearing street cobbles with a red lip, four banded piles either side of the door like a fortress gate | the roof finish keys on the building top: a floor of a roofing material, or any floor with a room under it, or one over that room's walls, whatever the tile says (`roofCell()`), wears the flag piece at its own 1:1 module on whole 3x3 blocks over a dark fill, the rest lead, and its lips in the roof's own dark. A timber post (a lone cell, or one whose single wall neighbour is of another kind) is a square pillar, plaster indoors and timber out; within two cells of the water it keeps its tarred boards and carries one pile, its head over the core. | 05, 06, 11, 01 |

## The critic's fourteen, from the second pass

The first cut scored 6/10 and listed fourteen defects. Where each one
landed. Code lines are in `native/src/render3d/static_pieces.cpp` unless said
otherwise.

| # | the defect | what changed | see |
|---|---|---|---|
| 1 | raw atlas showing through (the Gull's ceiling, the quay, the pier lip) | every dressed wall standing over an open cell gets the plaster quad under it, in the room's own ceiling tint (`ceilings()`, the second pass). Every floor slab edge that faces air gets a lip quad the slab's own height, planks on timber and tinted plaster on stone (`lips()`). Steel has a rule now, so the quay's odd patch is dressed. | 13, 14, 08, 17 |
| 2 | brick facing into plastered rooms behind the door frame | the thin plaster quad stands behind the header and both jambs, cut round the opening, plus two returns over the frame's ends in the reveal planes (`doors()`). Plaster faces in general are the one-sided thin quad now (`WallPlaster`), so a corner has no brick back and no brick end. | 14, 23 |
| 3 | lamps with no light in them, no lit windows | lanterns are `SM_Prop_Camp_Lantern_01` on a bracket arm with a warm translucent halo (two crossed quads, their own light, brighter after dark). Fires keep the brazier and get an ember tray in the cage and a flame over it. A window pane goes warm at night when the room behind it is lit by a lamp, or when the tile hash keeps a candle in a roofed room (two windows in three), so the ward is not dead after dark. | 10, 15, 16, 19 |
| 4 | the Gull an empty box | joists across every room ceiling on the odd grid lines. Tables with benches and mugs on a lattice round the indoor lantern where a clear 3x3 of floor allows. Shelves with bottles on the indoor masonry faces. Barrel racks against walls. The free-standing two-cell masonry block is a hearth and wears the fireplace with a fire in it. The lone timber cells inside are square plastered pillars with a stool each side (`furniture()`, `posts()`). | 07, 21, 22 |
| 5 | floorboard roofs, chimneys on fences | roof planes wear the flagstone piece as dark slates over a slate fill, loose tiles scattered by hash, an upstand along every edge cell (brick over masonry, boards over timber), chimneys one in twelve over masonry walls only in three variants, the odd crate or barrel at the edge (`roofs()`). Still flat, see below. | 05, 06, 19 |
| 6 | hulls as sheds, 1x1 posts as boarded pillars | a timber wall with the harbour beside it is a hull. Its boards run across and lean outward nine degrees, tarred, with a gunwale beam along the open top and mooring lines down the side. Rowboats moor along quay edges one cell in seven, cranes stand one cell back from a pier head. A lone timber cell out of doors is a tarred core with a banded timber post at each corner. The chunk box is still a metre square, that is the sim's cell. | 08, 17, 18, 20 |
| 7 | light steps at seams, lit patchwork on floors | each run piece is lit at the exact cut, blended between the two cell centres the cut falls between, so the neighbour piece gets the same value. Floors and ceilings are lit at their four corner points and blended bilinearly (a four-tint shader in `rl_backend.cpp`), and blocks are capped at 4x4 so a lamp pool resolves. | 07, 22 |
| 8 | brick courses off across seams | brick runs are laid at the module, whole 2.5 m pieces at 1.0 and one filler at the far end, or the last piece stretched a little when the remainder is short. | 11 |
| 9 | ground a repeated 2x2, bare strips | two cobble blocks alternate by tile hash and turn by hash. The one-wide strip along a frontage is still the flat fill in a matching tint, the patterned piece does not fit one tile. | 01, 02, 12 |
| 10 | no windows on timber, doors without leaves, gates unframed | a hung window on timber storeys, the window rhythm is every second piece along a run and never a coin flip, never a squeezed piece, only where a room stands behind. A door leaf hangs open on each jamb. A gate wider than a door gets a post at each jamb and a beam across. | 03, 09, 12, 14 |
| 11 | sconce scale, lamp post under a lintel | the sconce is gone. A lantern beside an overhang hangs on a chain from the overhang's edge instead of standing a post. | 09 |
| 12 | water cull seam | the water plane's reach is 140 and every piece is culled by its nearest reach, not its origin. | 04, 17 |
| 13 | props clipping the wall pieces | a prop is pushed to the wall piece's face, its own radius back from it. | 22 |
| 14 | code nits | the corner share reads the wall's width, the lamp shift comment says what the code does, every hash salt is named, the prop percentages live in one place (`RuleKnobs`), the water depth loop and the light clamp are named constants, and a frustum cull drops every piece behind the eye or off its cone before it is described (`world_scene.cpp`). The banner is the gated commit. | the numbers below |

## Frames

| frame | vantage | command (after `dist\granadad.exe --smoke=40`) |
|---|---|---|
| 01 | the Tarwalk at street level, from the authored spawn, looking west | `--spawn=156,63,19 --yaw=265 --hold --time=10 --screenshot=01.png` |
| 02 | the same, forty steps down the street (the default drive) | `--time=10 --screenshot=02.png` |
| 03 | the Gilded Gull's door, from the street, looking south | `--spawn=153,61,19 --yaw=180 --pitch=4 --hold --time=10 --screenshot=03.png` |
| 04 | the quay, the water, the piers, the cranes | `--spawn=150,59,19 --yaw=300 --pitch=-10 --hold --time=10 --screenshot=04.png` |
| 05 | the rooftop, the game's own `--roofs` drive | `--roofs --time=10 --screenshot=05.png` |
| 06 | overview from the Gull's roof looking north-west, down, at ten | `--spawn=153,72,21 --yaw=300 --pitch=-20 --hold --time=10 --screenshot=06.png` |
| 07 | inside the Gull at eight in the evening | `--threshold=gull --time=20 --hold --screenshot=07.png` |
| 08 | the Long Piers, a hull, a crane | `--threshold=piers --hold --time=10 --screenshot=08.png` |
| 09 | the Netters' gate, the lantern on its chain | `--spawn=171,96,19 --yaw=180 --hold --time=10 --screenshot=09.png` |
| 10 | the Tarwalk at nine at night, the door lamp and a lit window | `--spawn=156,63,19 --yaw=265 --hold --time=21 --screenshot=10.png` |
| 11 | the King's Bond (brick) from the Tarwalk, looking east | `--spawn=120,64,19 --yaw=100 --pitch=-8 --hold --time=10 --screenshot=11.png` |
| 12 | the Gull's door zoomed (45 degree field) | `--spawn=153,58,19 --yaw=180 --pitch=-6 --fov=45 --hold --time=10 --screenshot=12.png` |
| 13 | the Gull's ceiling from the threshold, looking up | `--threshold=gull --time=20 --hold --pitch=50 --screenshot=13.png` |
| 14 | the door head from inside the Gull | `--spawn=153,68,19 --yaw=0 --pitch=18 --hold --time=20 --screenshot=14.png` |
| 15 | the door lantern at nine, up close | `--spawn=152,64,19 --yaw=180 --pitch=20 --fov=50 --hold --time=21 --screenshot=15.png` |
| 16 | the Gull's frontage at nine from the street | `--spawn=150,63,19 --yaw=150 --pitch=6 --hold --time=21 --screenshot=16.png` |
| 17 | the quay edge at the water | `--spawn=147,59,19 --yaw=330 --pitch=-14 --hold --time=10 --screenshot=17.png` |
| 18 | a pier head, the crane on it | `--spawn=131,44,19 --yaw=0 --pitch=-4 --hold --time=10 --screenshot=18.png` |
| 19 | the overview at nine at night | `--spawn=153,72,21 --yaw=300 --pitch=-20 --hold --time=21 --screenshot=19.png` |
| 20 | the Gull's door, lantern and window at 55 degrees (the posts either side are in 01) | `--spawn=152,61,19 --yaw=180 --pitch=2 --fov=55 --hold --time=10 --screenshot=20.png` |
| 21 | the Gull's hearth, head on | `--spawn=150,73,19 --yaw=180 --hold --time=20 --screenshot=21.png` |
| 22 | the Gull's tables, pillars and bar from the snug end | `--spawn=156,67,19 --yaw=250 --pitch=-3 --hold --time=20 --screenshot=22.png` |
| 23 | the doorway corner from inside the room | `--spawn=155,69,19 --yaw=325 --pitch=6 --fov=50 --hold --time=20 --screenshot=23.png` |
| 24 | the quay wall from the pier, coping to water | `--spawn=132,54,19 --yaw=120 --pitch=-22 --fov=70 --hold --time=10 --screenshot=24.png` |
| 25 | the Gull's door bay, the window beside the door, the lantern between | `--spawn=152,60,19 --yaw=180 --pitch=6 --fov=50 --hold --time=10 --screenshot=25.png` |

## What is placed

The chunk mesh stays (it is the collision-true building and the placeholder
every test draws). The pieces are a skin over it, placed by
`native/src/render3d/static_pieces.cpp` from the tile grid.

- **walls**. Brick out of doors on brick buildings (`SM_Bld_Base_Wall_01`, laid
  at its own 2.5 m module with one filler per run). The thin plaster quad
  (`SM_Bld_Base_Wall_Thin_01`) on rendered stone out of doors and on every
  indoor face, tinted per material. The plank quad stood on edge on timber.
  A timber wall beside the harbour is a hull and wears the plank quad across,
  tarred, plumb (the lean is a knob, shipped at zero). A masonry face on the
  harbour band with the water beside it is a quay wall and wears the flag
  piece on edge, coping to water.
- **corners**. A brick wall tile with exactly two exposed adjacent faces gets
  the corner piece. Other convex ends extend so the two faces close the
  corner. One-sided quads extend to their standoff and meet there.
- **windows**. Every second piece along an outdoor run, on whole modules, only
  where a room stands behind, counted from the door's end so the bay beside a
  door gets one. The kit window on masonry, a hung window
  (`SM_Bld_House_Window_04`) on timber. The pane is dark by day and warm at
  night over a lit or a homely room.
- **doors**. A two or three tile gap in a wall line, roofed on one side and
  open on the other, gets the double door frame, the plaster behind it on a
  rendered building, and a leaf hung open on each jamb. Four to eight tiles
  is a gate and gets a post each side and a beam across.
- **cornices, caps, undersides**. The trim along every outdoor masonry roof
  line above the harbour band, the wall heads capped in the wall's colour, the
  plaster quad under every dressed wall that stands over an open cell, at the
  slab plane.
- **floors and lips**. Two cobble blocks alternating and turning by hash on
  brick and granite streets over a flat fill, flags on Reman concrete, planks
  on oak and trudgeon, the flat fill on dirt and ash. The slab side quad
  wherever a floor edge faces air.
- **ceilings and joists**. The plaster quad under every floor slab with a room
  or the water under it, and under the slab over a pillar, a bar or a hearth
  inside a room, tinted per the floor. Beams across every room ceiling on the
  odd grid lines.
- **roofs**. Every building top with sky over it (a roofing material, a floor
  over a room, the ring over that room's walls) wears the flagstone piece at
  its own module as slates over a slate fill, loose tiles one cell in seven,
  an upstand along every edge cell, chimneys one in twelve over masonry
  walls, a crate or a barrel one edge cell in nine.
- **water**. The Dungeon Realms water plane over the harbour's surface, to 140
  tiles.
- **props and furniture**. One floor cell in eleven against exactly one wall
  gets a barrel, a crate or a sack, or a barrel rack indoors. Round every
  indoor lantern, tables with benches and mugs on the lattice and shelves with
  bottles on the masonry. A free-standing two-cell masonry block in a roofed
  room is a hearth.
- **posts**. A timber post (a lone 1x1 cell, or one against a wall of another
  kind) is a square pillar, plastered indoors and timber out of doors. Within
  two cells of the water it is a tarred core with one pile through it.
- **lamps**. Every baked lamp. A fire is a brazier with an ember tray and a
  flame. A lantern beside a wall hangs from a bracket arm, a lantern beside a
  door hangs beside it, a lantern under or beside a roof hangs on a chain, a
  lantern in the open is the lamp post. Every flame is its own light, a soft
  halo that fades by day.
- **shading**. A prop, a boat, a pillar, a chimney, a leaf: anything placed
  at a point is shaded by its own normals, undersides dark and tops lifted,
  so it has volume under a flat sky.
- **harbour**. Rowboats along quay edges over clear water, cranes one cell back
  from pier heads, mooring lines down the hulls.

## Roofs are flat, and that is the call

The critic asked for pitched roofs wherever a plane is not a Skyrunner deck.
There is no such plane. The sim has no roof-walk flag. Every roof cell in the
district is standable, the Skyrunners route across them, and the roof fill in
`test_roofrun.cpp` reaches 24,060 cells by mantle, leap and drop from the
spawn. A pitched piece over any of them would be a picture of a wall the body
walks through. So every roof plane stays flat and is made to read as a flat
roof instead. Slates over a dark fill, an upstand round the edge, chimneys over
the masonry, clutter. If you want pitched roofs on some buildings, that is a
sim decision first (which roofs the bodies may not cross), and the rule is
one line once the tiles say so.

## Honest gaps

- The one-wide cobble strip along a frontage is still the flat fill. The
  patterned block does not fit one tile without squashing its stones.
- No step at the doorways. `SM_Bld_House_StepsSmall_01` is half a metre tall
  and every body would walk through it at every threshold, which reads worse
  than a flush sill.
- A 1x1 timber cell is still a metre square. That is the sim's own cell (the
  Gull's tables are these, in the sim's terms), so the dressing keeps the
  footprint and makes it a pillar or a post cluster rather than pretending
  it is thin.
- The lantern halo is two crossed translucent quads with a radial falloff.
  It reads as a glow; walk through one and the crossing shows.
- The roof flags land on whole 3x3 blocks at their own module. A roof strip
  narrower than three stays the plain dark fill, lead rather than slate.
- The hull stands plumb. A lean is a knob (`hullFlareDegrees`) but a leaning
  rectangle opens a wedge at every corner, so the shipped catalogue keeps it
  at zero.
- Props, furniture and boats are render-only. The sim knows nothing of them
  and a body walks through a table.
- Some crowd bodies render white in daylight (a rig's embedded texture does
  not load). That is the rig export, not this lane.

## Numbers

Gate stamp `624164ad`, native digest
`5570bd9d9054c6e1a25b969cf69de754430d382403599ff6cab60c86eccf2dfd`, 1186
ctest cases, `verify-windows.ps1` PASS with both reports byte-identical.
Tavern baseline `0x86E05F527E54E795` and population baseline
`0x2646C1AAA2BA38DF`, both twice, unmoved. Scene hash on this build,
`--smoke=40 --time=20` twice as separate processes, `0xA47D544D1681F761` both
times with byte-identical PNGs (sha256 `FD3D16BB...4A70A1`). 19,234 pieces
placed over the district, about 3,600 described from the spawn.
