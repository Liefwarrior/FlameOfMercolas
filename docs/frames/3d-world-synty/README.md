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

### Second pass (2026-09-25)

The critic gave the first pass 6/10 and named three things, all fair.

1. "The glow throws nothing." The wall above the cap read the same as the far
   corner and the wall two lamp-widths off was untouched, so the halo agreed
   with no light cast on anything.
2. "The flame is a hard axis-aligned square." A pale rectangle with corners,
   sitting in the glass.
3. "The glass is darker than the paintwork." Flat grey-olive, dimmer than the
   copper cap over it and far dimmer than the lit door panel a metre below.

Number 2 is the one that explains the other two, and it is a backend split we
had not caught. The falloff lived in the GL 3.3 blend shader (`pieceMode == 1`,
a radial alpha). rlsw has no shaders. So the shipped exe drew a disc and the
headless twin drew a flat rectangle, and every frame the critic was reading
came off the software path. The two backends have to agree, and on this they
did not.

What changed, code in `native/src/render3d/` unless said:

- `scene.cpp`, `haloFanMesh()`: the halo is GEOMETRY now. A unit disc, 24
  wedges over 5 rings, white, the falloff in the alpha channel. Two stops: the
  core ring sits at full alpha at 0.22 of the radius (about 0.12 m on a
  lantern, the flame itself), then a smooth `(1 - t)^2` skirt to exactly zero
  at the rim. Zero at the rim is load-bearing, see the width below. Both
  rasterizers carry vertex colours verbatim, so both draw the same glow.
- `rl_backend.cpp`, `drawHaloFan()`: a `kDrawHalo` piece no longer draws its
  own mesh at all. The adapter fits the fan to the piece's local span with a
  matrix and puts it through the piece's own transform, through the default
  material, no shader. Depth-tested, never depth-written, two-sided, as
  before. The `pieceMode == 1` branch is gone from both shaders.
- `static_pieces.cpp`: the halo went 0.46 x 0.58 to 1.1 x 1.1, better than
  three times the lamp's body, and square so it comes out round. That is wider
  than the 0.42 m the lamp stands off its wall, so from along the wall the far
  arc does sink into the plaster and the depth test cuts it. It does not show,
  because the fan is at alpha 0 out there. A brazier's is 0.9 for the same
  reason in the other direction: it burns lower and its skirt would scrape the
  cobbles.
- `world_scene.cpp`, `relightPieces()`: the lantern's GLASS is hot after dark.
  It was `darkPane()` of the lamp's own tint, which is a quarter of it with a
  blue lean, which is exactly the grey-olive the critic called out. It is
  (255, 217, 140) at full value now, and because a lamp is `selfLit` the sky
  never touches it, so it stays hot in the dark. By day it goes back to plain
  glass. This rides the pane slot the light law already fills, so nothing in
  `paneGlows()` or the window rule was touched to get it.
- `lighting.hpp`, `pooledGlow()`: THE POOL. A lamp's falloff is
  `peak * (1 - d^2/r^2)^2` over four to five and a half tiles, which is barely
  a falloff at all: four tiles out still keeps a fifth of the lamp. Ambient
  under it, the surface clamp over it, and a whole street of plaster comes out
  at one value. That is note 1, word for word. So a surface now scales the
  lamp's glow by its own strength, full where the lamp is strong and down to
  0.15 of itself where it is nearly gone. One more power of the falloff with
  the peak and the radius left where they were: nothing gets brighter, the far
  field gets darker, and that is what makes the near field read as a pool.
  Applied in `world_scene.cpp` (kit pieces) and `chunk_mesher.cpp` (walls and
  cobbles), and NOWHERE ELSE. The glow field itself is untouched, so the light
  law behind the panes, the 2D renderer and the stealth model all read the
  numbers they always read.
- `render/controls.hpp`: `kMinFov` 60 to 40. `--fov=45` was silently clamped
  and handed back the same frame as 60 while 75 obeyed, because `Session`
  takes the flag straight off the run config and then calls `sanitise()`. 40
  is a long lens, which is what you want when the subject is 30 cm of lantern
  glass. One number for the flag and the slider both, per #77.

Reshoot, from the repo root, `--smoke=40 --hold` on each:

| what | command (after `dist\granadad.exe --smoke=40 --hold`) | should show |
|---|---|---|
| night, straight on, arm's length (1.1 m) | `--spawn=152,64,19 --yaw=180 --pitch=15 --fov=45 --time=22 --screenshot=halo2-front.png` | the glass the brightest thing in the frame, white-amber, hotter than the copper cap and hotter than the door panel below it. A round glow round it, soft, three times the lamp across. The plaster within a couple of metres clearly warmer than the plaster at the frame's edge |
| night, 45 degrees | `--spawn=151,64,19 --yaw=132 --pitch=10 --fov=45 --time=22 --screenshot=halo2-45.png` | the same disc, the same size, no seam and no bar; the door's light and the window beside it unbroken by it |
| night, beside it, against the sky | `--spawn=151,65,19 --yaw=94 --pitch=14 --fov=45 --time=22 --screenshot=halo2-beside.png` | a round glow on the GLASS, not the ring, soft against the black sky, no corners anywhere on it |
| night, from under it (0.37 m) | `--spawn=152,65,19 --yaw=112 --pitch=54 --fov=60 --time=22 --screenshot=halo2-below.png` | a glow, round, filling much of the view at this range, the base a warm dark shape inside it. The cobbles under the lamp clearly warmer than the cobbles a few tiles off |
| night, from the Gull's roof, down the street | `--spawn=153,72,21 --yaw=225 --pitch=-18 --time=22 --screenshot=halo2-roof.png` | lamps as pools, not as a wash: each lantern a bright disc with warm plaster and cobble under it and the street between them dark |
| noon, straight on | `--spawn=152,64,19 --yaw=180 --pitch=15 --fov=45 --time=12 --screenshot=halo2-noon.png` | the lantern and its wall in daylight, plain glass, the glow an eighth of its night alpha. Nothing you can point to, and no square |
| noon, from under it | `--spawn=152,65,19 --yaw=112 --pitch=54 --fov=60 --time=12 --screenshot=halo2-noon-below.png` | the same: the base and the wall, no bar, no square |

Still fixed and has to stay fixed: no seam or bar from any angle, and no
square at noon. There is no square anywhere any more, at any hour, because
there is no quad.

The scene hash moves again (the halo's span, the lamp's pane tint and every
surface a lamp reaches). The tavern and population baselines do not: all of
this is render. Two runs of any line above must still be byte-identical.


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
A compound whose houses share a roofed passage is one household. See the
second pass below for the pane's own flatness and the wall's own dark --
one is fixed, the other only half.

### Second pass (2026-09-25, the critic's 5/10)

The critic shot `--smoke=0 --hold --settle-steps=0 --time=HH` and got the
same Bond, the same roofline, at 23:00 and at 02:00. Traced the whole chain
end to end -- `--time=` into `SessionConfig::timeOfDay`, into `Session`'s
own `timeOfDay_` at construction (session.cpp:305), into
`WorldSceneParams::timeOfDaySeconds` off `session.timeOfDay()`
(`SceneRig::refresh`, main.cpp), into `ChunkLighting::timeOfDaySeconds`,
into `relightPieces()`'s own `hour` -- and every link already reads the
clock fresh: `litVersion_` starts at a bucket `chunkVersion()` can never
produce (world_scene.hpp), so the first `refresh()` a shutter ever takes
relights for real, no second frame needed, no `session.step()` needed
either. `skipToHour()` (a sleep, a fast travel) writes `timeOfDay_`
synchronously through `syncClockAfterSkip()` for the same reason. The
sequencing was never broken.

What was: the Bond is `HouseKind::Dark`, a kept-dark store whose watchman's
lamp draws off `lot % 100 < storeLampPercent` alone -- no hour in the
formula, on purpose, a lamp that burns all night. Two shots of the same
building at two hours of the same night are supposed to match; that is the
law working, not frozen. The roofline the critic paired next to it carries
the same named, `litAllNight` buildings for the same reason. Picked the
wrong two witnesses is the honest verdict, but a claim like that needs a
test that cannot be misread, so one was added: `test_chunk_mesher.cpp`,
"a session's own clock relights the ward with no session.step() between the
two reads" builds a real `render::Session` at ten, counts the ward's own
lit household panes through a real `WorldScene::refresh()` (not
`paneGlows()` called by hand), calls `session.skipToHour(2)` -- no step(),
the exact budget a `--settle-steps=0` shutter spends -- refreshes again and
checks the count actually fell. It does.

