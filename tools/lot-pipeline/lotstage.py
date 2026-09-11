"""Shared plumbing for the LOT staging tools (select-icons.py, pack-vfx.py, ...).

Every tool that stages Lord of Trojia assets under content/art/lot/ goes through
this module so the two ledgers stay consistent:

  content/art/lot/staged.json     machine ledger, gitignored with the assets.
                                  {"tools": {"<tool>": {"generated": ..., "rows": [...]}}}
                                  One row per staged file: dst, bytes, sha256, src, purpose, license.
  docs/asset-manifest-lot.md      tracked, human ledger. One section per tool between
                                  <!-- lot-pipeline:begin <tool> --> / <!-- lot-pipeline:end <tool> -->
                                  markers; a rerun replaces its own section and leaves the
                                  other lanes' sections alone.

Both are rewritten idempotently: a byte-identical staged tree yields a byte-identical
ledger (timestamps live in a single "generated" field that the diff can ignore).

Licence classes used in the manifest (see docs/design/ASSET-PIPELINE-SPEC.md section 5):
  asset-store-eula   paid Unity Asset Store pack -- ships inside a build, never as raw files in git
  cc0                Kenney and other public-domain packs
  owner              Eli's own bakes
"""

from __future__ import annotations

import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_LOT_ROOT = Path(r"C:\repositories\LordOfTrojia-MVP\Assets")


def repo_root(start: Path | None = None) -> Path:
    """Walk up from `start` (default: this file) to the directory holding .gitignore + content/."""
    p = (start or Path(__file__)).resolve()
    for cand in [p, *p.parents]:
        if (cand / ".gitignore").is_file() and (cand / "content").is_dir():
            return cand
    raise SystemExit("lotstage: could not find the repo root (no .gitignore + content/ above %s)" % p)


def lot_dir(root: Path) -> Path:
    return root / "content" / "art" / "lot"


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def assert_gitignored(root: Path) -> None:
    """Refuse to stage unless /content/art/lot/ is in .gitignore -- the licence guard."""
    gi = (root / ".gitignore").read_text(encoding="utf-8").splitlines()
    if not any(line.strip() in ("/content/art/lot/", "content/art/lot/", "/content/art/lot") for line in gi):
        raise SystemExit("lotstage: /content/art/lot/ is not in .gitignore -- refusing to stage licensed assets")


def rel_dst(root: Path, path: Path) -> str:
    """Path of a staged file relative to content/art/lot/, forward slashes."""
    return path.resolve().relative_to(lot_dir(root).resolve()).as_posix()


def rel_src(lot_root: Path, path: Path) -> str:
    return path.resolve().relative_to(lot_root.resolve()).as_posix()


def make_row(root: Path, lot_root: Path, dst: Path, src: Path | list[Path], purpose: str,
             license_class: str, pack: str, **extra) -> dict:
    srcs = src if isinstance(src, list) else [src]
    row = {
        "pack": pack,
        "src": [rel_src(lot_root, s) for s in srcs] if len(srcs) != 1 else rel_src(lot_root, srcs[0]),
        "dst": rel_dst(root, dst),
        "bytes": dst.stat().st_size,
        "sha256": sha256_of(dst),
        "purpose": purpose,
        "license": license_class,
    }
    row.update(extra)
    return row


# ---------------------------------------------------------------- staged.json

def write_ledger(root: Path, tool: str, rows: list[dict], dry_run: bool = False) -> Path:
    path = lot_dir(root) / "staged.json"
    data = {"schemaVersion": 1, "tools": {}}
    if path.is_file():
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            pass
    data.setdefault("tools", {})
    data["tools"][tool] = {
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "rows": sorted(rows, key=lambda r: r["dst"]),
    }
    if not dry_run:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
    return path


# ------------------------------------------------------ docs/asset-manifest-lot.md

MANIFEST_HEADER = """# LOT asset manifest -- what tools/lot-pipeline stages under content/art/lot/

Generated sections. Each tool under `tools/lot-pipeline/` rewrites its own block between
`lot-pipeline:begin` / `lot-pipeline:end` markers when it runs; do not hand-edit inside them.
`content/art/lot/` is gitignored (`.gitignore:48`): every file listed here is a derivative of a
purchased asset-store pack unless its licence column says otherwise, and none of them are in git.
`content/art/lot/staged.json` is the machine twin of this file (bytes + sha256 per staged file).

Columns: pack | source (relative to the LOT `Assets/`) | staged (relative to `content/art/lot/`) |
bytes | sha256[:12] | purpose | licence.

"""


def write_manifest_section(root: Path, tool: str, title: str, intro: str, rows: list[dict],
                           dry_run: bool = False) -> Path:
    path = root / "docs" / "asset-manifest-lot.md"
    text = path.read_text(encoding="utf-8") if path.is_file() else MANIFEST_HEADER
    begin = f"<!-- lot-pipeline:begin {tool} -->"
    end = f"<!-- lot-pipeline:end {tool} -->"

    lines = [begin, f"## {title}", "", intro.rstrip(), "",
             "| pack | source | staged | bytes | sha256 | purpose | licence |",
             "|---|---|---|---|---|---|---|"]
    for r in sorted(rows, key=lambda r: r["dst"]):
        src = r["src"] if isinstance(r["src"], str) else "<br>".join(r["src"])
        lines.append("| %s | `%s` | `%s` | %d | `%s` | %s | %s |" % (
            r["pack"], src, r["dst"], r["bytes"], r["sha256"][:12],
            r["purpose"].replace("|", "\\|"), r["license"]))
    total = sum(r["bytes"] for r in rows)
    lines += ["", f"{len(rows)} files, {total:,} bytes.", end]
    section = "\n".join(lines) + "\n"

    if begin in text and end in text:
        head = text[: text.index(begin)]
        tail = text[text.index(end) + len(end):].lstrip("\n")
        text = head + section + ("\n" + tail if tail else "")
    else:
        text = text.rstrip("\n") + "\n\n" + section
    if not dry_run:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
    return path


def up_to_date(dst: Path, srcs: list[Path]) -> bool:
    """True when dst exists and is newer than every source (the skip rule; -Force overrides)."""
    if not dst.is_file():
        return False
    m = dst.stat().st_mtime
    return all(s.stat().st_mtime <= m for s in srcs)
