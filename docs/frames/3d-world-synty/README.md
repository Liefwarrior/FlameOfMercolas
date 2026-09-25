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
| 3 | lamps with no light in them, no lit windows | lanterns are `SM_Prop_Camp_Lantern_01` on a bracket arm with a warm translucent halo (two crossed quads then, one billboard since -- see "The halo, from under it" below; their own light, brighter after dark). Fires keep the brazier and get an ember tray in the cage and a flame over it. A window pane goes warm at night when the room behind it is lit by a lamp, or when the tile hash keeps a candle in a roofed room (two windows in three), so the ward is not dead after dark. | 10, 15, 16, 19 |
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
  two cells of the water it is a tarred core with one pile through it. Out of
  doors with a job -- within two cells of a door gap, or one of a pair that
  carries a rail -- it is the strapped timber post fitted to the cell instead
  (the jambs section below).
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
- The lantern halo is one translucent quad with a radial falloff, turned to
  the eye in three dimensions and floated a hand's breadth toward it (see
  "The halo, from under it" below). It reads as a glow; walk into one and it
  fills the view, as a glow does.
- The roof flags land on whole 3x3 blocks at their own module. A roof strip
  narrower than three stays the plain dark fill, lead rather than slate.
- The hull stands plumb. A lean is a knob (`hullFlareDegrees`) but a leaning
  rectangle opens a wedge at every corner, so the shipped catalogue keeps it
  at zero.
- Props, furniture and boats are render-only. The sim knows nothing of them
  and a body walks through a table.
- Some crowd bodies render white in daylight (a rig's embedded texture does
  not load). That is the rig export, not this lane.

## The halo, from under it (2026-09-16)

The placement critic's one open minor: "the halo's edge-on quad shows at
arm's length". Measured first on the gated exe of wip `d23ba091` (dist
digest `6ea17fc2`), which already had the crossed pair collapsed to one quad
yawed to the eye (`55cefc2c`; frame 15 above is the OLD crossed pair, its
seam plain down the ring). What the one yawed quad still did, shot at the
Gull's door lantern (`lamp_gull_door`, hung on the jamb west of the door at
about x 152.7, y 65.58, band 19):

- Straight on at arm's length and from 45 degrees it read as a soft glow,
  no seam. The yaw was doing its job along the street.
- From under it -- stand on the tile in front of the jamb and look up --
  the quad stood plumb whatever the eye did, so at 66 degrees of pitch it
  was foreshortened to a bar and the lantern's base hid what was left.
  The glow was gone from beneath. From a roof it was the same sliver, the
  other way up.
- From beside it, against the sky, the glow was centred on the RING, a
  hand above the glass. `kLanternFlameDrop` measured 0.66 from the piece's
  origin, taken for the top ring; the origin is the top of the lantern's
  hanging rod, 0.4 above the ring, and the glass is 0.8 to 1.0 down.
- By noon an eighth of the alpha (`kFlameDayAlpha`) is nothing you can
  see against a daylit wall, from any angle. No square.

What changed, code in `native/src/render3d/`:

- `world_scene.cpp`, the billboard block: the quad is a SPHERE'S billboard
  now. Its normal follows the whole line to the eye -- the yaw as before,
  then a pitch about the quad's own X (`StaticInstance::pitch`, which the
  adapter applies before the yaw), down to an eye under the lamp, up to
  one on the roof -- so there is no angle it is edge-on from. The origin
  is re-derived so the centre holds. And the centre floats toward the eye
  along the eye's own ray, half the quad's height (`kHaloForward`, 0.29 m
  on a lantern, never more than four tenths of the way to the eye): the
  same pixel as the flame, but the plane clears the lantern's cap, cage
  and base from every side beyond 0.72 m, so the body never slices the
  glow along a line that walks with the eye. The glow is drawn over the
  lamp and the lamp reads through it. Nearer than that the cap wins and
  the base's rim comes through (the last row of the table).