**The pane was still a flat decal.** Fixed. Every hung timber window is two
stacked placements now, not one (`StaticPlacement::paneCore`,
`static_pieces.cpp`): the sill-side 45% of the opening and the head-side
rest, sharing one seam, never overlapping, so there is no z-fight to sort
and no shader needed. Lit, the sill band keeps the law's own warm tint
unchanged (`knobs.litPane`, 255/198/118 -- already close to the candle a
window is supposed to be), the head band cools toward `kPaneEdgeLit`
(217/140/64, `world_scene.cpp`) -- a hotter core low, a cooler glass high.
Dark, the two bands agree exactly as the single pane used to. Tested in
"a lit timber pane reads as two stops, not one flat wash": the two bands
differ when lit and match when dark, on the real Docks, at ten and at noon.

A true bilinear gradient (the shader every other blended piece uses) turned
out to be closed to glass on purpose --
`rl_backend.cpp:691, if (blended && !glass)` -- and the software backend has
no shader at all, so a GPU-only gradient would not have been the same
picture on both. Two flat, proven bands beat one gradient neither backend
could be trusted to draw the same way.

**Still zero spill, mostly.** A lit window's own timber frame
(`PieceRole::WindowTimber`) now carries its pane's household too
(`houseOf()`, called on the frame the same as the pane) and leans 40% of
the way toward the law's own warm tint when its household is up
(`kFrameWarmMix`, `world_scene.cpp`) -- the reveal itself catches the
candle now. The wall cells further out did not: that wants the pane
registered as a weak `render::Lamp` in `ChunkLighting::dynamicLamps` so
`dynamicGlowAt()` (lighting.cpp) washes the chunk mesh the way a real lamp
does, and the honest radius for a candle behind glass is nearer 1.5 m than
`falloffOf()`'s own floor of 3.5 -- which means a second falloff curve, in
the one function every baked and dynamic light in the game shares, changed
blind. Left it alone rather than gamble the whole ward's lighting on an
untested formula; the frame warming is the piece of this that was safe to
ship without a compiler.

**Reshoot,** the same command as the first pass, plus `--time=5` (the
rising hour) and a close-up on the frontage:

`dist\granadad.exe --smoke=0 --hold --settle-steps=0 --width=1280 --height=720 --scale=1 --time=HH --spawn=X,Y,Z --yaw=DEG [--pitch=DEG] [--fov=40] --screenshot=path.png`

| vantage | flags | at eight | at eleven | at two | at five |
|---|---|---|---|---|---|
| the Tarwalk west from the spawn | `--spawn=156,63,19 --yaw=265 --pitch=4` | as the first pass, and now every warm timber pane shows a hotter sill and a cooler head instead of one flat block, the frame around each one a shade warmer than the wall past it | about a third of the households gone dark by house; the Bond's watchman's window unmoved, by design | the Gull and the Bilge lit, one or two stubborn windows, the rest dark and the Bond exactly as it was at eleven | the early trades' windows picking back up one by one, gradients and frames warming with them |
| the Tarwalk east from the Bond | `--spawn=124,64,19 --yaw=85 --pitch=3` | the Bilge's frontage warm, gradient visible on its oak storey's panes | the same two houses lit, the street thinner | the two houses lit, the street dark, the Bond behind the eye reading exactly as it did at eight and at two -- same lamp, same law | a scatter of risers down the street |
| the quay looking back at the frontage | `--spawn=141,55,19 --yaw=150 --pitch=2` | the Bilge and the Gull across the Tarwalk, both storeys warm, each pane now two-toned and each frame a shade lifted | unchanged (both lit all night) | unchanged | unchanged |
| the overview from the Gull's roof, south-west | `--spawn=153,72,21 --yaw=225 --pitch=-18` | the Rows and the Mission lit, warm panes across the roofscape, the gradient too small to read at this distance but the frames still lift | about a third out, the named houses unmoved | the Rows, the Mission, a handful of owls | risers on the terraces below |
| close-up on the Gull's frontage | `--spawn=145,58,19 --yaw=160 --pitch=0 --fov=40` | every pane on the storey shows its own hot sill and cool head, the timber frame around each one visibly warmer than the plaster beside it, and the wall past the frame reads exactly as dark as it did before this pass -- the honest edge of what shipped | -- | -- | -- |

A house's windows still change together; a house's watchman's lamp and a
named house's own lights still do not change at all, on any of these
frames, at any hour -- that is `HouseKind::Dark` and `litAllNight` working,
not a bug to chase in a third pass.

### Third pass (2026-09-25, the record critic's four)

The record critic on e3e7a600 gave the halo 7 and the night windows 6 with
four notes: the lantern's glass was the dullest part of the lamp and the
pool died by eight metres; the timber pane's two stops were inverted with
a seam between them, tan not amber; the boards round a lit pane stayed
black; and the roof at two and at five were the same picture, so the rising
window never relit. Here is what each turned out to be and what changed.
Code is `render/lighting.{hpp,cpp}`, `render3d/world_scene.cpp`,
`render3d/static_pieces.cpp`, `render3d/rl_backend.cpp`, `render3d/scene.cpp`,
`render3d/actor_instances.cpp`; knobs and the glass cut in
`content/raws/world3d/docks-pieces.json`.

**The lamp's glass.** The camp lantern the Docks hang
(`SM_Prop_Camp_Lantern_01`) is ONE opaque textured mesh. Its glass is a
swatch of the Dungeon Realms atlas, not a material, so no sub-mesh was ever
classed as glass and `kLampGlassNight` never reached it: the "hot glass after
dark" of the second pass tinted nothing, and the glass drew as the same tan
swatch the dome over it wears, dimmer than the copper bars. The catalogue
names the glass now -- `glassBand` [-1.0, -0.8] in the model's own Y and
`glassUv` the atlas patch its vertices sample (measured off the glTF: the
lower body's swatch sits at v 0.7470..0.7475, the cap's at 0.7476..0.7480) --
and the adapter cuts those triangles into a glass mesh of their own at load
(`applyGlass`, `rl_backend.cpp`), de-indexed, with a ramp in its vertex
colours that is white round the wick and eases to a deep amber at the head.
Glass draws on the backend's white texture in the pane tint VERBATIM, never
through the material's base colour, so both rasterizers give the same
near-white amber (255/240/205). The metal is sky-lit and its own pool now,
capped at half after dark (`kLampMetalNightCap`), so the cap never
out-values the glass at any range; by day it keeps the tint it always had.
The kit window's real glass material gets the same ramp at load, hot at the
sill.

**The pool.** The baked field is the light law's number and the 2D pass's:
`P * (1 - d^2/R^2)^2` over four to five and a half tiles, and `pooledGlow()`
then pulled the far field down again so a street did not come out at one
value -- together a pool gone by four metres. The 3D pass has a field of its
own now, `render::LampGlow::buildPool`, over the same lamps by the same
accumulation (saturating union, weighted colour) under its own shape: an
inverse-square core over the first metre plus a linear skirt that reaches
nothing at ~8.5 tiles (`poolValueAt`, pinned in a test). Ambient at night is
0.05; the skirt is still twice that at six tiles. Read by the chunk colour
stage (`ChunkLighting::pool`), the piece relight and the bodies' tint
(`ActorSceneParams::pool`) and by nothing else: the law reads the baked
field, the 2D pass paints with it, the sim never knew either existed. The
bodies took the law's RAW glow before -- nine tenths of white a tile from a
lantern -- which is why the Creel urchin stood beside a lamp at night in his
daylight coat. The halo disc grew with the pool, 1.5 m on a lantern and 1.2
on a fire, its flat core held at the 0.24 m it was (`kHaloCoreFraction`
0.16).

