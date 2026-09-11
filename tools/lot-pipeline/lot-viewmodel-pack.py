#!/usr/bin/env python3
"""lot-viewmodel-pack.py - Unity viewmodel renders -> Granadad viewmodel sheets.

Input : content/art/lot/unity-renders/<job>/frames.json + <state>_<n>.png
        (written by tools/lot-pipeline/unity/render-lot.ps1: 512x384 RGBA, alpha
        background, one PNG per (state, frame)).
Output: content/art/lot/viewmodel/<weapon>.png   4-column grid of 192x144 cells
        content/art/lot/viewmodel/<weapon>.json  cell -> state / frame index
        content/art/lot/staged.json              tools.lot-viewmodel-pack.rows (lotstage.py)
        docs/asset-manifest-lot.md               this tool's section (lotstage.py)

Everything under content/art/lot/ is gitignored: the renders are derivatives of
purchased asset-store content (Synty POLYGON rigs, Malbers clips, Synty weapons).
Only this tool and the manifest section are tracked.

Sizing (ASSET-PIPELINE-SPEC section 3.5): the game frame is 640x360
(render/session.hpp:81); the viewmodel cell is 192x144 = 30% x 40% of it, drawn
at k = max(1, h/360) anchored bottom-centre. 512x384 -> 192x144 is an exact
x0.375 BOX downsample done premultiplied (no dark fringe on the cutout edge),
then a hard alpha cut at 128 -- the rule WorldRenderer::drawSprite already
applies to every textured sprite (world_renderer.cpp:849).

Cell order is fixed so a sheet index is stable across reruns:
    idle, charge, swing[0..], hard_swing[0..], block, cast, hit, <others A-Z>
The .json "states" map is what the renderer reads; the cell index is derived.

Deterministic: byte-identical rerun (no timestamps, fixed order, PNG
optimize=False compress_level=9). Outputs newer than every input are skipped
unless --force. Exits non-zero on any assertion (missing state, wrong size or
aspect, a cell that cuts to nothing).

Usage:
    python tools/lot-pipeline/lot-viewmodel-pack.py                  # every viewmodel-* job
    python tools/lot-pipeline/lot-viewmodel-pack.py --job viewmodel-fists --proof out.png
    python tools/lot-pipeline/lot-viewmodel-pack.py --dry-run
"""

import argparse
import json
import math
import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lotstage  # noqa: E402  (shared ledger plumbing, beside this file)

TOOL = "lot-viewmodel-pack"
JOB_PREFIX = "viewmodel-"
STATE_ORDER = ["idle", "charge", "swing", "hard_swing", "block", "cast", "hit"]
REQUIRED_STATES = ["idle", "charge", "swing", "hard_swing", "block"]
PACK = "Synty POLYGON + Malbers Animations"
LICENSE = "asset-store-eula"