- `rl_backend.cpp`, `drawStaticMesh`: a `kDrawHalo` mesh is depth-tested
  (the cage and the wall stand in front of it where they should) but
  never depth-WRITTEN, so the clear corners of its square cannot cut a
  hole through a halo or a pane drawn after it. It was writing depth.
- `static_pieces.cpp`: `kLanternFlameDrop` 0.66 to 0.9, the glass. The
  brazier and the hearth flames are untouched and ride the same billboard.
- The world-scene test "a flame's halo faces the eye from wherever the
  eye is" now looks from under the lamp and from the roof as well, checks
  the normal in three dimensions, and picks the lantern's flame (the
  house's hearth has one too, placed first).

Reshoot on the next gated exe, from the repo root, `--smoke=40 --hold` on
each as in the table:

| what | command (after `dist\granadad.exe --smoke=40 --hold`) | should show |
|---|---|---|
| night, straight on, arm's length (1.1 m) | `--spawn=152,64,19 --yaw=180 --pitch=15 --fov=60 --time=22 --screenshot=halo-front.png` | a soft round glow centred on the lantern's glass, the cage and cap read through it, no edge anywhere, the wall lit warm behind |
| night, 45 degrees | `--spawn=151,64,19 --yaw=132 --pitch=10 --fov=60 --time=22 --screenshot=halo-45.png` | the same disc, the same size, no seam or bar; the door's light and the window beside it unbroken by it |
| night, beside it, against the sky | `--spawn=151,65,19 --yaw=94 --pitch=14 --fov=60 --time=22 --screenshot=halo-beside.png` | a round glow on the GLASS, not the ring, soft against the black sky; the rod and ring above it barely touched |
| night, from under it (0.37 m) | `--spawn=152,65,19 --yaw=112 --pitch=54 --fov=60 --time=22 --screenshot=halo-below.png` | a glow, round, filling much of the view at this range, the lantern's base a warm dark shape inside it; no bar, no sliver. This close the float is capped short of the eye (four tenths of the way, so the quad stays off the near plane) and the base's own rim stands in front of the glow's plane: the underside of a lamp is dark, its rim comes through the glow. Step back a tile and the plane clears the whole body |
| noon, straight on | `--spawn=152,64,19 --yaw=180 --pitch=15 --fov=60 --time=12 --screenshot=halo-noon.png` | the lantern and its wall in daylight, the glow an eighth of its night alpha -- nothing you can point to, and no square |
| noon, from under it | `--spawn=152,65,19 --yaw=112 --pitch=54 --fov=60 --time=12 --screenshot=halo-noon-below.png` | the same: the base and the wall, no bar |

The scene hash moves (every halo carries a pitch and a float now); the
tavern and population baselines do not (render only). Two runs of any line
above must still be byte-identical: nothing in the turn reads a clock or a
die, only the eye and the anchor.

## Numbers

Gate stamp `624164ad`, native digest
`5570bd9d9054c6e1a25b969cf69de754430d382403599ff6cab60c86eccf2dfd`, 1186
ctest cases, `verify-windows.ps1` PASS with both reports byte-identical.
Tavern baseline `0x86E05F527E54E795` and population baseline
`0x2646C1AAA2BA38DF`, both twice, unmoved. Scene hash on this build,
`--smoke=40 --time=20` twice as separate processes, `0xA47D544D1681F761` both
times with byte-identical PNGs (sha256 `FD3D16BB...4A70A1`). 19,234 pieces
placed over the district, about 3,600 described from the spawn.

## The ward after dark (2026-09-25)

The critic counted the lit windows across the whole Docks at night and got
one, two, three. Fair. Shot the wip build at nine from the spawn and the
Gull's roof: one warm pane beside the Gull's door, one on the roofscape, and
every timber storey a black box. Here is why, and what changed. Code is
`native/src/render3d/static_pieces.cpp` and `world_scene.cpp`.

**Root cause.** Three things stacked.