**The pane.** One placement per hung window, drawn as `paneRampMesh()`
(`scene.hpp`) in the pane tint -- `kDrawPaneRamp`, the halo's own mechanism:
a unit quad in three rows, white at the sill, still nearly white at the knee
(38% up), a deep amber multiplier at the head, fitted to the placement's
span -- so the glass is hottest at the sill where the candle stands and eases
to the head, in vertex colour, the same picture on GL and on rlsw. The two
stacked bands and `paneCore` are gone. `litPane` is candle amber now,
255/178/74, on every pane.

**The spill.** The board module a hung window sits in is cut in two at the
window's centre (the seam falls behind the frame), each half carrying the
pane's household through `houseOf()` (`StaticPlacement::spill`, `spillAtB`),
so the relight can add a candle-coloured emissive pool at the half's end AT
the pane (`kSpillGlow`) and a quarter of it at the far end, under the same
clamp everything else wears. One board lit at two ends could only wash flat;
two halves blend outward from the window. It goes out with the pane.

**The morning.** The rising branch was never broken. Probed on the shipped
e3e7a600 exe with the catalogue alone: moving the rising window to half
past midnight lit the roof vantage's candle pixels at two (RGB 181/141/84,
the same value a late bedtime or an all-owl ward gives them), so the knobs
reach `paneGlows()` and the relight honours the rise. What the roof vantage
sees is THREE households, all of which drew rising hours past five under the
4..6.5 window -- a sample of three, and the same three at two and at five.
The window closes at 5.5 now (`houseRisingTo`), so two houses in three are
up by five, and a test pins the rise the way the critic tried to read it:
the ward's lit household panes through a real `Session` at eight, `skipToHour`
to two, to five -- fewer at two than at five, fewer at five than at eight. On
the shipped Docks that is 137 at eight, 36 at two, 133 at five under the
3.5..5.25 window the third round settled on (97 under 4..5.5; of 148
household panes; the named houses and the stores' watchman's lamps on top).

**Tests** (`test_chunk_mesher.cpp`): the ramp mesh's rows and one ramp pane
per opening (176 on the Docks, every one with both halves); the two halves
flanking every lit timber pane warmer at the pane than at the module's end;
the morning counts through a real session; the pool's curve, its reach past the law's field on the real
Docks, and a body's tint within a tile and a half of a lantern against one
ten tiles from any (86 Docks cells seven tiles from a lantern take the pool
where the law's field is black); a lantern's glass out-valuing its metal after dark and
the metal at its catalogue tint by day; the catalogue's glass cut on the
hung lantern and on the ref the adapter reads.

**Fix round one (the three critics on 0e072010: director 6, player 7,
auditor 7).** What they found and what moved:

- *The halo up close and in fog.* Inside two metres the metre-and-a-half
  disc, floating toward the eye, laid one cream veil over cap, cage, glass
  and whoever stood under the lamp; in fog its five-ring skirt showed its
  facets once the veil lifted the background to grey, and lanterns went to
  fog grey before the sacks beside them. The world scene fades the halo's
  alpha with the eye's distance now (`kHaloNearGone`/`kHaloNearFull`,
  `kHaloNearFloor`), takes half the fog's e-folding off it by distance
  (`kHaloFogShare`), the fan has nine rings, and the adapter draws the halos
  PAST the weather's veils so a glow is seen through a fog rather than
  veiled with the wall behind it. Clear frames only see the fade.
- *The spill is a light on the wall, not a board's tint.* Plaster windows
  cast nothing and the timber spill stopped dead at a plank seam. It is a
  map of cells now (`WorldScene::spill`, `buildSpill`): every pane that is
  up lays `kSpillGlow` on its own wall cell and half of it on the two cells
  along the wall and the street cell before it, rebuilt on the minute the
  relight moves, read by the chunk colour stage (`ChunkLighting::spill`),
  the piece relight (through `surfaceAt`), the bodies and the ground items.
  A kit window on plaster casts it as a hung timber window does -- its
  module, one run piece with the glass in the middle cell, takes three
  quarters of its pane's own cell at both ends (`kSpillWindowModuleShare`),
  since its cuts stand a tile and a quarter off the glass; a board half's
  end at the module's cut shares the value with the module beyond it, so
  nothing stops at the seam; the street cell took three quarters
  then (`kSpillStreetShare`; the second round below re-spreads it). The map keys off the pane's own wall cell
  (`StaticPlacement::paneX/paneY`, set by `houseOf()`), not a run piece's
  first cut.
- *The pool on the ground.* A grey cobble under the law's near-white
  lantern colour came out a pale beige-grey. The pool has its own deeper
  warmths (`kLanternPoolColour`, `kFirePoolColour`), a wider core (1.4
  tiles at 0.7 share) and a peak a little over the law's (`kPoolPeakScale`,
  under the pieces' 1.15 surface clamp; the chunks, bodies and items clamp
  at one), and it gutters with the wind as the halos
  do (`SkyState::haloScale`) -- a candle behind glass does not. Its field
  normalises the lamps' hue to a PEAK of one, not the law's mean of one: the
  law's field lifts a lantern's red to 1.25 of the intensity and clips it per
  channel (as it always has; the 2D pass and the light law read those bytes
  and they are unchanged), which on the pool would have flattened the lamp's
  own cell against the tile beside it and turned it yellow-white.
- *One amber at one value.* Every lit pane wore `litPane` exactly. A
  household's candle draws its own strength off its lot now
  (`kCandleSpread`, down to two thirds), per pane on a house lit all
  night, so a frontage is a row of candles and not a strip.
- *The rim ring's sawtooth.* The cut classed a triangle by its centroid, so
  the octagonal ring where the glass meets the cap (y -0.818..-0.790, across
  the band's edge) went half hot, half metal. A triangle is glass only when
  EVERY vertex is in the band and on the patch; the ring is metal whole.
- *Posts capped with no glass.* `hasLampGlass()` capped the lamp post's
  whole body at half after dark with nothing hot on it. The post's glass is
  named now (its pale swatch on the lamp head, y 2.25..2.65), and the cap
  applies only to a lamp whose catalogue row cuts a glass.
- *The software path and the cut metal.* The rebuilt metal mesh carried a
  white colour array, which on the GL 1.1 path REPLACES the material colour
  (`tinted()`): rlsw would have dropped the metal's tint and its cap. The
  metal carries no colour array now; only glass does. An all-glass mesh
  keeps its slot instead of leaving an empty one; the scene hash covers the
  cut's fields; the dead `kPoolRadiusFloor` is gone.
- *Ground items read the law's raw glow* (the rope beside the Gull's lamp
  outshone the setts it lay on). They read the pool and the spill now, and a
  self-lit item's cut glass is hot after dark and dark glass by day; the kit's
  own carried lantern names the same cut as the hung one (the two rows share
  a file, and the adapter's cache is by file).
- *Also in this round, found by its auditor:* the spill scales by the
  candle's own strength, so a two-thirds candle lays a two-thirds spill; and
  the kit window module's ends took the pool at their cuts plus a share of
  the pane's cell (0.45, in place of the cuts' own spill, never on top) --
  a rule the third round retired again, see below.
