#!/usr/bin/env python3
"""lot-viewmodel-silhouette.py - code-drawn first-person viewmodel sheet, the
zero-licence stand-in for the Unity-rendered LOT sheet.

Same schema, cell size and cell order as lot-viewmodel-pack.py's output
(content/art/lot/viewmodel/<weapon>.png + .json), so the renderer needs ONE
code path -- draw whichever sheet is staged -- and the Synty render replaces
this byte-for-byte the moment render-lot.ps1 has run. It draws two-tone arms
and fists in the MERCOLAS-24 palette (the actor sprites' own ramp: pale skin
E4/B1/B2, coat cuff G2/G3, N0 outline by 4-adjacent expansion) for the twelve
combat states of ASSET-PIPELINE-SPEC section 3.5:

    idle, charge, swing x3, hard_swing x4, block, cast, hit

Poses are authored as numbers below (fist centre, scale, kind) -- a fist that
punches AWAY from the eye gets smaller and drifts to the centre, one that winds
up gets bigger and drops, the guard crosses both forearms over the face, the
cast opens the right hand, the hit jolts both hands down and to the right (the
same edge kTakenJoltBam nudges the camera on).

ORIGINAL pixel art from code: no LOT pixels, redistributable. Written beside
the LOT sheets as <weapon>-silhouette.png/.json so the two never collide;
feel over fidelity -- this proves the LAYER (size, anchor, state machine)
while the Synty render waits on a Unity licence sign-in.

Deterministic: byte-identical rerun (no RNG, no timestamps).

Usage:
    python tools/lot-pipeline/lot-viewmodel-silhouette.py --proof out.png
    python tools/lot-pipeline/lot-viewmodel-silhouette.py --out some/dir --no-ledger
"""

import argparse
import importlib.util
import math
import os
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True   # no __pycache__ beside the tools
import lotstage  # noqa: E402