1. The hung timber window (`SM_Bld_House_Window_04`) is a frame and three
   shutter panels. No glass mesh, so there was nothing to tint. The old rule
   set `hasInside` on the frame and the relight dutifully wrote a warm pane
   tint that no submesh ever wore. Every timber storey, every timber hovel,
   dark by construction.
2. The kit window on masonry did light, two panes in three over a roofed
   room, by the pane's own tile hash. But the Tarwalk is timber above its
   stone ground floors, so from the street that came to the one window
   beside the Gull's door. And per-pane hashing meant a lit house was a
   scatter of odd panes, never a home.
3. The night gate sat at daylight 0.42, which on the sky curve is twenty
   past eight. The eight o'clock frame had every pane dark by definition.

**The law.** A pane glows by its household, never by itself. At placement
the roofed room behind each window is flood-filled once and every window on
it draws one lot off the room's anchor cell, the storey folded out, so a
house's floors agree where their footprints do. The lot's bytes are the
house's candle (85 in 100 keep one), whether it is a night owl (10 in 100
never put it out), its bedtime (hashed between half past nine and half past
two) and its rising hour (four to half past six, the early trades first).
`paneGlows()` reads the lot against the hour at relight, on the minute, so
a house goes dark on the minute its lot names and the placement stays pure
over the tiles. A lamp that reaches the room still lights the pane on top
of all this, as before.

The ward's own signs name the exceptions by their `place` text
(`docks_signs_generated.hpp`, read at the wall cell and then at the room
cell behind it, the smallest listed footprint winning where they nest). The
Gull, the Bilge, the Mission, the Lantern Room, the Rows, the Eel-Pots, the
Watch-Post and the Guardhouse keep their lights whatever the hour. The
King's Bond, the Long Store, the Counting-House, the Impound, the Ropewalk,
Salt Row, Pitchfield, Dawnstalls, Harl's, Merle's, Kennel Row, the Coopers
and the Drowned Hold are kept dark but for a watchman's lamp in one window
in seven (that draw is per pane, so a lamp is a window and never a whole
warehouse). A window with no roofed room behind it (a yard, a deck, a
parapet) is dark unless a lamp reaches the cell.

The timber frame gets a pane: `pane_timber`, the thin plaster quad fitted to
the frame's opening (0.68 by 0.79 m, measured off the frame's vertices), set
two centimetres behind the jamb faces and three clear of the shutter panels,
under the hood and over the sill. Dark blue-grey glass by day, warm by the
law at night. The dusk gate moved to 0.7, last light, about ten to eight.

Deterministic throughout: a hash of the building's cell and the hour, no
RNG, render-only. The sim baselines do not move; the scene hash does.

**What it comes to.** Of the households, about 85 in 100 lit at eight, 77 at
ten, 62 at eleven, 47 at midnight, 16 at two, 8 at three (the owls), 39 at
five, 76 at six, until first light takes every pane at about twenty to
seven. The named houses on top, either way. That is the Oblivion curve: a
town that is up in the evening, thins after eleven, and is a few stubborn
windows and the tavern by two.

**Knobs** (`content/raws/world3d/docks-pieces.json`, `rules`, all read into
the catalogue digest):

| knob | value | what |
|---|---|---|
| `paneDuskBelow` | 0.7 | a pane may glow once the sky's daylight is under this |
| `houseCandlePercent` | 85 | households that keep a candle at all |
| `houseBedtimeFrom` / `To` | 21.5 / 26.5 | the bedtime window, hours past noon (26.5 is half past two) |
| `houseRisingFrom` / `To` | 4 / 6.5 | the rising window |
| `houseOwlPercent` | 10 | candle-keepers that never put it out |
| `storeLampPercent` | 15 | a kept-dark house's windows with a watchman's lamp |
| `litAllNight` | eight names | houses lit whatever the hour, by sign text |
| `keptDark` | thirteen names | stores and yards kept dark, by sign text |

The defaults with no knobs set are the old rule (two in three, up all night),
so a catalogue without them places as it did.