def parse_wh(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


# --------------------------------------------------------------------------
# frames.json
# --------------------------------------------------------------------------

def load_frames(job_dir):
    path = os.path.join(job_dir, "frames.json")
    if not os.path.isfile(path):
        raise SystemExit(f"{TOOL}: no frames.json in {job_dir} (run render-lot.ps1 first)")
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    frames = data.get("frames") or []
    if not frames:
        raise SystemExit(f"{TOOL}: {path} lists no frames")
    return data, frames, path


def ordered_cells(frames):
    """Fixed cell order: STATE_ORDER first, unknown states after alphabetically,
    frames ascending inside a state."""
    by_state = {}
    for rec in frames:
        by_state.setdefault(rec["state"], []).append(rec)
    for recs in by_state.values():
        recs.sort(key=lambda r: int(r["frame"]))
    missing = [s for s in REQUIRED_STATES if s not in by_state]
    if missing:
        raise SystemExit(f"{TOOL}: frames.json is missing required state(s): {', '.join(missing)}")
    names = [s for s in STATE_ORDER if s in by_state] + sorted(s for s in by_state if s not in STATE_ORDER)
    cells = []
    for name in names:
        for rec in by_state[name]:
            cells.append((name, int(rec["frame"]), rec))
    return cells


# --------------------------------------------------------------------------
# pixels
# --------------------------------------------------------------------------

def downsample(path, cell, cutoff, posterize):
    """Premultiplied BOX downsample to `cell`, hard alpha cut at `cutoff`,
    RGB zeroed where transparent. Returns (RGBA image, coverage, source size)."""
    im = Image.open(path)
    im.load()
    if im.mode != "RGBA":
        im = im.convert("RGBA")
    src_size = im.size
    small = im.convert("RGBa").resize(cell, Image.BOX).convert("RGBA")
    r, g, b, a = small.split()
    mask = a.point(lambda v: 255 if v >= cutoff else 0)
    rgb = Image.merge("RGB", (r, g, b))
    if posterize:
        rgb = ImageOps.posterize(rgb, posterize)
    rgb = Image.composite(rgb, Image.new("RGB", cell, (0, 0, 0)), mask)
    out = Image.merge("RGBA", (*rgb.split(), mask))
    opaque = mask.histogram()[255]
    return out, opaque / float(cell[0] * cell[1]), src_size


# --------------------------------------------------------------------------
# ledger helpers shared with lot-viewmodel-silhouette.py
# --------------------------------------------------------------------------

def merge_tool_rows(root, tool, new_rows):
    """A run that packs one job must not forget the other sheets this tool has
    ledgered: keep the tool's existing staged.json rows whose files still exist,
    replaced by the new rows where the dst matches."""
    path = lotstage.lot_dir(root) / "staged.json"
    kept = []
    if path.is_file():
        try:
            old = json.loads(path.read_text(encoding="utf-8")).get("tools", {}).get(tool, {}).get("rows", [])
        except (ValueError, AttributeError):
            old = []
        fresh = {r["dst"] for r in new_rows}
        kept = [r for r in old if r["dst"] not in fresh and (lotstage.lot_dir(root) / r["dst"]).is_file()]
    return kept + list(new_rows)


def write_sheet_index(json_out, index):
    with open(json_out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(index, f, indent=2)
        f.write("\n")


# --------------------------------------------------------------------------
# proof strip
# --------------------------------------------------------------------------

def write_proof(path, title, cell, cells, images, frame_wh):
    """Every cell labelled on a checker, then the cell in a mock 640x360 frame at
    the anchor the renderer will use -- so the owner sees state AND scale."""
    cw, ch = cell
    fw, fh = frame_wh
    pad, label_h, per_row = 8, 18, 6
    font = ImageFont.load_default(size=13)
    title_font = ImageFont.load_default(size=16)
    n = len(cells)
    rows = int(math.ceil(n / float(per_row)))
    strip_w = per_row * (cw + pad) + pad
    picks = [i for i, (s, f, _) in enumerate(cells) if (s, f) in (("idle", 0), ("charge", 0), ("hard_swing", 2), ("block", 0))]
    if not picks:
        picks = [0]
    mocks_w = len(picks) * (fw + pad) + pad
    title_h = 30
    W = max(strip_w, mocks_w)
    H = title_h + rows * (ch + label_h + pad) + pad + label_h + fh + pad * 2
    proof = Image.new("RGB", (W, H), (0x1A, 0x19, 0x1F))
    draw = ImageDraw.Draw(proof)
    draw.text((pad, 7), title, fill=(0xC9, 0xC2, 0xB0), font=title_font)

    checker = Image.new("RGB", (cw, ch), (0x2B, 0x2A, 0x31))
    cd = ImageDraw.Draw(checker)
    for y in range(0, ch, 8):
        for x in range(0, cw, 8):
            if ((x // 8) + (y // 8)) % 2 == 0:
                cd.rectangle([x, y, x + 7, y + 7], fill=(0x3F, 0x3E, 0x47))
    for i, ((state, frame, _), im) in enumerate(zip(cells, images)):
        r, c = divmod(i, per_row)
        x = pad + c * (cw + pad)
        y = title_h + r * (ch + label_h + pad)
        tile = checker.copy()
        tile.paste(im, (0, 0), im)
        proof.paste(tile, (x, y))
        draw.text((x, y + ch + 2), f"[{i}] {state} {frame}", fill=(0xC9, 0xC2, 0xB0), font=font)

    my = title_h + rows * (ch + label_h + pad) + pad
    draw.text((pad, my), f"in the {fw}x{fh} frame (k=1): cell anchored bottom-centre at ({(fw - cw) // 2},{fh - ch}), "
                         f"{100 * cw // fw}% x {100 * ch // fh}% of the frame; the HUD band draws over it",
              fill=(0x9A, 0x96, 0x8C), font=font)
    my += label_h
    for k, i in enumerate(picks):
        state, frame, _ = cells[i]
        x0 = pad + k * (fw + pad)
        mock = Image.new("RGB", (fw, fh), (0x12, 0x13, 0x1A))
        md = ImageDraw.Draw(mock)
        horizon = fh // 2
        md.rectangle([0, horizon, fw, fh], fill=(0x1E, 0x1C, 0x21))
        md.line([0, horizon, fw, horizon], fill=(0x2B, 0x2A, 0x31))
        for vx in (fw // 4, fw * 3 // 4):
            md.line([vx, horizon, vx, fh], fill=(0x26, 0x24, 0x2A))
        md.rectangle([fw // 2 - 2, horizon - 2, fw // 2 + 2, horizon + 2], outline=(0x75, 0x74, 0x7C))
        mock.paste(images[i], ((fw - cw) // 2, fh - ch), images[i])
        md.rectangle([0, 0, fw - 1, fh - 1], outline=(0x57, 0x56, 0x5F))
        proof.paste(mock, (x0, my))
        draw.text((x0 + 4, my + fh + 2), f"[{i}] {state} {frame}", fill=(0xC9, 0xC2, 0xB0), font=font)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    proof.save(path, "PNG", optimize=False, compress_level=9)


# --------------------------------------------------------------------------
# one job
# --------------------------------------------------------------------------

def pack_job(job_dir, args, root):
    data, frames, frames_path = load_frames(job_dir)
    job_name = data.get("jobName") or os.path.basename(job_dir)
    weapon = args.name or (job_name[len(JOB_PREFIX):] if job_name.startswith(JOB_PREFIX) else job_name)
    cells = ordered_cells(frames)
    cw, ch = args.cell
    cols = args.columns
    rows = int(math.ceil(len(cells) / float(cols)))
    png_out = os.path.join(args.out, weapon + ".png")
    json_out = os.path.join(args.out, weapon + ".json")

    inputs = [frames_path, os.path.abspath(__file__)] + [os.path.join(job_dir, rec["file"]) for _, _, rec in cells]
    for p in inputs:
        if not os.path.isfile(p):
            raise SystemExit(f"{TOOL}: missing input {p}")
    up_to_date = (os.path.isfile(png_out) and os.path.isfile(json_out) and
                  min(os.path.getmtime(png_out), os.path.getmtime(json_out)) >= max(os.path.getmtime(p) for p in inputs))

    print(f"{job_name}: {len(cells)} cells -> {weapon}.png ({cols * cw}x{rows * ch}), states: "
          + ", ".join(f"{s}[{sum(1 for c in cells if c[0] == s)}]" for s in dict.fromkeys(c[0] for c in cells)))
    if args.dry_run:
        return []

    images, meta, states = [], [], {}
    for i, (state, frame, rec) in enumerate(cells):
        src = os.path.join(job_dir, rec["file"])
        im, coverage, src_size = downsample(src, (cw, ch), args.alpha_cutoff, args.posterize)
        if abs(src_size[0] * ch - src_size[1] * cw) > max(cw, ch):
            raise SystemExit(f"{TOOL}: {rec['file']} is {src_size[0]}x{src_size[1]}, not the cell aspect {cw}:{ch}")
        if coverage < args.min_coverage:
            raise SystemExit(f"{TOOL}: cell {i} ({state} {frame}) keeps {coverage:.1%} opaque texels after the alpha cut "
                             f"(< {args.min_coverage:.0%}): the arms are out of frame or the render is blank -- "
                             f"check the job's camera/eyeOffset, do not ship an empty cell")
        images.append(im)
        states.setdefault(state, []).append(i)
        meta.append({
            "index": i, "state": state, "frame": frame,
            "x": (i % cols) * cw, "y": (i // cols) * ch,
            "src": rec["file"], "clip": rec.get("clipName", ""), "time": rec.get("time", 0.0),
            "coverage": round(coverage, 4),
        })
        print(f"  [{i:2d}] {state:<10} {frame}  {src_size[0]}x{src_size[1]} -> {cw}x{ch}  coverage {coverage:.1%}")

    if up_to_date and not args.force:
        print(f"  up to date: {png_out}")
    else:
        sheet = Image.new("RGBA", (cols * cw, rows * ch), (0, 0, 0, 0))
        for m, im in zip(meta, images):
            sheet.paste(im, (m["x"], m["y"]))
        os.makedirs(args.out, exist_ok=True)
        sheet.save(png_out, "PNG", optimize=False, compress_level=9)
        write_sheet_index(json_out, {
            "schemaVersion": 1,
            "provenance": f"Packed by tools/lot-pipeline/{TOOL}.py from unity-renders/{job_name}/frames.json (Lord of Trojia derivatives, licensed, not redistributable) -- rerun the tool; do not hand-edit",
            "weapon": weapon,
            "sheet": weapon + ".png",
            "cell": [cw, ch],
            "columns": cols,
            "alphaCutoff": args.alpha_cutoff,
            "posterize": args.posterize or 0,
            "states": states,
            "cells": meta,
            "source": {
                "job": job_name,
                "unity": data.get("unity", ""),
                "rig": frames[0].get("rig", ""),
                "weaponPrefab": frames[0].get("weapon", ""),
                "clips": sorted({rec.get("clipAsset", "") for rec in frames if rec.get("clipAsset")}),
            },
        })
        print(f"  wrote {png_out} and {os.path.basename(json_out)}")

    if args.proof:
        write_proof(args.proof, f"viewmodel proof: {weapon}  ({job_name}, Unity {data.get('unity', '')}, cell {cw}x{ch}, alpha cut {args.alpha_cutoff})",
                    (cw, ch), cells, images, args.frame)
        print(f"  proof strip: {args.proof}")

    if args.no_ledger:
        return []
    # ledger rows: sources are the Unity assets the job rendered (paths in frames.json are
    # relative to the LOT project dir, i.e. "Assets/..."; lotstage wants them under Assets/)
    lot_project = Path(args.lot_root).parent
    sources = sorted({frames[0].get("rig", "")} | {rec.get("clipAsset", "") for rec in frames} |
                     ({frames[0]["weapon"]} if frames[0].get("weapon") else set()))
    sources = [lot_project / s for s in sources if s]
    purpose = f"first-person viewmodel sheet '{weapon}': {len(cells)} cells, states " + "/".join(states) + \
              " (192x144 cells, 4 columns; ASSET-PIPELINE-SPEC 3.5)"
    return [
        lotstage.make_row(root, Path(args.lot_root), Path(png_out), sources, purpose, LICENSE, PACK,
                          job=job_name, unity=data.get("unity", ""), cells=len(cells)),
        lotstage.make_row(root, Path(args.lot_root), Path(json_out), sources,
                          f"cell index for {weapon}.png (state -> cell indices, per-cell coverage)", "generated", PACK),
    ]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
    root = lotstage.repo_root()
    ap.add_argument("--renders", default=str(lotstage.lot_dir(root) / "unity-renders"), help="unity-renders root (default: content/art/lot/unity-renders)")
    ap.add_argument("--out", default=str(lotstage.lot_dir(root) / "viewmodel"), help="sheet output dir (default: content/art/lot/viewmodel)")
    ap.add_argument("--lot-root", default=str(lotstage.DEFAULT_LOT_ROOT), help="LOT Assets/ directory, for the ledger's source column (read-only)")
    ap.add_argument("--job", action="append", help="job dir name(s) under --renders, e.g. viewmodel-fists (default: every viewmodel-*)")
    ap.add_argument("--name", help="sheet name override (default: job name minus 'viewmodel-')")
    ap.add_argument("--cell", type=parse_wh, default=(192, 144), help="cell size WxH (default 192x144)")
    ap.add_argument("--columns", type=int, default=4, help="grid columns (default 4)")
    ap.add_argument("--alpha-cutoff", type=int, default=128, help="alpha >= this is opaque (default 128, the renderer's rule)")
    ap.add_argument("--min-coverage", type=float, default=0.03, help="fail a cell below this opaque fraction (default 0.03; 0 disables)")
    ap.add_argument("--posterize", type=int, default=0, help="bits per channel to keep, 0 = off (a later register-matching pass)")
    ap.add_argument("--proof", help="write a labelled proof strip PNG here (every cell + in-frame mocks)")
    ap.add_argument("--frame", type=parse_wh, default=(640, 360), help="game frame WxH for the proof mock (default 640x360)")
    ap.add_argument("--no-ledger", action="store_true", help="do not touch staged.json / docs/asset-manifest-lot.md")
    ap.add_argument("--force", action="store_true", help="rewrite outputs even when up to date")
    ap.add_argument("--dry-run", action="store_true", help="plan only, write nothing")
    args = ap.parse_args()

    lotstage.assert_gitignored(root)
    if args.cell[0] <= 0 or args.cell[1] <= 0 or args.columns <= 0:
        raise SystemExit(f"{TOOL}: cell and columns must be positive")
    if not args.no_ledger and not Path(args.out).resolve().is_relative_to(lotstage.lot_dir(root).resolve()):
        raise SystemExit(f"{TOOL}: --out {args.out} is outside content/art/lot/; the ledger only tracks the staged tree "
                         f"(pass --no-ledger for a one-off elsewhere)")
    if args.job:
        job_dirs = [os.path.join(args.renders, j) for j in args.job]
    else:
        job_dirs = sorted(os.path.join(args.renders, d) for d in (os.listdir(args.renders) if os.path.isdir(args.renders) else [])
                          if d.startswith(JOB_PREFIX) and os.path.isfile(os.path.join(args.renders, d, "frames.json")))
    if not job_dirs:
        raise SystemExit(f"{TOOL}: no {JOB_PREFIX}* job with a frames.json under {args.renders} -- run render-lot.ps1 first")
    if args.name and len(job_dirs) > 1:
        raise SystemExit(f"{TOOL}: --name applies to a single --job")

    rows = []
    for job_dir in job_dirs:
        rows += pack_job(job_dir, args, root)
    if rows and not args.no_ledger:
        rows = merge_tool_rows(root, TOOL, rows)
        lotstage.write_ledger(root, TOOL, rows)
        lotstage.write_manifest_section(
            root, TOOL, "Viewmodel -- first-person arms from Synty rigs + Malbers clips (lot-viewmodel-pack.py)",
            "One sheet per `Weapon` (fists / edged / blunt / evictor), 192x144 cells in a 4-column grid, "
            "hard alpha at 128; the `.json` maps state -> cell indices. Packed from `unity-renders/<job>/` "
            "(render-lot.ps1). Every pixel is a derivative of Synty POLYGON and Malbers Animations content "
            "(asset-store-eula) -- ships in a build, never as raw files in git.", rows)
        print(f"ledger: {lotstage.lot_dir(root) / 'staged.json'} (tools.{TOOL}, {len(rows)} rows); manifest: docs/asset-manifest-lot.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