- *Tests.* A lit pane is read by `candlePane()` (the law's tint at the
  house's strength, never dark glass); the spill map is pinned on the Docks
  with masonry panes among the lit and a spread of strengths, and empty at
  noon; the bodies are pinned against the same roster described without the
  pool (the pool moves the tint of dozens and brightens those in a skirt);
  every hung window has exactly two halves.

Out of this lane, on the record from the same reports: the crosshair prompt
fires through the Gull's roof; the scripted night section opens in daylight
and `--demo-capture`/`--framedump` write HUD-only PNGs; urchins wear the
watchman rig; the 02:00 crowd is the 22:00 crowd; cloned townswomen crowd a
doorway.

**Fix round two (the three critics on 702f0a48: director 6, player 5,
auditor 5).** The first round's spill was a regression in clear weather and
the halo's move past the veils put the lamps out in fog. What moved:

- *The clamp keeps its hue.* Ambient + pool + candle was clamped per channel
  before it multiplied a surface's tint, so a wall under a lantern
  saturated its red first and went on taking green and blue: every lit
  plaster in the ward came out olive, at noon luminance, with a hue seam
  beside the Gull's lamp. `render::clampLight` scales the three channels
  together by the brightest (the pieces, the chunks, the bodies and the
  ground items all go through it), so a candle's light on a wall is a
  candle's colour at any strength. Pinned: a piece at the clamp is never
  bluer over red than its own tint.
- *The spill is a third of what it was, and blended.* `kSpillGlow` 0.62 ->
  0.34 on the pane's own cell, so the wall beside a window stays under the
  pane that lights it (pinned); along the wall 0.45 of it at one tile and a
  tail at two; out into the street more, since the light leaves through the
  glass -- 0.9 on the cell before the window, half on its neighbours and the
  row beyond, so a body talking under a lit window stands in it. The chunk
  colour stage reads the map AT THE VERTEX as the mean of the four cells
  that meet at its corner, so a plaster wall no longer breaks into slabs at
  cell seams; a run piece's cuts already fall between cells.
- *The veils write no depth.* The halos, drawn past the veils since round
  one, were depth-tested against the fog shells round the eye and every
  halo past the first shell was culled: in a fog the lamps went out before
  the walls they lit. `rlDisableDepthMask` round the veils; the halos and
  the bright glass are tested against the world alone.
- *Bright glass through a fog.* A lit pane's or a lamp's glass is a light,
  and a light is seen further into a fog than the wall round it. The world
  scene puts the fog's share by the eye's distance into the pane's alpha
  (`kHaloFogShare`, the halo's own rule) and the adapter draws such glass
  once more past the veils at that alpha over its own veiled pixels
  (`lateGlass`, `drawLateGlass`): hot at five metres, the fog's grey with a
  warmth in it at twenty. 255 in clear air, where nothing changes.
- *The fan is rounder.* Forty-eight segments, twelve rings, the flat core a
  tenth of the span: the iso-alpha contours of a twenty-four-gon read as
  straight edges on a veiled wall, and the core's own edge stood on the
  plaster past the glass. The near fade reaches four metres now
  (`kHaloNearFull`) with a floor of 0.08, so the cap is copper at two and
  three metres and not cream.
- *The rise comes forward.* `houseRisingFrom`/`To` 3.5 / 5.25: the fishers
  are up by five, and the street at five is not the street at two.

Declared, not built: the wind's dim on the pool is steady (a gutter in
motion would want a per-frame relight; the relight runs on the minute); a
kit window has no muntins and no room behind its glass (the Synty piece is
a card; the ramp is what the card can carry).

**Fix round three (the three critics on be4b5973: director 7, player 7,
auditor 6).** Nothing broke the spell outright this round; what held it at
seven and what moved:

- *The fog's own edges.* The "halo rim" the player had reported three
  rounds running, in the same place each time whatever the fan did, was
  never the fan: it is where the weather lane's innermost fog shell (1.5 m
  round the eye) cut the plaster above the Gull's door, a sixteen-percent
  step of that shell's own alpha, and the line that slid across every near
  pane as you walked was the same shell crossing the glass. Three thin
  shells stand inside two metres now (0.7, 1.2, 1.8), then a run of 0.9 m
  to about eight metres and a ladder growing by a tenth a shell beyond
  (`kVeilRadii`, twenty-four shells, no step over a tenth of a night fog's
  veil inside ten metres and only just at 9.9), forty-eight round, so each
  cut is a small step on a curve; the weather's own test pins the count. A
  first cut of this round at twenty-six shells 0.4 m apart blended the same
  small alpha twenty-six times in eight bits and drifted the fogged alley
  olive (the fourth round's player); at twenty-four with wider inner
  spacings the drift is mostly gone -- the player's olive count fell from
  7,770 pixels to none and red leads green on the alley wall again -- with
  a lean of a level or two left on the near cobbles that shows at three
  times saturation. Declared.
- *No seam at a cut.* The kit window module's ends carried a share of the
  pane's cell that its neighbours' ends did not, so every lit window on the
  quay front stood on a panel a step brighter or darker than the next. The
  module reads its cuts exactly as every run piece does now and the pool
  round a window is the map's own footprint, wider and gentler (three tiles
  along the wall, three out into the street, `kSpillSide*`/`kSpillOut*`,
  `kSpillGlow` 0.30). Pinned: on the shipped Docks at ten, every pair of run
  pieces meeting at a cut reads one light there, channel by channel, over
  their own tints -- hundreds of pairs, no seam.
- *The cap at three metres.* The near fade is cubic to five and a half
  metres: a fifth of the disc at three, two fifths at four, full past five
  and a half. The glass carries the lamp at those ranges; the disc the air
  round it.
- *The small hours.* A house lit all night keeps a light whatever the hour,
  but the sim's own tavern has its hearth banked and its candles out by
  five (the capture summary's gull=0), while its panes burned the evening's
  full row -- and on a frontage that never goes dark, two o'clock was five
  o'clock. From three in the morning until daylight takes every pane, a
  house lit all night burns at `kLitHouseSmallHours` (0.55) of its
  strength: the taproom banked and a watchman's candle. The hours are the
  sim's tavern's own (`sim/tavern.hpp`: doors shut at two, the fire to
  three and banked to ten, the doors shut to eleven), so two o'clock is
  still the evening's row and the panes never come back up before first
  light; the banking eases in over the three quarters of an hour before
  three (`kSmallHoursEase`), not in one step on the minute, and nothing ends
  it but first light. Households are untouched. Pinned at ten, two, three,
  five and six.
- *Also this round, by its auditor:* the late glass draws BEFORE the halos
  (drawn after them it cut a pane-shaped hole in every glow) and only
  where the fog's share is under 0.94 (`kLateGlassBelow` 240: a night fog
  past a metre and a third; an overcast's trace only from eight and a half
  metres out, where the halos over it make it moot); a light is red well
  over blue (`r >= 80 && r > b + 40`), which admits every candle at any
  strength and no dark pane, where a sum over 450 had left two thirds of
  the candles out of the fog pass; and the colour a piece is drawn with is
  scaled as one where it would clip at 255, while the chunks, the bodies
  and the ground items clamp their light at one as one colour, since a
  vertex colour or a model tint is clipped per channel by the draw. The
  seam pin's tolerance is one rounding step of each side's tint; the hue
  pin computes the colour the relight must give from the scene's own
  fields; "goes out with the pane" is pinned at night; the near fade is
  pinned.

Declared, still: the wind's dim on the pool is steady; a kit window is a
card; two men standing in the Whistling's own doorway at night are lit by
nothing but its window's third tile of spill, and read dark.

**THE RECORD ON 720f47f8 (2026-09-27): director 8, player 8, auditor 8**, the
first round every hat put at the bar, after four fix rounds (6/7/7, 6/5/5,
7/7/6, 8/7/7). Left on the record, declared and not built: the pool round a
window is the map's footprint and the run piece holding the pane cannot
peak at it (a vertical peak wants per-vertex light on kit pieces); a kit
window is a card with no muntins and no room behind it; the wind's dim on
the pool is steady; two men in the Whistling's doorway read dark; a lean
of a level or two on the near cobbles in fog at three times saturation.
Out of this lane, on the record from the same reports: the Gull's door and
its interior stay lit and open all night while the sim shuts the tavern at
two (the panes bank, the doorway does not); interaction prompts through
geometry (a rope on the door lantern, a mouse on the plaster, the bartender
through the roof); the scripted night section opening in daylight and
--demo-capture writing HUD-only frames; urchins in the watchman's rig; the
02:00 crowd; townswomen piled in a doorway; checker noise on the strapped
posts. The wording of this pass was corrected once more after the fifth
round's auditor (the shells' spacing, a dead branch in the small hours,
the late-glass comment's numbers).

**Reshoot.** `dist\granadad.exe --smoke=0 --hold --scale=1 --weather=clear --time=HH --spawn=X,Y,Z --yaw=DEG [--pitch=DEG] [--fov=DEG] --screenshot=path.png`