**Tests.** `test_chunk_mesher.cpp`: the law answers the hour on lots built by
hand (bedtime, rising, owl, no candle, lit, dark, none, and the old defaults);
on the house world every window on the ring carries one lot; on the baked
Docks the panes know their house the same way twice, more households are up
at ten than at three by a wide margin, every pane on the Gull is lit at both
hours, the King's Bond is kept dark, and the scene from the Gull's frontage
hashes the same twice, warmer at ten than at three, with no warm pane at
noon.

**Reshoot.** Every line is

`dist\granadad.exe --smoke=0 --hold --width=1280 --height=720 --scale=1 --time=HH --spawn=X,Y,Z --yaw=DEG [--pitch=DEG] --screenshot=path.png`

with these vantages, each at `--time=20`, `--time=23` and `--time=2`:

| vantage | flags | at eight | at eleven | at two |
|---|---|---|---|---|
| the Tarwalk west from the spawn | `--spawn=156,63,19 --yaw=265 --pitch=4` | the Gull's ground and oak storey warm on the left, the Bilge's beyond it, warm panes up the timber storeys down the street, the Bond dark at the far end but for a lamp or none | the Gull and the Bilge as they were, about a third of the other panes gone dark by house, not by pane | the Gull and the Bilge still lit, one or two stubborn windows down the street, the rest dark |
| the Tarwalk east from the Bond | `--spawn=124,64,19 --yaw=85 --pitch=3` | the Bilge's frontage warm on the right, the Gull's beyond, the stalls' panes if any on the left; the Bond behind the eye | the same two houses lit, the street beyond thinner | the two houses lit, the street dark |
| the quay looking back at the frontage | `--spawn=141,55,19 --yaw=150 --pitch=2` | the Bilge and the Gull across the Tarwalk, both storeys warm, the door lantern between | unchanged (both are lit all night) | unchanged; the water and the sky black, the frontage the one warm thing |
| the overview from the Gull's roof, south-west | `--spawn=153,72,21 --yaw=225 --pitch=-18` | the Rows and the Mission lit, Fenner's and the Bathhouse by their lot, warm panes across the roofscape | about a third of the households out, the Rows and the Mission unmoved | the Rows and the Mission and a handful of owls; the Guardhouse at the edge of the window cull |

A house's windows change together. If you see one storey of a house lit and
the other not, either the storeys have different footprints (their rooms
draw apart) or a lamp reaches one room. If you see odd panes on and off
along one wall of one room, that is a bug.

**Honest gaps.** Windows cull at 48 tiles, so an overview from the Saltgate
head sees the terraces lit and the Tarwalk dark for distance, not for the
law. A house that spans two storeys of different footprints draws two lots.
A compound whose houses share a roofed passage is one household. The pane
tint is flat; there is no glow on the wall under a lit window.

## The jambs, 2026-09-16

The critic's line: the Gull's door posts are metre-square 3 m pillars. They
were. The four lone timber cells on the quay's back edge before the Gull --
world (149,61)/(151,61) and (155,61)/(157,61), the Tarwalk's two sign frames
with their rails and boards -- and the oak hitching post against the frontage
at (156,65), two cells from the door, all wore `SM_Bld_Base_Pillar_01`: a
concrete column with a plinth and a capital, fitted to the cell at 2.8x and
tinted brown. Brown concrete either side of a tavern door, a plinth each.

What moved. Code is `native/src/render3d/static_pieces.cpp` (`posts()`,
`doorPostAt()`), the role is `door_post` in
`content/raws/world3d/docks-pieces.json`.

- A lone timber cell out of doors WITH A JOB -- within two cells of a door
  gap's cell (the jamb itself, the hitching post against the wall beside the
  door), or one half of a pair two cells apart that carries the hitching rail
  (the pair `post_rail` already finds, read from either end) -- is a DOOR
  POST: the Knights' strapped timber post (`SM_Prop_Beam_01`, the piece the
  piles are driven from), fitted to the cell as the pillar was, the storey
  tall, turned by cell, in the material's own tint. No plinth, no capital, no
  concrete: a squared oak post with two iron straps.