def _load_packer():
    """lot-viewmodel-pack.py has a hyphen in its name, so it is loaded by path."""
    spec = importlib.util.spec_from_file_location("lot_viewmodel_pack", os.path.join(HERE, "lot-viewmodel-pack.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


PACK = _load_packer()
TOOL = "lot-viewmodel-silhouette"
LICENSE = "original"
CELL = (192, 144)
COLUMNS = 4

# MERCOLAS-24 entries used (tools/scripts/gen_actor_sprites.py PAL).
N0 = (0x0D, 0x0B, 0x10)
G2 = (0x3F, 0x3E, 0x47)
G3 = (0x57, 0x56, 0x5F)
E4 = (0x70, 0x57, 0x3A)   # pale skin shadow
B1 = (0xC9, 0xC2, 0xB0)   # pale skin base
B2 = (0xE4, 0xDC, 0xC6)   # pale skin light

# --------------------------------------------------------------------------
# poses: (state, frame, {hand: (cx, cy, scale, kind)}, jolt)
#   hand "L"/"R"; cx,cy = fist centre in cell px; scale 1.0 = guard distance;
#   kind "fist" | "open"; jolt = (dx, dy) applied to both hands.
# Draw order is L then R (R nearer), except block where the crossed R goes under.
# --------------------------------------------------------------------------

POSES = [
    ("idle",       0, {"L": (58, 100, 1.18, "fist"), "R": (136, 104, 1.18, "fist")}, (0, 0)),
    ("charge",     0, {"L": (54, 108, 1.22, "fist"), "R": (144, 116, 1.30, "fist")}, (0, 0)),
    ("swing",      0, {"L": (58, 100, 1.18, "fist"), "R": (120, 84, 0.95, "fist")}, (0, 0)),
    ("swing",      1, {"L": (58, 102, 1.18, "fist"), "R": (102, 60, 0.62, "fist")}, (0, 0)),
    ("swing",      2, {"L": (58, 100, 1.18, "fist"), "R": (128, 94, 0.95, "fist")}, (0, 0)),
    ("hard_swing", 0, {"L": (48, 116, 1.34, "fist"), "R": (138, 104, 1.18, "fist")}, (0, 0)),
    ("hard_swing", 1, {"L": (84, 78, 0.95, "fist"), "R": (138, 104, 1.18, "fist")}, (0, 0)),
    ("hard_swing", 2, {"L": (106, 52, 0.58, "fist"), "R": (138, 104, 1.18, "fist")}, (0, 0)),
    ("hard_swing", 3, {"L": (70, 94, 1.00, "fist"), "R": (138, 104, 1.18, "fist")}, (0, 0)),
    ("block",      0, {"R": (78, 48, 1.00, "fist"), "L": (114, 44, 1.00, "fist")}, (0, 0)),
    ("cast",       0, {"L": (58, 100, 1.18, "fist"), "R": (114, 68, 0.95, "open")}, (0, 0)),
    ("hit",        0, {"L": (58, 100, 1.18, "fist"), "R": (136, 104, 1.18, "fist")}, (7, 9)),
]

BASE_X = {"L": 34, "R": 158}   # where each forearm leaves the bottom edge


# --------------------------------------------------------------------------
# drawing
# --------------------------------------------------------------------------

def _unit(dx, dy):
    n = math.hypot(dx, dy) or 1.0
    return dx / n, dy / n


def draw_forearm(d, side, cx, cy, s):
    """Tapered quad from below the bottom edge to the wrist, lit half + shadow
    half, with a coat cuff over the first third."""
    bx, by = BASE_X[side], CELL[1] + 12
    ux, uy = _unit(cx - bx, cy - by)
    nx, ny = -uy, ux
    wrist = (cx - ux * 15 * s, cy - uy * 15 * s)
    bw, ww = 21.0, 13.0 * s
    length = math.hypot(wrist[0] - bx, wrist[1] - by)

    def at(t, w):
        px, py = bx + ux * length * t, by + uy * length * t
        return px, py, w

    # light falls from the top-left: the +n side is lit for the left arm, the -n side for the right
    lit = 1.0 if side == "L" else -1.0
    top, wr = at(0.0, bw), at(1.0, ww)
    axis = [(top[0], top[1]), (wr[0], wr[1])]
    for sign, colour in ((lit, B1), (-lit, E4)):
        d.polygon([axis[0], (top[0] + nx * bw * sign, top[1] + ny * bw * sign),
                   (wr[0] + nx * ww * sign, wr[1] + ny * ww * sign), axis[1]], fill=colour)
    # highlight stripe along the lit edge
    d.line([(top[0] + nx * bw * lit * 0.75, top[1] + ny * bw * lit * 0.75),
            (wr[0] + nx * ww * lit * 0.7, wr[1] + ny * ww * lit * 0.7)], fill=B2, width=max(2, int(3 * s)))
    # coat cuff over t in [0, 0.30]
    c0, c1 = at(0.0, bw), at(0.30, bw - (bw - ww) * 0.30)
    d.polygon([(c0[0] + nx * c0[2], c0[1] + ny * c0[2]), (c0[0] - nx * c0[2], c0[1] - ny * c0[2]),
               (c1[0] - nx * c1[2], c1[1] - ny * c1[2]), (c1[0] + nx * c1[2], c1[1] + ny * c1[2])], fill=G2)
    d.line([(c1[0] + nx * c1[2], c1[1] + ny * c1[2]), (c1[0] - nx * c1[2], c1[1] - ny * c1[2])], fill=G3, width=3)


def draw_fist(d, side, cx, cy, s):
    rx, ry = 18 * s, 15 * s
    inner = -1.0 if side == "R" else 1.0      # toward the frame centre
    d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=E4)
    d.ellipse([cx - rx + 2 * s, cy - ry + 1 * s, cx + rx - 4 * s, cy + ry - 5 * s], fill=B1)
    # knuckle highlights along the top, finger seams below them
    for k in range(4):
        kx = cx - 12 * s + k * 8 * s
        d.ellipse([kx - 2.6 * s, cy - 10 * s, kx + 2.6 * s, cy - 6 * s], fill=B2)
        if k:
            sx = cx - 8 * s + (k - 1) * 8 * s
            d.line([(sx, cy - 3 * s), (sx, cy + 7 * s)], fill=E4, width=max(1, int(1.5 * s)))
    # thumb on the inner side
    tx = cx + inner * 15 * s
    d.ellipse([tx - 6 * s, cy - 2 * s, tx + 6 * s, cy + 9 * s], fill=B1)
    d.arc([tx - 6 * s, cy - 2 * s, tx + 6 * s, cy + 9 * s], 20, 200, fill=E4, width=max(1, int(1.5 * s)))


def draw_open_hand(d, side, cx, cy, s):
    inner = -1.0 if side == "R" else 1.0
    d.ellipse([cx - 14 * s, cy - 8 * s, cx + 14 * s, cy + 16 * s], fill=E4)
    d.ellipse([cx - 12 * s, cy - 8 * s, cx + 11 * s, cy + 12 * s], fill=B1)
    for k, h in enumerate((18, 22, 21, 17)):
        fx = cx - 12 * s + k * 7.5 * s
        d.rounded_rectangle([fx, cy - 8 * s - h * s, fx + 5.5 * s, cy - 2 * s], radius=2.5 * s, fill=B1, outline=E4, width=1)
        d.line([(fx + 1, cy - 8 * s - h * s + 2), (fx + 1, cy - 6 * s)], fill=B2, width=1)
    tx = cx + inner * 16 * s
    d.rounded_rectangle([min(tx, cx + inner * 8 * s), cy - 4 * s, max(tx, cx + inner * 8 * s), cy + 5 * s], radius=3 * s, fill=B1, outline=E4, width=1)


def draw_cell(pose):
    state, frame, hands, jolt = pose
    im = Image.new("RGBA", CELL, (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    order = ["R", "L"] if state == "block" else ["L", "R"]
    for side in order:
        if side not in hands:
            continue
        cx, cy, s, kind = hands[side]
        cx, cy = cx + jolt[0], cy + jolt[1]
        draw_forearm(d, side, cx, cy, s)
        (draw_open_hand if kind == "open" else draw_fist)(d, side, cx, cy, s)
    # N0 outline by 4-adjacent expansion (the actor sprites' rule), then a hard alpha
    a = im.split()[3].point(lambda v: 255 if v >= 128 else 0)
    grown = a
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        grown = ImageChops.lighter(grown, ImageChops.offset(a, dx, dy))
    outline = ImageChops.subtract(grown, a)
    # ImageChops.offset wraps: an arm leaving the bottom edge must not grow an
    # outline along the top row, so the expansion stops one pixel inside the cell
    edge = Image.new("L", CELL, 0)
    ImageDraw.Draw(edge).rectangle([1, 1, CELL[0] - 2, CELL[1] - 2], fill=255)
    outline = ImageChops.multiply(outline, edge)
    rgb = Image.composite(im.convert("RGB"), Image.new("RGB", CELL, (0, 0, 0)), a)
    rgb = Image.composite(Image.new("RGB", CELL, N0), rgb, outline)
    alpha = ImageChops.lighter(a, outline)
    return Image.merge("RGBA", (*rgb.split(), alpha))


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    root = lotstage.repo_root()
    ap.add_argument("--out", default=str(lotstage.lot_dir(root) / "viewmodel"), help="sheet output dir (default: content/art/lot/viewmodel)")
    ap.add_argument("--weapon", default="fists", help="sheet name; output is <weapon>-silhouette.png/.json (default fists)")
    ap.add_argument("--proof", help="write a labelled proof strip PNG here")
    ap.add_argument("--frame", type=PACK.parse_wh, default=(640, 360), help="game frame WxH for the proof mock (default 640x360)")
    ap.add_argument("--no-ledger", action="store_true", help="do not touch staged.json / docs/asset-manifest-lot.md")
    ap.add_argument("--dry-run", action="store_true", help="plan only, write nothing")
    args = ap.parse_args()

    lotstage.assert_gitignored(root)
    if not args.no_ledger and not Path(args.out).resolve().is_relative_to(lotstage.lot_dir(root).resolve()):
        raise SystemExit(f"{TOOL}: --out {args.out} is outside content/art/lot/; the ledger only tracks the staged tree "
                         f"(pass --no-ledger for a one-off elsewhere)")
    name = f"{args.weapon}-silhouette"
    png_out = os.path.join(args.out, name + ".png")
    json_out = os.path.join(args.out, name + ".json")
    cw, ch = CELL
    rows = int(math.ceil(len(POSES) / float(COLUMNS)))
    print(f"{name}: {len(POSES)} cells -> {name}.png ({COLUMNS * cw}x{rows * ch})")
    if args.dry_run:
        return 0

    cells, images, meta, states = [], [], [], {}
    for i, pose in enumerate(POSES):
        state, frame = pose[0], pose[1]
        im = draw_cell(pose)
        coverage = im.split()[3].histogram()[255] / float(cw * ch)
        cells.append((state, frame, {}))
        images.append(im)
        states.setdefault(state, []).append(i)
        meta.append({"index": i, "state": state, "frame": frame, "x": (i % COLUMNS) * cw, "y": (i // COLUMNS) * ch,
                     "src": "", "clip": "", "time": 0.0, "coverage": round(coverage, 4)})
        print(f"  [{i:2d}] {state:<10} {frame}  coverage {coverage:.1%}")

    sheet = Image.new("RGBA", (COLUMNS * cw, rows * ch), (0, 0, 0, 0))
    for m, im in zip(meta, images):
        sheet.paste(im, (m["x"], m["y"]))
    os.makedirs(args.out, exist_ok=True)
    sheet.save(png_out, "PNG", optimize=False, compress_level=9)
    PACK.write_sheet_index(json_out, {
        "schemaVersion": 1,
        "provenance": f"Drawn by tools/lot-pipeline/{TOOL}.py (original code-drawn silhouettes, MERCOLAS-24, redistributable) -- rerun the tool; do not hand-edit",
        "weapon": args.weapon,
        "sheet": name + ".png",
        "cell": [cw, ch],
        "columns": COLUMNS,
        "alphaCutoff": 128,
        "posterize": 0,
        "states": states,
        "cells": meta,
        "source": {"job": "", "unity": "", "rig": "", "weaponPrefab": "", "clips": [], "generator": TOOL + ".py"},
    })
    print(f"  wrote {png_out} and {os.path.basename(json_out)}")

    if args.proof:
        PACK.write_proof(args.proof, f"viewmodel proof: {args.weapon} SILHOUETTE FALLBACK (code-drawn, {TOOL}.py; not the Synty render) "
                                     f"cell {cw}x{ch}", CELL, cells, images, args.frame)
        print(f"  proof strip: {args.proof}")

    if not args.no_ledger:
        # not a LOT asset, so the row is built by hand in lotstage's shape (the source
        # column names the generator; make_row would want a path under LOT/Assets)
        def row(path, purpose):
            return {"pack": "(none: code-drawn)", "src": f"tools/lot-pipeline/{TOOL}.py", "dst": lotstage.rel_dst(root, Path(path)),
                    "bytes": os.path.getsize(path), "sha256": lotstage.sha256_of(Path(path)), "purpose": purpose, "license": LICENSE}
        rows_ = PACK.merge_tool_rows(root, TOOL, [
            row(png_out, f"first-person viewmodel sheet '{args.weapon}', code-drawn silhouette fallback: {len(POSES)} cells, states " + "/".join(states)),
            row(json_out, f"cell index for {name}.png (state -> cell indices)"),
        ])
        lotstage.write_ledger(root, TOOL, rows_)
        lotstage.write_manifest_section(
            root, TOOL, "Viewmodel -- code-drawn silhouette fallback (lot-viewmodel-silhouette.py)",
            "Same schema, cell size and cell order as the Synty sheets, drawn from code in MERCOLAS-24: no LOT "
            "pixels, redistributable (licence `original`). Written as `<weapon>-silhouette.*` beside the LOT sheets "
            "so the two never collide; staged under `lot/` for now -- the owner may move the pair into git if a "
            "clean checkout should carry a fallback sheet.", rows_)
        print(f"ledger: {lotstage.lot_dir(root) / 'staged.json'} (tools.{TOOL}); manifest: docs/asset-manifest-lot.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