| vantage | flags | what to see |
|---|---|---|
| the lamp, arm's length | `--time=22 --spawn=152,64,19 --yaw=180 --pitch=15 --fov=45` | the glass the brightest thing on the lamp, white-amber, hottest a third of the way up; the dome and bars over it dark copper; the plaster behind pooled, not washed |
| the lamp from eight tiles | `--time=22 --spawn=152,56,19 --yaw=180 --pitch=6 --fov=60` | the lamp still a bright point over its cap; the pool on the wall and cobbles readable out to the eye's own feet |
| the cobbles under it | `--time=22 --spawn=152,63,19 --yaw=180 --pitch=-40 --fov=70` | a warm pool on the setts that fades, not a disc with an edge; a body crossing it lit by it |
| the Gull's frontage | `--time=22 --spawn=145,58,19 --yaw=160 --pitch=0 --fov=40` | every timber pane one ramp, hot sill to deep-amber head, no seam; the boards either side warm at the frame and dark at the module's end |
| the overview from the roof | `--time=20/23/2/5 --spawn=153,72,21 --yaw=225 --pitch=-18` | lamps as pools with skirts that carry; the roofscape's panes thinning to two and picking up by five |

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

What that pass did not move. The cell stayed a metre square and three tall.
That is the sim's own wall cell, and the chunk box inside it is drawn
whatever the catalogue says (the placeholder rule: no art, the box stands),
so nothing thinner than the cell could stand in it without the box showing
through -- the pile beside the water hides its box under tarred boards for
the same reason, and that post hid it the way the pillar did, edge to edge
plus a hair. A jamb that is genuinely 0.3 m thick needs the mesher to leave
that cell's box out, which the pass deferred. The critic's answer, scoring
it 3 of 10: still metre-square, storey-tall boxes, and they frame no door.
He was right. The section below is that box coming out.

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

## The jambs, the mesher, 2026-09-25

The box is out. A door's two jamb cells lose their ground storey in the
chunk mesh, and what stands beside the opening is a beam a quarter of a
metre across, floor to head, with a beam laid over the two heads. No
metre-square block, no concrete, no cap.

The rule moved too, and that is half the fix. A door post was any lone
timber cell WITHIN TWO of a door. That is why the critic counted four posts
in two pairs standing well clear of the opening while the Gull's own door
had none: those were the Tarwalk's sign frames, and the frontage's actual
jamb cells were part of a wall run, never lone, never looked at. Now a jamb
is exactly the two cells `findDoors()` already demands: the wall hard
against the left of the opening and the wall hard against its right. Two per
door, no more, and a gate keeps the posts and beam it always had.

Where the rule lives. `native/include/granadad/render3d/door_jambs.hpp` and
its `.cpp`: ONE test, `doorPostAt(tiles, x, y, z)`, pure over the tile bytes
with no catalogue in it, because the mesher has no catalogue. The placer
(`static_pieces.cpp`, the new `jambs()`) stands the beam on every cell it
says yes to; the mesher (`chunk_mesher.cpp`, in `CellCache`'s constructor,
where the doc said it would go) raises that cell's voxel floor to the head
before anything else is asked of it. They cannot drift, because there is
nothing to drift from.

Cutting the cell in the cache and not in the emit loop is what keeps the
frontage whole. Every question after it reads the cut voxel: the jamb's own
faces start at the head, so its west face is still hidden behind the wall
next door and nothing z-fights; the wall next door is no longer covered, so
it draws its reveal down the full height of the opening; the cell under the
jamb is no longer covered, so it draws its top, which is the threshold the
beam stands on; the jamb's underside draws, which is the soffit over the
opening. Above the head the wall meshes exactly as it did. No hole, no
sliver, nothing see-through.

The beam. `SM_Prop_Beam_01` at its own section, 0.261 m, not the 4x the
fitted dress used. Height is the door head, 2.03 m, so the strapped cap that
looked like a maroon block at four times scale now sits under the lintel
where a cap belongs. It stands at the edge of the opening, its whole section
inside its own cell, and its depth straddles the frontage's finish: half
buried in the plaster, half proud of it, which also means it never floats
off a thin timber wall. The head beam is the kit joist, its underside on the
head, running outer face to outer face. Tint is the wall material's own.

What else changed. The old rule keeps its cells and its name in the code
(`streetPostAt()`): a lone timber cell out of doors with a job is still the
strapped post FITTED to its cell, because that cell's box is still standing
round it. So the Tarwalk's sign frames and the Eel-Pots' hitching post stay
timber and do not go back to brown concrete. They are just not door posts
any more. The sign and the rail still hang off the cell's faces, including
on a jamb cell.

Render only. `TileQuery::solid()` is what a body walks into and the cell is
still a wall, so nothing about movement or the twin hashes moves. Tavern and
population baselines unchanged by construction.

Known edge: the mesher cannot ask the catalogue what class a wall is, so the
shared test is geometric. A doorway cut in a wall of a material with NO wall
class (thatch, dirt, ash) would lose its box with no frontage panel to cover
it. Nothing on the Docks map is such a wall, but the day one exists, that is
where to look.

Reshoot. Every line is `dist\granadad.exe --smoke=0 --hold --width=1280
--height=720 --scale=1` plus:

| frame | vantage | command |
|---|---|---|
| jamb-03 | README row 03: the Gull's door from the street, looking south | `--time=10 --spawn=153,61,19 --yaw=180 --pitch=4 --screenshot=jamb-03.png` |
| jamb-12 | README row 12: the Gull's door zoomed, 45 degree field | `--time=10 --spawn=153,58,19 --yaw=180 --pitch=-6 --fov=45 --screenshot=jamb-12.png` |
| jamb-14 | README row 14's vantage: the door head from inside the Gull, the soffit and the reveal | `--time=20 --spawn=153,68,19 --yaw=0 --pitch=18 --screenshot=jamb-14.png` |
| jamb-25 | THE 3/4 STREET VIEW: the frontage on the slant from the east, both jambs and the head beam against the plaster | `--time=14 --spawn=160,57,19 --yaw=215 --pitch=-4 --fov=60 --screenshot=jamb-25.png` |
| jamb-26 | THE CLOSE-UP: one jamb at 40 degrees, foot on the threshold, head under the beam, a body beside it for scale | `--time=14 --spawn=154,60,19 --yaw=195 --pitch=-8 --fov=40 --screenshot=jamb-26.png` |
| jamb-27 | ANOTHER DOOR: the Eel-Pots', its own two jambs | `--time=14 --spawn=136,58,19 --yaw=180 --pitch=-4 --fov=60 --screenshot=jamb-27.png` |
| jamb-28 | UNCHANGED: the Tarwalk's sign frames, still timber, still fitted to their cells | `--time=14 --spawn=153,50,19 --yaw=180 --pitch=-2 --fov=60 --screenshot=jamb-28.png` |
| jamb-29 | A GATE, UNCHANGED: the Netters', its own posts and beam | `--time=10 --spawn=171,96,19 --yaw=180 --screenshot=jamb-29.png` |

## The post is the reveal, 2026-09-25 (jambs, second cut)

The first cut scored 7/10. The critic's four, and where each one landed.
Code is `native/src/render3d/static_pieces.cpp` unless said otherwise.

| # | the defect | what changed |
|---|---|---|
| 1 | the timber post stands a third of a metre outboard of the opening, a plaster strip between it and a grey stone surround: a pilaster pair, not a jamb | the grey surround WAS the kit's door wall (`SM_Bld_Base_Wall_Door_Double_01`), stretched over the gap with its own trim round a hole narrower than the gap and its own plaster margin either side; the post stood at the gap's edge, outside all of that. The kit door wall is gone (`doors()` keeps the gates and the leaves, nothing else). The post's inner face is ON the opening's edge, and the jamb's reveal quad (`walls()`) stands 6 mm inboard of that face, cut from the street post's back to the room post's back: the post is the edge of the reveal. Over the head the wall's own finish closes the frontage to the storey top -- plaster over a rendered door, brick over a brick one, boards over a boarded one (`finishOf()`) -- and a plaster soffit lies across the head between the two lintels (its own role, `soffit`, the ceiling piece face down: a door-head soffit is not a room ceiling) |
| 2 | from the taproom the door is a bare cut in plaster | the frame is mirrored onto the room face: two more posts and a lintel in the plane of the room's plaster, the header over the head in the room's tint, and the room-side panel that ends at the jamb no longer reaches a quarter of a metre past it into the opening (the slot it closed is closed by the post and the reveal now) |
| 3 | the Bilge's door gets the lintel and no posts | the frame the critic shot (`A81_bilge_face`) is the Gilded Gull's, from the Tarwalk facing south -- the lantern to the right of the door and the hitching post to its left are the Gull's, the Bilge's hitch stands the other side -- and both posts are in it, the left one behind a body and the LOOK label. The Bilge's own door (world 137..138 on row 66) has its four. The rule was never false on either: `doorPostAt()` is unchanged, and `StaticPlacementStats::bareJambs` now counts every flank of a door the rule refuses, which the district test pins at zero along with four posts, two lintels and one soffit for every door that is not a gate |
| 4 | at 22:00 the posts take no light from the wall lantern a metre away | two things. The post wore the wall material's tint -- granite's blue-grey [156,160,168] over dark oak, which is a post at a quarter of the plaster's brightness under any light -- and was lit from its own wall cell alone. The frame's timber wears its own oak now, and each post is lit at the cut between its jamb cell and the gap cell beside it (the two averaged), which is the value the wall's own piece ends on at that jamb, with the wall's facing factor. Same light law, same numbers, the timber just shows them |

