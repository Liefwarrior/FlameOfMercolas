# content/art/oblivion — the world's procedural surfaces

Phase 4 of `docs/design/OBLIVION-PLAN.md` (section 7, item 1). Everything under
`materials/` is **generated**, by `granadad-gen-world-materials`
(`native/src/tools/gen_world_materials.cpp`, over `native/src/render3d/world_surfaces.cpp`):

```
cmake --build build/local --target granadad-gen-world-materials
build/local/bin/granadad-gen-world-materials            # writes here, materials/
```

| File | What | Tone from |
|---|---|---|
| `granite_ashlar.png` | coursed granite blocks (walls) | `granite` |
| `granite_flag.png` | irregular flagstones (interior floors) | `granite` |
| `cobble.png` | harbour setts (outdoor floors) | `granite` |
| `brick_wall.png`, `brick_facade.png` | running-bond brick | `brick`, `brick_facade` |
| `plank_deck.png` | floor boards | `oak` |
| `plank_wall.png` | vertical boards | `oak` |
| `wet_wood.png` | tarred, weathered timber: hulls, piles, quay woodwork | `trudgeon_wood` |
| `plaster.png` | lime plaster, the inside of a masonry room | explicit cream |
| `concrete.png`, `reman_facade.png` | panelled reman concrete | `reman_concrete`, `reman_facade` |
| `thatch.png` | straw | `thatch` |
| `slate_roof.png`, `clay_roof.png` | shingle roofs | explicit slate, explicit clay |
| `earth.png`, `ash.png` | packed earth, cinders | `dirt`, `ash` |
| `iron.png` | riveted iron plate | `steel` |
| `cloth.png` | woven cloth | `cloth` |
| `beam.png` | dark oak timber: beams, door posts, trim | explicit |
| `contact-sheet.png` | all of the above on one sheet | |

Each is 128 x 128 texels and tiles on both axes. It covers **two metres** (two
tiles) of wall, floor or roof, 64 texels to the metre; the mesher gives every
vertex world-metre UVs and the shader repeats the surface across them.

**Provenance and licence.** There is no third-party image in here. Every texel is
a pure function of its coordinates through an integer value noise and an integer
Voronoi in `world_surfaces.cpp` (no float, so the same bytes come out of every
compiler), seeded per surface. The base colours come from the renderer's own
material tone table (`render::materialTone`, `native/src/render/atlas.cpp`), the
one place the project says what colour granite, brick, oak and thatch are; three
surfaces (plaster, slate, clay, the beam) name an explicit tone in the generator.
The game does not read these files (it generates the same texels in process, so a
checkout without `content/` draws the same district), and
`native/tests/test_world_surfaces.cpp` fails if they stop being exactly what the
generator writes.
