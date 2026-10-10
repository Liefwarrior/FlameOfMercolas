#!/usr/bin/env python3
"""pack-vfx.py -- cut the chosen Piloto Studio hit / blood / dust / flame / slash textures into
register-scale cells under content/art/lot/vfx/.

Reads tools/lot-pipeline/vfx-manifest.json. For each row: open the LOT source (read-only),
crop the grid cell, optionally crop again to the alpha bounding box (square-padded so the
shape is centred), convert per mode, BOX-downsample to `out`, and save
content/art/lot/vfx/cells/<id>.png. Every cell is also packed into content/art/lot/vfx/vfx.png
on a fixed grid (sheetCell px, sheetColumns wide) with vfx.json beside it
({"cells": {id: {x, y, w, h}}, "animations": {name: [ids]}}) so session code can look a
cell up by name and hand it to SpriteInstance.art with artSize = sheetCell.

Modes
  rgba          keep colour and alpha (pre-coloured sources such as the fireball flipbook)
  mask          keep alpha, force RGB to white -- the tint comes from SpriteInstance.colour
  luma          rgb24-on-black sources: luma becomes alpha, colour becomes white
  packed:r|g|b  channel-packed masks: that plane becomes alpha, colour white

Assertion: after the sampler's cutout (alpha >= cutoutAlpha, 128) a cell must keep at least
minCoverage (4 %) of its texels -- a soft source that would draw nothing fails here instead
of on screen. No tint is baked, no colour is chosen here; the spec's hues live in code.

Idempotent (byte-identical output; unchanged cells are skipped unless --force), --dry-run,
refuses to run unless content/art/lot/ is gitignored. --preview writes a contact sheet with
names: each cell at 1x and 4x nearest over black, and the cutout-only view the renderer sees.

Usage (from the repo root):
  python tools/lot-pipeline/pack-vfx.py
  python tools/lot-pipeline/pack-vfx.py --preview lot-proof-vfx.png
  python tools/lot-pipeline/pack-vfx.py --lot-root D:\\LOT\\Assets --dry-run
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    raise SystemExit("pack-vfx: Pillow is required (pip install pillow)")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lotstage  # noqa: E402

TOOL = "pack-vfx"


def crop_grid(img: Image.Image, grid: list[int], cell) -> Image.Image:
    cols, rows = grid
    if cell == "whole":
        return img
    cw, ch = img.size[0] // cols, img.size[1] // rows
    i = int(cell)
    if not 0 <= i < cols * rows:
        raise SystemExit(f"{TOOL}: cell {i} outside a {cols}x{rows} grid")
    x, y = (i % cols) * cw, (i // cols) * ch
    return img.crop((x, y, x + cw, y + ch))


def to_rgba(img: Image.Image, mode: str) -> Image.Image:
    """Return an RGBA image whose alpha is the shape and whose RGB is what the sampler multiplies."""
    if mode == "rgba":
        return img.convert("RGBA")
    if mode == "mask":
        rgba = img.convert("RGBA")
        white = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
        white.putalpha(rgba.getchannel("A"))
        return white
    if mode == "luma":
        alpha = img.convert("L")
        white = Image.new("RGBA", img.size, (255, 255, 255, 255))
        white.putalpha(alpha)
        return white
    if mode.startswith("packed:"):
        plane = mode.split(":", 1)[1].upper()
        if plane not in ("R", "G", "B"):
            raise SystemExit(f"{TOOL}: packed mode wants r|g|b, got {plane}")
        alpha = img.convert("RGB").getchannel(plane)
        white = Image.new("RGBA", img.size, (255, 255, 255, 255))
        white.putalpha(alpha)
        return white
    raise SystemExit(f"{TOOL}: unknown mode {mode}")


def crop_bbox_square(img: Image.Image, margin: float = 0.04) -> Image.Image:
    """Crop to the alpha bounding box, then pad to a square about its centre (plus a small margin)
    so the shape scales without being squashed."""
    box = img.getchannel("A").point(lambda a: 255 if a >= 8 else 0).getbbox()
    if box is None:
        raise SystemExit(f"{TOOL}: source cell is fully transparent")
    x0, y0, x1, y1 = box
    side = int(max(x1 - x0, y1 - y0) * (1 + 2 * margin)) + 1
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(img, (side // 2 - cx, side // 2 - cy))
    return sq


def coverage(img: Image.Image, cutout: int) -> float:
    a = img.getchannel("A")
    kept = sum(1 for v in (a.get_flattened_data() if hasattr(a, "get_flattened_data") else a.getdata()) if v >= cutout)
    return kept / (img.size[0] * img.size[1])


def save_png_deterministic(img: Image.Image, path: Path) -> None:
    img.save(path, format="PNG", optimize=True, compress_level=9)


def write_preview(cells: list[tuple[str, Image.Image]], cutout: int, out: Path) -> None:
    columns = 4
    zoom = 4
    pad = 8
    tile_w = 32 + pad + 32 * zoom + pad + 32 * zoom + pad
    tile_h = 32 * zoom + 16 + pad
    rows = (len(cells) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * tile_w + pad, rows * tile_h + 24), (0, 0, 0, 255))
    d = ImageDraw.Draw(sheet)
    d.text((4, 4), "lot vfx cells -- 1x | 4x nearest, as staged (alpha) | 4x cutout at alpha >= %d (what drawSprite draws), on black" % cutout,
           fill=(180, 180, 180, 255))
    for i, (cid, img) in enumerate(cells):
        x0 = pad + (i % columns) * tile_w
        y0 = 24 + (i // columns) * tile_h
        sheet.alpha_composite(img, (x0, y0))
        big = img.resize((img.size[0] * zoom, img.size[1] * zoom), Image.NEAREST)
        sheet.alpha_composite(big, (x0 + 32 + pad, y0))
        cut = big.copy()
        cut.putalpha(cut.getchannel("A").point(lambda a: 255 if a >= cutout else 0))
        sheet.alpha_composite(cut, (x0 + 32 + pad + 32 * zoom + pad, y0))
        d.text((x0, y0 + 32 * zoom + 2), f"{cid}  {img.size[0]}x{img.size[1]}", fill=(200, 200, 200, 255))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lot-root", type=Path, default=lotstage.DEFAULT_LOT_ROOT, help="LOT Assets/ directory (read-only)")
    ap.add_argument("--manifest", type=Path, default=Path(__file__).resolve().parent / "vfx-manifest.json")
    ap.add_argument("--out", type=Path, default=None, help="default: <repo>/content/art/lot/vfx")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--preview", type=Path, default=None, help="write a named contact sheet PNG here")
    args = ap.parse_args()

    root = lotstage.repo_root()
    lotstage.assert_gitignored(root)
    m = json.loads(args.manifest.read_text(encoding="utf-8"))
    ids = [c["id"] for c in m["cells"]]
    if len(ids) != len(set(ids)):
        raise SystemExit(f"{TOOL}: duplicate ids in manifest")
    for anim, frames in m.get("animations", {}).items():
        missing = [f for f in frames if f not in ids]
        if missing:
            raise SystemExit(f"{TOOL}: animation {anim} names unknown cells {missing}")
    sheet_cell, columns = int(m["sheetCell"]), int(m["sheetColumns"])
    cutout, min_cov = int(m["cutoutAlpha"]), float(m["minCoverage"])
    out_dir = args.out or (lotstage.lot_dir(root) / "vfx")
    cells_dir = out_dir / "cells"

    sources: dict[Path, Image.Image] = {}
    cells: list[tuple[str, Image.Image]] = []
    rows: list[dict] = []
    written = skipped = 0
    for c in m["cells"]:
        src = args.lot_root / c["src"]
        if not src.is_file():
            raise SystemExit(f"{TOOL}: missing source {src}")
        if src not in sources:
            im = Image.open(src)
            im.load()
            sources[src] = im
        img = crop_grid(sources[src], c["grid"], c["cell"])
        img = to_rgba(img, c["mode"])
        if c.get("bbox"):
            img = crop_bbox_square(img)
        w, h = c["out"]
        if w > sheet_cell or h > sheet_cell:
            raise SystemExit(f"{TOOL}: {c['id']} out {w}x{h} exceeds the {sheet_cell} px sheet cell")
        small = img.resize((w, h), Image.BOX)
        cov = coverage(small, cutout)
        if cov < min_cov:
            raise SystemExit(f"{TOOL}: {c['id']} keeps only {cov:.1%} of its texels at alpha >= {cutout} (min {min_cov:.0%}) -- pick a harder cell or another mode")
        cells.append((c["id"], small))
        dst = cells_dir / f"{c['id']}.png"
        if args.dry_run:
            print(f"  {c['id']:14} {c['src'].split('/')[-1]:36} cell {str(c['cell']):5} {c['mode']:8} -> {w}x{h}  coverage {cov:.0%}")
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not args.force and dst.is_file():
            tmp = dst.with_suffix(".tmp.png")
            save_png_deterministic(small, tmp)
            if tmp.read_bytes() == dst.read_bytes():
                tmp.unlink()
                skipped += 1
            else:
                tmp.replace(dst)
                written += 1
        else:
            save_png_deterministic(small, dst)
            written += 1
        rows.append(lotstage.make_row(root, args.lot_root, dst, src, c["purpose"], m["license"], m["pack"],
                                      id=c["id"], mode=c["mode"], cell=c["cell"], coverage=round(cov, 3)))

    # pack the sheet
    sheet_rows = (len(cells) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * sheet_cell, sheet_rows * sheet_cell), (0, 0, 0, 0))
    index = {}
    for i, (cid, img) in enumerate(cells):
        x, y = (i % columns) * sheet_cell, (i // columns) * sheet_cell
        sheet.paste(img, (x, y))
        index[cid] = {"x": x, "y": y, "w": img.size[0], "h": img.size[1]}

    if not args.dry_run:
        out_dir.mkdir(parents=True, exist_ok=True)
        sheet_png, sheet_json = out_dir / "vfx.png", out_dir / "vfx.json"
        save_png_deterministic(sheet, sheet_png)
        sheet_json.write_text(json.dumps({
            "schemaVersion": 1,
            "provenance": f"Generated by tools/lot-pipeline/{TOOL}.py from {args.manifest.name}; do not hand-edit. "
                          f"Source: {m['pack']} ({m['license']}) -- not redistributable, stays under content/art/lot/.",
            "cell": sheet_cell, "columns": columns, "cutoutAlpha": cutout,
            "cells": index, "animations": m.get("animations", {}),
        }, indent=2) + "\n", encoding="utf-8")
        first = args.lot_root / m["cells"][0]["src"]
        sheet_row = lotstage.make_row(root, args.lot_root, sheet_png, first, f"packed sheet of all {len(cells)} cells on a {sheet_cell} px grid; keyed by vfx.json", m["license"], m["pack"])
        sheet_row["src"] = "Piloto Studio/Textures/* (the cells' sources)"
        json_row = lotstage.make_row(root, args.lot_root, sheet_json, first, "cell rects + animation frame lists for vfx.png", "generated", m["pack"])
        json_row["src"] = args.manifest.resolve().relative_to(root).as_posix()
        rows += [sheet_row, json_row]
        lotstage.write_ledger(root, TOOL, rows)
        lotstage.write_manifest_section(
            root, TOOL, "Hit VFX -- Piloto Studio cells (pack-vfx.py)",
            f"{len(cells)} cells cut from five Piloto textures by `tools/lot-pipeline/vfx-manifest.json`: one steel glint (block), "
            f"one impact fleck (landed), one splash (kill), a 4-frame dust puff (body down), the 8-frame Flame cast, and the optional "
            f"hard-swing crescent. Cut to {sheet_cell} px for `WorldRenderer::drawSprite`'s textured path (hard cutout at alpha >= {cutout}); "
            f"every cell keeps >= {min_cov:.0%} of its texels after the cutout (the `coverage` column in staged.json). No tint is baked: "
            f"`mask`/`luma` cells are white and take `SpriteInstance.colour`; `rgba` cells (puff, flame) keep their own colour. "
            f"Hues, step counts and sizes are the spec's (ASSET-PIPELINE-SPEC 3.4) and are not wired. Licence: {m['license']}.",
            rows)
        print(f"{TOOL}: {len(cells)} cells -> {cells_dir.relative_to(root)} ({written} written, {skipped} unchanged); "
              f"sheet {sheet.size[0]}x{sheet.size[1]} -> {sheet_png.relative_to(root)}")
    else:
        print(f"{TOOL}: dry run, {len(cells)} cells validated; sheet would be {sheet.size[0]}x{sheet.size[1]}")

    if args.preview:
        write_preview(cells, cutout, args.preview)
        print(f"{TOOL}: preview -> {args.preview}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