- A lone timber cell with no door near it and no partner is the pillar it
  was. Indoors it is the plastered pier with its stools (the taproom's tables
  in the sim's terms). Beside the water it is the tarred core with its pile.
  None of those moved.
- The sign and the rail hang exactly where they did -- off the cell's faces,
  not the piece's -- so the Gull's boards still hang in their frames and the
  rails still run post to post.

What did not move, and why. The cell is still a metre square and three tall.
That is the sim's own wall cell, and the chunk box inside it is drawn
whatever the catalogue says (the placeholder rule: no art, the box stands),
so nothing thinner than the cell can stand in it without the box showing
through -- the pile beside the water hides its box under tarred boards for
the same reason, and this post hides it the way the pillar did, edge to edge
plus a hair. A jamb that is genuinely 0.3 m thick needs the mesher to leave
that cell's box out (`chunk_mesher.cpp`'s `CellCache`, keyed off the same
`doorPostAt()` test, and the street's fill laid over the ground the box hid,
since the fill under the Tarwalk's posts is a bare wall top), which is a
lane of its own: the box is what the body collides with, the 2D pass and the
no-art build both draw it, and the frame test pins it standing. This pass
takes the concrete out of the jamb. It does not take the metre out of the
cell.

On the map, off `docks_surface.tmx` with the same rules: 31 cells change --
the Gull's four and its hitching post; the Eel-Pots' oak post at (135,65)
beside its door; the post before the timber house's door in the lane at
(137,82); the yard's rail grid at x 52..58 on rows 72..87 (23 of its 24:
(58,78) has no partner and stays a pillar); the pair at (75,100)/(77,100).
53 outdoor posts stay pillars, 45 indoor ones stay piers, 44 piles stay
piles. Nothing in the sim moved: the tavern and population baselines are
where they were.

Reshoot on this commit's exe once it is gated (the vantages were checked on
`d23ba091`, the BEFORE, where every one of them shows the column). Every
line is `dist\granadad.exe --smoke=0 --hold --width=1280 --height=720
--scale=1` plus:

| frame | vantage | command |
|---|---|---|
| jambs-01 | the Gull's door from the street, zoomed (frame 12's own): the inner pair at the frame's edges | `--time=14 --spawn=153,58,19 --yaw=180 --pitch=-6 --fov=45 --screenshot=jambs-01.png` |
| jambs-02 | the frontage from the water side: both pairs, rails and boards, the door between | `--time=14 --spawn=153,50,19 --yaw=180 --pitch=-2 --fov=60 --screenshot=jambs-02.png` |
| jambs-03 | angled from the east: the east pair close, the hitching post on the wall, the door beyond | `--time=14 --spawn=161,57,19 --yaw=220 --pitch=-4 --fov=60 --screenshot=jambs-03.png` |
| jambs-04 | inside the taproom looking out: the east post and its rail through the doorway (the bouncer stands in it) | `--time=14 --spawn=153,70,19 --yaw=0 --pitch=2 --fov=60 --screenshot=jambs-04.png` |
| jambs-05 | ANOTHER DOOR: the Eel-Pots', its oak post against the wall beside it | `--time=14 --spawn=136,58,19 --yaw=180 --pitch=-4 --fov=60 --screenshot=jambs-05.png` |
| jambs-06 | ANOTHER DOOR: the timber house in the lane, the post two cells before its door | `--time=14 --spawn=131,78,19 --yaw=135 --pitch=-6 --fov=80 --screenshot=jambs-06.png` |
| jambs-07 | UNCHANGED: the lone pillar in the lane behind the Gull, no door within two, no partner | `--time=14 --spawn=148,82,19 --yaw=90 --pitch=-2 --fov=80 --screenshot=jambs-07.png` |
| jambs-08 | UNCHANGED: the taproom's plastered piers with their stools (frame 22's vantage) | `--time=20 --spawn=156,67,19 --yaw=250 --pitch=-3 --screenshot=jambs-08.png` |
