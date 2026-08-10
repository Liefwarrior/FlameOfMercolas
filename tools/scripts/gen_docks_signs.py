#!/usr/bin/env python3
"""Generate native/include/granadad/sim/docks_signs_generated.hpp from the
`place_sign` markers authored in content/maps/src/docks_surface.tmx.

Deterministic (byte-identical on every run; no timestamps, no randomness) --
walks the .tmx in document order and applies the same VOID-border transform
docks.hpp's kPlaces table already documents:

    world tile x/y = local tile x/y + 32
    world band     = local z-group number + 8

(docks.hpp:11-13 states the rule for kPlaces; the signage survey verified it
independently against sign_k03_gilded_gull, whose transformed rect is an exact
match for kPlaces' own "THE GILDED GULL" entry.)

Each `place_sign` object carries:
  * kind        "door" (a building entrance) or "way" (a street/area post)
  * place       the real display name
  * what        a one-line flavour description
  * x0/x1/y0/y1 the footprint rectangle, in LOCAL tile coordinates
  * the object's own x/y (Tiled pixel coordinates, tilewidth=16) is the sign's
    own placement -- the anchor a label should be drawn near, which sits at
    the building's door/facade rather than at the footprint's centre.

Re-run this script and commit the regenerated header whenever
docks_surface.tmx's markers change.
"""

import os
import xml.etree.ElementTree as ET

SRC = os.path.join(
    os.path.dirname(__file__), "..", "..", "content", "maps", "src", "docks_surface.tmx"
)
OUT = os.path.join(
    os.path.dirname(__file__), "..", "..", "native", "include", "granadad", "sim",
    "docks_signs_generated.hpp",
)

# The VOID border every baked world carries -- see docks.hpp's own header.
WORLD_XY_OFFSET = 32
WORLD_Z_OFFSET = 8
TILE_PX = 16


def escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def parse_local_z(group_name: str) -> int:
    # "z:+11" -> 11
    assert group_name.startswith("z:+"), group_name
    return int(group_name[len("z:+"):])


def walk(elem, current_local_z, out):
    tag = elem.tag
    next_local_z = current_local_z
    if tag == "group" and elem.get("name", "").startswith("z:+"):
        next_local_z = parse_local_z(elem.get("name"))
    if tag == "objectgroup" and elem.get("name") == "markers" and current_local_z is not None:
        for obj in elem.findall("object"):
            if obj.get("type") != "place_sign":
                continue
            props = {}
            pel = obj.find("properties")
            if pel is not None:
                for p in pel.findall("property"):
                    props[p.get("name")] = p.get("value")
            out.append({
                "name": obj.get("name") or "",
                "kind": props.get("kind", ""),
                "place": props.get("place", ""),
                "what": props.get("what", ""),
                "x0": int(props["x0"]),
                "y0": int(props["y0"]),
                "x1": int(props["x1"]),
                "y1": int(props["y1"]),
                "objx": float(obj.get("x")),
                "objy": float(obj.get("y")),
                "local_z": current_local_z,
            })
    for child in elem:
        walk(child, next_local_z, out)


def main():
    tree = ET.parse(SRC)
    root = tree.getroot()
    signs = []
    walk(root, None, signs)
    signs.sort(key=lambda s: s["name"])

    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("// GENERATED FILE -- DO NOT EDIT BY HAND.")
    lines.append("//")
    lines.append("// Produced by tools/scripts/gen_docks_signs.py from the `place_sign`")
    lines.append("// markers in content/maps/src/docks_surface.tmx. Re-run the script and")
    lines.append("// commit the diff if that .tmx changes.")
    lines.append("//")
    lines.append(f"// {len(signs)} signs: every real name a mapper already authored for a")
    lines.append("// Docks building door or street post. World tile x/y = local + 32, world")
    lines.append("// band = local z-group + 8 -- the same VOID-border rule docks.hpp's own")
    lines.append("// kPlaces table uses (docks.hpp:11-13), verified against sign_k03_gilded_gull")
    lines.append('// producing an exact match for kPlaces\' "THE GILDED GULL" rect.')
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("#include <cstddef>")
    lines.append("")
    lines.append("namespace granadad::sim::docks {")
    lines.append("")
    lines.append("enum class SignKind { Door, Way };")
    lines.append("")
    lines.append("struct Sign {")
    lines.append("    const char* id;      // the .tmx object's own name, for debugging/lookup")
    lines.append("    const char* place;   // real display name authored in the .tmx")
    lines.append("    const char* what;    // one-line flavour text")
    lines.append("    SignKind kind;")
    lines.append("    float anchorX;       // world tile: where the sign itself stands")
    lines.append("    float anchorY;")
    lines.append("    std::int32_t band;")
    lines.append("    std::int32_t x0, y0, x1, y1;  // world tile footprint rectangle")
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr Sign kSigns[] = {")
    for s in signs:
        kind = "SignKind::Door" if s["kind"] == "door" else "SignKind::Way"
        world_x0 = s["x0"] + WORLD_XY_OFFSET
        world_y0 = s["y0"] + WORLD_XY_OFFSET
        world_x1 = s["x1"] + WORLD_XY_OFFSET
        world_y1 = s["y1"] + WORLD_XY_OFFSET
        band = s["local_z"] + WORLD_Z_OFFSET
        anchor_x = s["objx"] / TILE_PX + WORLD_XY_OFFSET
        anchor_y = s["objy"] / TILE_PX + WORLD_XY_OFFSET
        lines.append(
            '    {{"{id}", "{place}", "{what}", {kind}, {ax:.3f}F, {ay:.3f}F, {band}, '
            "{x0}, {y0}, {x1}, {y1}}},".format(
                id=escape(s["name"]),
                place=escape(s["place"]),
                what=escape(s["what"]),
                kind=kind,
                ax=anchor_x,
                ay=anchor_y,
                band=band,
                x0=world_x0,
                y0=world_y0,
                x1=world_x1,
                y1=world_y1,
            )
        )
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr std::size_t kSignCount = sizeof(kSigns) / sizeof(kSigns[0]);")
    lines.append("")
    lines.append("}  // namespace granadad::sim::docks")
    lines.append("")

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"wrote {len(signs)} signs to {OUT}")


if __name__ == "__main__":
    main()