What did not change: the jamb rule (`door_jambs.hpp`), the mesher's cut
under the head, the gates (their own two posts and beam, as before), the
leaves (hinged just inside the post's face now, still swung open along
the reveal), foot seating, proportion.

Render only. `TileQuery::solid()` is untouched and the cell is still a
wall. Tavern and population baselines unchanged by construction; the
scene hash moves (every door in the ward is four posts, two lintels, two
headers and a soffit now, and two catalogue rows are gone).

### The critics' round

Three critics scored the cut above at 7/10 each -- a director, a player,
a technical-art auditor -- and named the same things. Where each landed.

| # | the defect | what changed |
|---|---|---|
| 1 | a black slot beside every leaf, striped, both faces; from the taproom you could see the street down it | the leaf's inner face hung 16 mm off the post's inner face and the reveal quad 6 mm off it, one-sided, so any angle off the axis looked past the quad's back into the wall's hollow, and straight down the 10 mm between leaf and quad to the street. The leaf's inner face is ON the post's plane now (`kLeafOffReveal` 0) with the reveal quad half a millimetre into the opening, inside the leaf's thickness (`kRevealFlush` 0.0005): no slot, no overlap, nothing to fight |
| 2 | a plaster line down every post's inner face, "a zero-thickness fin at mid-depth that slices through the strap"; the west jamb's inner face read as plaster from the east | the fin was the wall quad's own 5 mm cut overlap (`kCutOverlap`) carried past the opening's edge into the post's depth; a run that ends at a jamb post now stops 5 mm INSIDE the post instead, and the header over the head overlaps that retracted end by twice the hair, since above the head there is no post to hide a seam (the first cut of this fix met it edge to edge and drew a dotted line of the stone course behind). The plaster read was the other half: a post's front and inner faces drew the one flat colour (`kDrawShaded` leaves upright faces alone), so 26 cm of oak depth vanished and the reveal beyond the leaf's edge read as the post's side. The frame's timber is `kDrawFaced` now (scene.hpp, the blend shader): its upright faces take the chunk's own `kFacingX` / `kFacingY` by their normals, the two numbers printed into the shader at load, so a post's two faces split exactly as the wall's faces round it do |
| 3 | a hairline of plaster under every lintel, dashed | the kit joist's section is 0.36 m, not the 0.369 I took from its width, so the beam sat 3 mm off the head and the header's bottom showed under it. The lintel is a different beam now (below) and the header starts 2 cm above the head, inside the beam's body (`kHeaderInset`) |
| 4 | a dark triangle dead-centre on every lintel, the same on every door: a repeated stamp | the kit joist (`SM_Gen_Bld_Beam_01`) carries three notches along its five metres, and squashed to a door's width one landed on every lintel's face. The lintel is the posts' own strapped beam (`SM_Prop_Beam_01`) laid on its side across the two heads, its own section, so the frame is one timber -- straps and all |
| 5 | "the posts take the lantern only as a screen-space halo; the post under the lamp is pixel-identical to the far one" | that one is the light law, not the frame: a piece is lit per cell, with no vertical term, and both posts by the Gull's door sit at the clamp (`kLightClamp`). The posts DO take the lamp -- the tint is the value the wall's piece ends on at that jamb -- but a post will not brighten toward its head until a piece can be lit along its height. On the record, not fixed |

Also on the record, the critics' list for a nine, none of it done here: a sill
or step stone under the posts and threshold; hinge pintles on the street
post and pegs at the lintel instead of the kit's iron collars; per-door
variation (flip, wear, salt); contact darkening at the feet and under the
lintel; a hint of grain on the flat oak.

The round-two critics (director 6, player 7) then found the same slot, the
same fin and a lintel floating a finger over its posts, in the same pixels,
and that was the real find of the lane: the kit's strapped post is 0.209 m
of oak inside 0.261 m of iron collars, and I had placed the whole frame by
the collars (the mesh's bounds, `width`). The shaft's inner face stood 2.6 cm
short of the opening's edge -- that was the slot, the plaster fin and the
"no oak on the inner face"; the lintel's shaft sat 2.6 cm over the head with
its collars hanging under it -- that was the floating lintel and the
"collars through the soffit". `core` is a catalogue field now (the shaft's
section; `coreOf()`), and the frame is placed by it: shaft on the edge, shaft
on the head, the reveal quad from the shaft's back, the leaf's hinge behind
it. The collars stand proud of all of it, as collars do.

Two more off my own frames after that, both the depth buffer and not the
geometry: the soffit ran on ten centimetres under each lintel's underside,
four millimetres apart, and at four metres the two striped; and the lintel's
underside lay in the post's top plane along the whole junction and dotted.
Nothing is coplanar now and nothing overlaps within a hair: the soffit ends
at the lintels' back faces and lies three centimetres over the head; the
lintel sits three centimetres down into the posts' heads and three behind
their faces (two millimetres and one still dotted -- the depth buffer at
four metres cannot tell millimetres apart, and its collars hang under the
head where the soffit's edge crosses them); the reveal quad is on the post's
own plane (`kRevealFlush` 0), starts where the post's back ends, and is
drawn from both sides. The rule for anyone dressing a frame after this: two
surfaces that overlap in area want three centimetres between them, or one
of them wants to end where the other begins.

Round three (director 7, player 7, auditor 7, every item of every list
fixed but one): with the reveal quad on the post's own plane, the tenth of
a millimetre the mesh's bounds leave between the two -- and the leaf's back
face, which the kit draws, lying on that same plane -- tied down the whole
hinge edge as a zipper. So the reveal is a rebate now: a centimetre behind
the post's inner face, running on into the post's depth, the post standing
that centimetre proud of its plaster, and the leaf tucked into the rebate
with its inner face three centimetres behind the plaster. The soffit runs
three centimetres past each reveal into the jamb cell's stone course, so
its edge is buried, and the lintel sits three centimetres into the posts'
heads and three behind their faces. Nothing at a door is within the depth
buffer's reach of anything it does not meet at an edge.

Round four (7, 7, 7 again): the zipper and the dotted joint gone, and one
thing the rebate had let out -- a black band over every open leaf. Not a
hole: the jamb cell's own stone course, which the mesher kept from the head
up, showing through the centimetre of rebate between the leaf's top (the
kit door is 1.97 m) and a soffit three centimetres over the head. So the
soffit now lies at the lintels' top level (`kDoorSoffitHeight`, 2.20 -- the
beams stand proud of it, their tops hidden in the closed head box, and the
hewn V on the room lintel's top with them), and the mesher cuts the jamb
cell to three centimetres over THAT (`kDoorHeadCut`), so the reveal is
plaster from the threshold to the soffit and the stone starts where
nothing can see it. The leaf's hinge end runs a centimetre into the post
instead of stopping a centimetre short (`kLeafOffPost` -0.01): the short
stop showed a sliver of reveal between post and leaf from any stand beside
the door, on both jambs, which the auditor's mirrored cameras called an
asymmetry.

Round five (director 8, player 7, auditor 7): the band gone, two things
left. A comb of dark teeth on the reveal under the lintel, unchanged since
round three: the lintel's iron collars hang 2.6 cm under the head, and the
kit door's top (1.967 m) sat three millimetres under them -- the depth
buffer again. The leaf is scaled to stop at 1.95 (`kLeafTop`), five
centimetres under the collars. And the auditor's mirrored cameras were
right after all: the one jamb of the Gull's door with the WINDOW beside it
showed plaster over the back half of the post's inner face, because a kit
box's end cap stood on the opening's edge a centimetre in front of the
rebated reveal, where a thin quad had retreated into the post. A box (or
a board quad) ending at a jamb post now retreats 4 cm into it
(`kBoxRetract`), the header over the door runs to exactly where each
neighbour ends and abuts it (`jambEndRetract_`, no overlap to fight, no gap
to see through), and the soffit is back at the head with the lintel
resting on the posts' tops -- at the beams' top level the whole inner face
of each lintel hung in the head and its hewn facet read as a tent.

Round six (7, 7, 7): the comb, the tent and the asymmetry gone, and the
header's exact abutment opened the round-one crack again, thinner -- a
see-through hairline over every post on both faces, the hollow and the
sky behind it. Two pieces that meet edge to edge leak: a sliver at grazing
angles where their planes differ by the cuts' stagger, dropped pixels
where the two edges share a line but not their vertices (a kit box beside
a header box). And the soffit level with the lintels' undersides met each
one the same way -- a broken black hairline along the join, a wedge of
dark head box mid-span. Round seven answers both with one rule, on the
record now: NEVER AN EDGE-TO-EDGE JOIN WITH THE HOLLOW BEHIND IT. The
header keeps its exact abutment and gets a backing (`kHeaderBack`): the
wall's finish once more, three centimetres behind the plane, six wider at
each end, from over the soffit to the storey top, so whatever a seam drops
shows plaster; at three centimetres it fights nothing, and it shows
nowhere else. The soffit goes three centimetres up into the lintels'
bodies (`kDoorSoffitHeight` = head + 0.03) and runs to each beam's centre
plane, so it CROSSES the back face instead of meeting it, and the beams
stand three centimetres proud of it, which is what beams over a soffit
do.

Round seven (7, 7, 7, each one fix from 8): the cracks gone square-on
and at three-quarters, the lintel/soffit join clean, the mirrored pairs
holding -- and two things left. The backing leaked at a graze: a ray
through the seam at a grazing angle meets the backing's depth well to the
side of the seam, seven centimetres at the angle a body walking the
street sees the far jamb at, and six centimetres of reach left a
continuous line up the far header's end (17 at x 372, the director's
taproom graze). And the three centimetres of lintel that now stand proud
of the soffit brought back the dark tent mid-span on the inner face, the
same shape marked down in rounds one, four and five. Round eight: the
backing runs on over the whole jamb cell at each end (`kHeaderBackReach`
a metre, leaking under two degrees). And the tent is the beam itself: the
kit's strapped timber is hewn at two corners on opposite edges, a
diagonal cut a hand long on each (off its vertices: the -x/+z arris from
0.75 to 1.04 m along, the +x/-z arris from 1.07 to 1.85), and laid as it
was, one sat on the lintel's inner-bottom arris, in the head, its facet
looking down and in and darkened as an underside. It was never the
normals or the atlas (one swatch over every shaft triangle; and lighting
the faced timber per fragment off the geometric normal, tried first,
left the tent where it was and put derivative noise over every post).
For round eight the lintel was the beam turned a half turn about its own
Z, that corner a chamfer on the front-bottom arris and the other on the
hidden back-top edge -- the lintel needs 2.42 of the beam's 2.49 m, so no
orientation hides both.

