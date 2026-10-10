#!/usr/bin/env python3
"""select-icons.py -- stage the curated Artsystack white icon set under content/art/lot/icons/.

Reads tools/lot-pipeline/icons-manifest.json, stages each mapped 64 px white icon from LOT
(read-only) as content/art/lot/icons/64/<id>.png named to Granadad's vocabulary (the four
non-square sources in the pack are centred in the 64 px cell; pixels are otherwise untouched), packs every
cell into one sheet content/art/lot/icons/icons-white.png (16 columns of 64 px, no scaling --
the runtime box-filters to the row height) with icons-white.json beside it, then rewrites
this tool's rows in content/art/lot/staged.json and docs/asset-manifest-lot.md.

Assertions (any failure exits non-zero, nothing half-written):
  * every source exists, is RGBA, and fits the manifest cell size
  * where alpha > 0 the RGB is white within 8/255 -- a true 1-colour mask, so a tint by the
    row's ink is lossless
  * no two ids share a source, no id repeats

Idempotent: outputs are byte-identical on rerun; up-to-date copies are skipped unless --force.
Nothing under content/art/lot/ is committed (.gitignore:48); this tool refuses to run if the
ignore line is missing.

Usage (from the repo root):
  python tools/lot-pipeline/select-icons.py                 # stage
  python tools/lot-pipeline/select-icons.py --dry-run       # report only
  python tools/lot-pipeline/select-icons.py --preview out.png [--preview-sizes 14,21,35]
                                                            # contact sheet with names at register sizes
  python tools/lot-pipeline/select-icons.py --lot-root D:\\LOT\\Assets --force
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    raise SystemExit("select-icons: Pillow is required (pip install pillow)")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lotstage  # noqa: E402

TOOL = "select-icons"
WHITE_TOLERANCE = 8


def load_manifest(path: Path) -> dict:
    m = json.loads(path.read_text(encoding="utf-8"))
    ids = [r["id"] for r in m["icons"]]
    srcs = [r["src"] for r in m["icons"]]
    dup_ids = sorted({i for i in ids if ids.count(i) > 1})
    dup_srcs = sorted({s for s in srcs if srcs.count(s) > 1})
    if dup_ids:
        raise SystemExit(f"{TOOL}: duplicate ids in manifest: {dup_ids}")
    if dup_srcs:
        raise SystemExit(f"{TOOL}: two ids share a source: {dup_srcs}")
    return m


def check_mask(img: Image.Image, cell: int, name: str) -> Image.Image:
    """Assert the source is a white RGBA mask no larger than the cell; return it centred in a
    cell x cell canvas (4 of the 400 Artsystack icons are non-square, e.g. map_location 40x60)."""
    if img.mode != "RGBA":
        raise SystemExit(f"{TOOL}: {name} is {img.mode}, expected RGBA")
    if img.size[0] > cell or img.size[1] > cell:
        raise SystemExit(f"{TOOL}: {name} is {img.size}, larger than the {cell} px cell")
    worst = 0
    for r, g, b, a in img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata():
        if a > 0:
            worst = max(worst, 255 - min(r, g, b))
    if worst > WHITE_TOLERANCE:
        raise SystemExit(f"{TOOL}: {name} is not a white mask (RGB deviates {worst}/255 where alpha > 0)")
    if img.size == (cell, cell):
        return img
    canvas = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    canvas.paste(img, ((cell - img.size[0]) // 2, (cell - img.size[1]) // 2))
    return canvas


def pack_sheet(cells: list[tuple[str, Image.Image]], cell: int, columns: int) -> tuple[Image.Image, dict]:
    rows = (len(cells) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * cell, rows * cell), (0, 0, 0, 0))
    index = {}
    for i, (icon_id, img) in enumerate(cells):
        x, y = (i % columns) * cell, (i // columns) * cell
        sheet.paste(img, (x, y))
        index[icon_id] = i
    return sheet, index


def save_png_deterministic(img: Image.Image, path: Path) -> None:
    # No metadata chunks, fixed compression: same pixels -> same bytes.
    img.save(path, format="PNG", optimize=True, compress_level=9)


def write_preview(cells: list[tuple[str, Image.Image]], sizes: list[int], out: Path) -> None:
    """Contact sheet: each icon at each register size, tinted the register's yellow / green / bone
    on true black, with its id underneath -- what a row would look like at 540 / 720 / 1080."""
    tints = [(232, 200, 64), (96, 208, 112), (222, 214, 190)]
    columns = 8
    pad = 6
    big = max(sizes)
    strip_w = sum(s + pad for s in sizes) + big  # icon at each size, then the 64 px source
    col_w = max(strip_w, 150) + pad
    row_h = big + 14 + pad
    rows = (len(cells) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * col_w, rows * row_h + 20), (0, 0, 0, 255))
    d = ImageDraw.Draw(sheet)
    d.text((4, 4), "lot icons -- white masks tinted at %s px (Pillow BOX filter), source 64 px last" %
           "/".join(map(str, sizes)), fill=(180, 180, 180, 255))
    for i, (icon_id, img) in enumerate(cells):
        x0 = (i % columns) * col_w
        y0 = (i // columns) * row_h + 20
        x = x0
        for j, s in enumerate(sizes):
            small = img.resize((s, s), Image.BOX)
            tint = Image.new("RGBA", (s, s), tints[j % len(tints)] + (255,))
            tint.putalpha(small.getchannel("A"))
            sheet.alpha_composite(tint, (x, y0 + big - s))
            x += s + pad
        src64 = img.resize((big, big), Image.BOX) if img.size[0] != big else img
        sheet.alpha_composite(src64, (x, y0))
        d.text((x0, y0 + big + 2), icon_id, fill=(200, 200, 200, 255))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lot-root", type=Path, default=lotstage.DEFAULT_LOT_ROOT, help="LOT Assets/ directory (read-only)")
    ap.add_argument("--manifest", type=Path, default=Path(__file__).resolve().parent / "icons-manifest.json")
    ap.add_argument("--out", type=Path, default=None, help="default: <repo>/content/art/lot/icons")
    ap.add_argument("--force", action="store_true", help="recopy even when the staged file is newer than its source")
    ap.add_argument("--dry-run", action="store_true", help="validate and report; write nothing")
    ap.add_argument("--preview", type=Path, default=None, help="also write a named contact sheet PNG here")
    ap.add_argument("--preview-sizes", default="14,21,35", help="icon sizes for --preview (register rows at 540/720/1080)")
    args = ap.parse_args()

    root = lotstage.repo_root()
    lotstage.assert_gitignored(root)
    m = load_manifest(args.manifest)
    cell, columns = int(m["cell"]), int(m["columns"])
    src_dir = args.lot_root / m["srcDir"]
    if not src_dir.is_dir():
        raise SystemExit(f"{TOOL}: source dir not found: {src_dir}")
    out_dir = args.out or (lotstage.lot_dir(root) / "icons")
    cells_dir = out_dir / str(cell)

    cells: list[tuple[str, Image.Image]] = []
    rows: list[dict] = []
    copied = skipped = 0
    for r in m["icons"]:
        src = src_dir / r["src"]
        if not src.is_file():
            raise SystemExit(f"{TOOL}: missing source {src}")
        img = Image.open(src)
        img.load()
        img = check_mask(img, cell, r["src"])
        cells.append((r["id"], img))
        dst = cells_dir / f"{r['id']}.png"
        if args.dry_run:
            print(f"  {r['src']:32} -> {dst.relative_to(root)}")
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        tmp = dst.with_suffix(".tmp.png")
        save_png_deterministic(img, tmp)
        if not args.force and dst.is_file() and tmp.read_bytes() == dst.read_bytes():
            tmp.unlink()
            skipped += 1
        else:
            tmp.replace(dst)
            copied += 1
        rows.append(lotstage.make_row(root, args.lot_root, dst, src, r["purpose"], m["license"], m["pack"], id=r["id"]))

    sheet, index = pack_sheet(cells, cell, columns)
    sheet_png = out_dir / "icons-white.png"
    sheet_json = out_dir / "icons-white.json"
    if not args.dry_run:
        out_dir.mkdir(parents=True, exist_ok=True)
        save_png_deterministic(sheet, sheet_png)
        sheet_json.write_text(json.dumps({
            "schemaVersion": 1,
            "provenance": f"Generated by tools/lot-pipeline/{TOOL}.py from {args.manifest.name}; do not hand-edit. "
                          f"Source: {m['pack']} ({m['license']}) -- not redistributable, stays under content/art/lot/.",
            "cell": cell, "columns": columns, "rows": sheet.size[1] // cell,
            "icons": index,
        }, indent=2) + "\n", encoding="utf-8")
        first = src_dir / m["icons"][0]["src"]
        sheet_row = lotstage.make_row(root, args.lot_root, sheet_png, first, f"packed sheet of all {len(cells)} cells ({columns} cols x {cell} px); keyed by icons-white.json", m["license"], m["pack"])
        sheet_row["src"] = m["srcDir"] + "/* (the mapped names)"
        json_row = lotstage.make_row(root, args.lot_root, sheet_json, first, "cell index for icons-white.png (id -> cell)", "generated", m["pack"])
        json_row["src"] = args.manifest.resolve().relative_to(root).as_posix()
        rows += [sheet_row, json_row]

        lotstage.write_ledger(root, TOOL, rows)
        lotstage.write_manifest_section(
            root, TOOL, "Icons -- Artsystack white masks (select-icons.py)",
            f"Source `{m['srcDir']}` (LOT). {len(cells)} of 400 names mapped onto Granadad vocabulary by "
            f"`tools/lot-pipeline/icons-manifest.json`; staged as one `64/<id>.png` copy per id plus the packed "
            f"`icons-white.png` sheet. All are 1-colour white alpha masks (asserted within {WHITE_TOLERANCE}/255) meant to be "
            f"tinted by the row's ink at draw time -- an icon column beside register text, never an icon grid "
            f"(ASSET-PIPELINE-SPEC 3.3). Licence: {m['license']} -- inside a build yes, raw in the public repo no.",
            rows)
        print(f"{TOOL}: {len(cells)} icons -> {cells_dir.relative_to(root)} ({copied} copied, {skipped} up to date); "
              f"sheet {sheet.size[0]}x{sheet.size[1]} -> {sheet_png.relative_to(root)}")
    else:
        print(f"{TOOL}: dry run, {len(cells)} icons validated; sheet would be {sheet.size[0]}x{sheet.size[1]}")

    if args.preview:
        sizes = [int(s) for s in args.preview_sizes.split(",") if s]
        write_preview(cells, sizes, args.preview)
        print(f"{TOOL}: preview -> {args.preview}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