Round eight (director 7, player 7, auditor 8): the auditor finds no
crack, gap, see-through, z-fight or asymmetry left anywhere, from any
angle, and clears the frame. The other two hold at 7 on the turned
beam's corner: "a bite, not timber", a dark two-facet peak on the show
face of every lintel, and both say the same thing -- don't hide it,
delete it. Round nine does: a catalogue row can ask for its shaft SQUARED
(`"square": true`, `shaft_square.hpp`), and at load every vertex inside
the shaft's section that lies on one face but short of the other is
pushed out to the arris with its normal set to the face it was on, so
the cut's own triangles collapse to nothing; the collars are outside the
section and untouched. The posts and the lintel ask for it; the street's
hitching posts use the same file as it comes (a squared shaft is its own
loaded model). The half turn comes out again -- nothing left to turn
away from.

Round nine (director 8, player 8, auditor 8): the frame clears the bar.
The auditor's diff of the last two shoots touches nothing but the squared
timber; the scan finds no hairline where the cuts' triangles collapsed,
no crack, gap, see-through, z-fight or asymmetry anywhere, from any
angle, day or night. The one note from all three, taste not fault: the
posts lost their hewn knees with the same rule, and the frame reads as
sawn square stock now. If the hand-worked look comes back it comes back
on purpose -- an even adze face or a stopped chamfer the length of the
arris -- never the kit's two corner cuts.

### What 9 needs, on the record

The four critics' lists, merged, in the order they weight them:

- Collars at the post heads (corner irons, or oak pegs through the joint),
  symmetric, far fewer than twelve a door, none hanging under the head
  into the opening.
- A sill or worn stone step the posts and leaves stand on; the floorboards
  stop in a ruler line on the pavement strip.
- A stop bead or head lining the leaf visibly closes against; a hinge
  pintle and strap on the street post.
- Lamp light with a vertical term on the frame near a lantern; the night
  floor darker so the oak can go dark; the taproom's own lamps on the
  room-face frame.
- Contact darkening at the post feet and under the lintels; variation door
  to door (beam flip, wear, salt-greying on the Docks).
- Deliberate hewn character, if any: an even adze face or a stopped chamfer
  the length of the arris.
- The eave line: the header's top edge flush with its neighbour's (a 1-2 px
  step at fov 35).

### Reshoot

Every line is `dist\granadad.exe --smoke=0 --hold --width=1280 --height=720 --scale=1` plus:

| frame | vantage | command |
|---|---|---|
| jambs2-01 | the Gull's door from the street, the 3/4 view from the west: posts on the opening's edge, no plaster strip, no grey surround, the header in the frontage's plaster (the Tarwalk's sign frames block the wider stands; a body stands by the right post whatever the hour) | `--time=14 --spawn=151,61,19 --yaw=160 --pitch=-4 --fov=55 --screenshot=jambs2-01.png` |
| jambs2-02 | square on, zoomed: the reveal is the post | `--time=10 --spawn=153,58,19 --yaw=180 --pitch=-6 --fov=45 --screenshot=jambs2-02.png` |
| jambs2-03 | the head from inside the Gull: the room-side posts, lintel and header | `--time=20 --spawn=153,68,19 --yaw=0 --pitch=18 --screenshot=jambs2-03.png` |
| jambs2-04 | inside, standing back: the frame from the taproom | `--time=14 --spawn=154,70,19 --yaw=350 --pitch=0 --fov=70 --screenshot=jambs2-04.png` |
| jambs2-05 | the Bilge's own door, from the street | `--time=14 --spawn=137,61,19 --yaw=180 --pitch=-4 --fov=60 --screenshot=jambs2-05.png` |
| jambs2-06 | 22:00, the Gull's door under its lantern: the timber takes the lamp | `--time=22 --spawn=153,59,19 --yaw=180 --pitch=-2 --fov=60 --screenshot=jambs2-06.png` |
| jambs2-07 | a gate, unchanged: the Netters' | `--time=10 --spawn=171,96,19 --yaw=180 --screenshot=jambs2-07.png` |
| jambs2-14 | the west jamb from the east, 1920x1080, no HUD: the post's front and inner faces apart, the reveal behind the leaf | `--width=1920 --height=1080 --nohud --time=14 --spawn=157,62,19 --yaw=225 --pitch=-6 --fov=35 --screenshot=jambs2-14.png` (in place of the 1280x720) |
| jambs2-15 | the Bilge's room face at 22:00, 1920x1080, no HUD | `--width=1920 --height=1080 --nohud --time=22 --spawn=137,69,19 --yaw=0 --pitch=4 --fov=60 --screenshot=jambs2-15.png` |
| jambs2-16 | the Gull's room-face head, looking up, 1920x1080, no HUD: the lintel, its collar, the header over it | `--width=1920 --height=1080 --nohud --time=14 --spawn=153,69,19 --yaw=0 --pitch=22 --fov=50 --screenshot=jambs2-16.png` |
| jambs2-17 | the street face at a graze from the west, no HUD: the header's seam over the east post | `--nohud --time=10 --spawn=151,64,19 --yaw=121 --fov=50 --screenshot=jambs2-17.png` |
| jambs2-18 | the east head close, no HUD: the lintel's end, its collar, the header over it | `--nohud --time=10 --spawn=153,62,19 --yaw=157 --pitch=13 --fov=25 --screenshot=jambs2-18.png` |
| jambs2-19 | standing in the Gull's doorway looking east, 1920x1080, no HUD: the soffit, both lintels proud of it, the leaf against the reveal | `--width=1920 --height=1080 --nohud --time=14 --spawn=153,66,19 --yaw=90 --pitch=10 --fov=70 --screenshot=jambs2-19.png` |
| jambs2-20 | the west post's foot, no HUD (the auditor's mirrored pair, with 21) | `--nohud --time=12 --spawn=153,63,19 --yaw=180 --pitch=-30 --fov=30 --screenshot=jambs2-20.png` |
| jambs2-21 | the east post's foot, mirrored | `--nohud --time=12 --spawn=154,63,19 --yaw=180 --pitch=-30 --fov=30 --screenshot=jambs2-21.png` |
| jambs2-22 | the player's comb vantage, 1920x1080, no HUD: the west reveal's head under the collar | `--width=1920 --height=1080 --nohud --time=10 --spawn=157,62,19 --yaw=228 --pitch=3 --fov=50 --screenshot=jambs2-22.png` |
| jambs2-23 | the street 3/4 from the west at 15:00, 1920x1080, no HUD: the header's seam over the east post, the crack of round six | `--width=1920 --height=1080 --nohud --time=15 --spawn=151,63,19 --yaw=128 --pitch=6 --fov=38 --screenshot=jambs2-23.png` |
| jambs2-24 | the taproom face at 22:00, no HUD: the seams over both room posts | `--nohud --time=22 --spawn=153,69,19 --yaw=0 --pitch=8 --fov=70 --screenshot=jambs2-24.png` |
| jambs2-25 | the director's taproom graze, no HUD: the seam over the far room post at fov 60 | `--nohud --time=14 --spawn=151,68,19 --yaw=60 --pitch=5 --fov=60 --screenshot=jambs2-25.png` |
| jambs2-26 | the auditor's taproom 3/4 from the east, no HUD: the far room post's seam | `--nohud --time=14 --spawn=156,68,19 --yaw=320 --pitch=5 --fov=60 --screenshot=jambs2-26.png` |
| jambs2-27 | the mirror of 25 from the east | `--nohud --time=14 --spawn=155,68,19 --yaw=300 --pitch=5 --fov=60 --screenshot=jambs2-27.png` |

## Weather, 2026-09-25

Four skies, render-only. Roadmap D15 ruled it yes on the one condition that
the sim never reads it, and `DECISIONS.md` has the row. Clear, overcast,
harbour fog and wind, each with an intensity that eases in over an hour and
out over an hour, two to four periods a day, drawn off (seed, calendar day,
clock) and nothing else. Live by default in the game and in every capture;
for photography, pin one with `--weather=STATE` and it sits at full for the
whole run. The summary line says what the frame was shot under either way:
`weather=fog 1.00` pinned, `weather=fog 0.83` a live fog most of the way in,
`weather=clear 0.00` the sky every frame above was drawn under.

Weather is mood, not a feature demo. If a frame reads as "a weather effect"
rather than "a harbour morning", it is wrong.

Every command below is

```
dist\granadad.exe --smoke=0 --hold --width=1280 --height=720 --scale=1 --spawn=152,63,19 --yaw=270
```

plus what the table says, from the repo root, on a build of this lane. The
wip exe in `dist\` predates `--weather=` and will refuse it; the Tarwalk
spawn looks west down the street with the Gull's door on the left.

| frame | what it should look like | add |
|---|---|---|
| clear 07:00 | the reference: dawn's sodium wash along the horizon, the lamps still carrying, the street sharp to its far end. Byte-identical to the same frame shot without `--weather` on a build before this lane. | `--time=7 --weather=clear --screenshot=weather-clear-07.png` |
| clear 14:00 | flat overcast harbour light, the Tarwalk read to its end, the King's Bond's brick past the Gull | `--time=14 --weather=clear --screenshot=weather-clear-14.png` |
| clear 22:00 | near-black, lamp pools, the far end of the street swallowed at seventeen tiles | `--time=22 --weather=clear --screenshot=weather-clear-22.png` |
| overcast 07:00 | the dawn wash mostly gone under a lid: the sky one cool grey from zenith to horizon, a touch dimmer, a lit window warm by contrast | `--time=7 --weather=overcast --screenshot=weather-overcast-07.png` |
| overcast 14:00 | the lid on at noon: no gradient in the sky, the light cooler and flatter, a little more haze down the street and no more than that | `--time=14 --weather=overcast --screenshot=weather-overcast-14.png` |
| overcast 22:00 | barely different from clear night, the zenith a shade flatter. If you cannot tell, that is right | `--time=22 --weather=overcast --screenshot=weather-overcast-22.png` |
| fog 07:00 | harbour fog at first light: milky with a little of the dawn's sodium in it, the far end of the street gone inside ten tiles, the near lamps haloed, faces flat | `--time=7 --weather=fog --screenshot=weather-fog-07.png` |
| fog 14:00 | milky grey-white, eight tiles at full. The sky is the fog. The Gull's frontage is there; the brick beyond it is not | `--time=14 --weather=fog --screenshot=weather-fog-14.png` |
| fog 22:00 | cold grey-blue, six and a half tiles, the blacks lifted a shade by the lamps' own scatter. A lamp pool is a lamp pool; the street past it is nothing | `--time=22 --weather=fog --screenshot=weather-fog-22.png` |
| wind 07:00 | clear-ish and a shade crisper than clear, every flame's halo at two-thirds, the wind bed up if you are listening | `--time=7 --weather=wind --screenshot=weather-wind-07.png` |
| wind 14:00 | the air scoured: further down the street than clear, the zenith a touch deeper. Mostly heard | `--time=14 --weather=wind --screenshot=weather-wind-14.png` |
| wind 22:00 | night with the lanterns guttering, the far end a little further off than clear night | `--time=22 --weather=wind --screenshot=weather-wind-22.png` |
| the quay in fog | the long view down the quay at first light, `--spawn=150,59,19 --yaw=300 --pitch=-6` in place of the Tarwalk spawn and yaw: the water, the rowboats, the piers and the cranes stepping back into the fog, the nearest crane there and the pier heads gone | `--spawn=150,59,19 --yaw=300 --pitch=-6 --time=7 --weather=fog --screenshot=weather-quay-fog.png` |

Shoot any of them twice as separate processes: same seed, same day, same
hour, same sky, byte for byte. That is the whole contract, and
`native/tests/test_weather.cpp` holds the code to it: the clear sky is the
old sky at every minute, a fogged session steps to the clear run's tavern
and population digests, and two sessions under one fog draw one frame.

How the fog is drawn: the 3D pass has never had a fog shader (rlsw has no
shader at all), so the weather's fog is geometry. Nested translucent shells
round the eye, nineteen of them then (twenty-four since the night-light lane's third pass, three of them inside two metres; see that pass) out to fifty-six tiles and closer together
near the eye, each coloured the fog's colour below the horizon and the sky's
own above it, each with the alpha of the fog between it and the one inside,
drawn last in the pass far to near with the depth test on. Whatever stands
inside a shell is clear of it; whatever stands beyond it is seen through it.
The rlsw frame and the GPU frame are the same picture. It is banded where a
shader would be smooth; at eight tiles the eye reads it as fog.

Honest gaps: the hands are still lit under the clear sky (`viewmodel.cpp`
belongs to a live session and was left alone); the software `--2d` pass has
the fog by distance only, no veil; the sky dome sits at the far plane, so a
veil over open sky is the sky's own colour and the fog is only ever on the
world in front of it.
