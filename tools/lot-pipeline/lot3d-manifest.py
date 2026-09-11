"""lot3d-manifest.py -- verify the 3D exports under content/art/lot-3d/ and write their ledgers.

render-lot.ps1 -Mode gltf|glb (tools/lot-pipeline/unity/) leaves per-job manifests behind:
  <out>/static/manifest.json        one record per static prefab -> .gltf + .bin (+ atlas PNGs)
  <out>/characters/manifest.json    one record per rig -> .glb with its baked clips
This step reads those, opens every file it names, checks it is structurally what the C++
loaders expect (glTF JSON parses, buffers/images resolve and have the declared byte length;
GLB header + chunk lengths agree with the file size, exactly one skin, animations present in
the job's order) and writes:
  <out>/manifest.json               combined machine ledger (+ sha256 per file)
  <out>/MANIFEST.md                 prefab -> file -> licence, clip tables, the licence note
  docs/asset-manifest-lot.md        the tracked ledger section  <!-- lot-pipeline:begin lot3d -->
Exit 1 when anything named in a manifest is missing or malformed.

  python tools/lot-pipeline/lot3d-manifest.py --out C:/repositories/fom-3d/content/art/lot-3d
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lotstage  # noqa: E402

TOOL = "lot3d"
LICENCE = "asset-store-eula"


# ---------------------------------------------------------------- readers

def read_glb(path: Path) -> tuple[dict, int, int]:
    """Parse a .glb: returns (json, json chunk bytes, bin chunk bytes). Raises on a bad container."""
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError("too short to be a glb")
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF":
        raise ValueError("bad magic %r" % magic)
    if version != 2:
        raise ValueError("glTF version %d, want 2" % version)
    if length != len(data):
        raise ValueError("header length %d != file size %d" % (length, len(data)))
    off, chunks = 12, []
    while off + 8 <= len(data):
        clen, ctype = struct.unpack_from("<I4s", data, off)
        off += 8
        if off + clen > len(data):
            raise ValueError("chunk %r overruns the file" % ctype)
        chunks.append((ctype, data[off:off + clen]))
        off += clen
    if not chunks or chunks[0][0] != b"JSON":
        raise ValueError("first chunk is not JSON")
    js = json.loads(chunks[0][1].decode("utf-8"))
    binlen = len(chunks[1][1]) if len(chunks) > 1 and chunks[1][0] == b"BIN\0" else 0
    return js, len(chunks[0][1]), binlen


def read_gltf(path: Path) -> tuple[dict, list[Path]]:
    """Parse a .gltf and resolve its external buffers/images. Returns (json, referenced files)."""
    js = json.loads(path.read_text(encoding="utf-8"))
    refs: list[Path] = []
    for buf in js.get("buffers", []):
        uri = buf.get("uri")
        if not uri or uri.startswith("data:"):
            continue
        f = path.parent / uri
        if not f.is_file():
            raise ValueError("buffer %s missing" % uri)
        if f.stat().st_size != buf.get("byteLength", -1):
            raise ValueError("buffer %s is %d bytes, declared %d" % (uri, f.stat().st_size, buf.get("byteLength")))
        refs.append(f)
    for img in js.get("images", []):
        uri = img.get("uri")
        if not uri or uri.startswith("data:"):
            continue
        f = path.parent / uri
        if not f.is_file():
            raise ValueError("image %s missing" % uri)
        refs.append(f)
    return js, refs


def describe(js: dict) -> dict:
    accessors = js.get("accessors", [])
    tris = 0
    prims = 0
    for mesh in js.get("meshes", []):
        for p in mesh.get("primitives", []):
            prims += 1
            idx = p.get("indices")
            if idx is not None and idx < len(accessors):
                tris += accessors[idx].get("count", 0) // 3
    anims = []
    for a in js.get("animations", []):
        dur = 0.0
        for s in a.get("samplers", []):
            acc = accessors[s["input"]] if s.get("input") is not None and s["input"] < len(accessors) else {}
            mx = acc.get("max") or [0.0]
            dur = max(dur, float(mx[0]))
        anims.append({"name": a.get("name", ""), "channels": len(a.get("channels", [])), "duration": round(dur, 4)})
    skins = js.get("skins", [])
    return {
        "nodes": len(js.get("nodes", [])),
        "meshes": len(js.get("meshes", [])),
        "primitives": prims,
        "triangles": tris,
        "materials": [m.get("name", "") for m in js.get("materials", [])],
        "images": [i.get("uri", "(embedded)") for i in js.get("images", [])],
        "skins": len(skins),
        "joints": len(skins[0].get("joints", [])) if skins else 0,
        "animations": anims,
        "extensionsUsed": js.get("extensionsUsed", []),
    }


# ---------------------------------------------------------------- the pass

def run(out: Path, root: Path, lot_root: Path, dry_run: bool) -> int:
    problems: list[str] = []
    assets: list[dict] = []          # combined ledger records
    rows: list[dict] = []            # docs rows
    md: list[str] = []
    atlases: dict[Path, dict] = {}   # atlas file -> first pack that referenced it

    def rel(p: Path) -> str:
        return p.resolve().relative_to(out.resolve()).as_posix()

    def src_rel(asset_path: str) -> str:
        # manifest sources are Unity asset paths ("Assets/Synty/..."); the docs table is relative to <LOT>/Assets
        return asset_path[len("Assets/"):] if asset_path.startswith("Assets/") else asset_path

    # ---- static
    static_manifest = out / "static" / "manifest.json"
    if static_manifest.is_file():
        job = json.loads(static_manifest.read_text(encoding="utf-8"))
        md += ["## Static set -- `static/<pack>/<prefab>.gltf` + `.bin` (glTFast, job `%s`)" % job.get("jobName", ""), "",
               "One `.gltf` + `.bin` per Synty prefab, materials flattened to base-colour-only, the pack atlas copied once per pack folder. "
               "Coordinates: %s." % job.get("coordinates", ""), "",
               "| prefab | role | file | tris | bounds (glTF, m) | materials | licence |", "|---|---|---|---|---|---|---|"]
        for a in job.get("assets", []):
            f = static_manifest.parent / a["file"]     # "file" is relative to the manifest's own folder (<out>/static/)
            try:
                js, refs = read_gltf(f)
                d = describe(js)
            except (OSError, ValueError, json.JSONDecodeError) as e:
                problems.append("%s: %s" % (a["file"], e))
                continue
            for r in refs:
                if r.suffix.lower() in (".png", ".jpg", ".jpeg", ".tga") and r not in atlases:
                    atlases[r] = {"pack": a["pack"], "texture": next((m["texture"] for m in a.get("materials", []) if m.get("texture")), "")}
            bg = a.get("boundsGltf", {})
            bounds = "%s .. %s" % (bg.get("min"), bg.get("max"))
            record = dict(a)
            record.update({"sha256": lotstage.sha256_of(f), "binSha256": lotstage.sha256_of(f.with_suffix(".bin")) if f.with_suffix(".bin").is_file() else "",
                           "gltf": d, "license": LICENCE})
            assets.append(record)
            rows.append({"pack": a["pack"], "src": src_rel(a["source"]), "dst": rel(f), "bytes": a["bytes"], "sha256": record["sha256"],
                         "purpose": "%s %s, %d tris (+ .bin)" % (a["role"], a["name"], d["triangles"]), "license": LICENCE})
            md.append("| `%s` | %s | `%s` | %d | %s | %s | %s |" % (
                a["name"], a["role"], rel(f), d["triangles"], bounds, ", ".join(d["materials"]), LICENCE))
        md.append("")
        sheet = out / "static" / "contact-sheet.png"
        if sheet.is_file():
            md.append("Contact sheet: `static/contact-sheet.png` (cell names in `contact-sheet.json`).")
            md.append("")

    # ---- characters
    rig_manifest = out / "characters" / "manifest.json"
    if rig_manifest.is_file():
        job = json.loads(rig_manifest.read_text(encoding="utf-8"))
        md += ["## Rigs -- `characters/<name>.glb` (UnityGLTF, job `%s`)" % job.get("jobName", ""), "",
               "One `.glb` per humanoid: a single skin over one bone list, textures embedded, every clip baked to node TRS at 30 fps "
               "(LINEAR samplers, no KHR_animation_pointer). Animation index = the job's clip order = the C++ enum; names are the job's names.", ""]
        for a in job.get("assets", []):
            f = rig_manifest.parent / a["file"]        # relative to <out>/characters/
            try:
                js, json_len, bin_len = read_glb(f)
                d = describe(js)
            except (OSError, ValueError, json.JSONDecodeError) as e:
                problems.append("%s: %s" % (a["file"], e))
                continue
            if d["skins"] != 1:
                problems.append("%s: %d skins (raylib reads exactly one)" % (a["file"], d["skins"]))
            want = [c["name"] for c in a.get("clips", [])]
            got = [x["name"] for x in d["animations"]]
            if got != want:
                problems.append("%s: animation order %s != job order %s" % (a["file"], got, want))
            if d["joints"] > 128:
                problems.append("%s: %d joints > raylib's 128-bone GPU skinning cap" % (a["file"], d["joints"]))
            record = dict(a)
            record.update({"sha256": lotstage.sha256_of(f), "gltf": d, "license": LICENCE})
            assets.append(record)
            rows.append({"pack": a["source"].split("/")[2] if a["source"].count("/") >= 2 else "", "src": src_rel(a["source"]), "dst": rel(f),
                         "bytes": a["bytes"], "sha256": record["sha256"],
                         "purpose": "%s %s: %d bones, %d tris, clips %s" % (a["role"], a["name"], d["joints"], d["triangles"], "/".join(got)),
                         "license": LICENCE})
            md += ["### `%s` -- `%s`" % (a["name"], rel(f)), "",
                   "- source: `%s` (avatar `%s`)%s" % (a["source"], a.get("avatarModel", ""), (", weapon `%s`" % a["weapon"]) if a.get("weapon") else ""),
                   "- %d joints (root `%s`), %d tris, %d materials, %d embedded images, %s bytes; glTF bounds %s .. %s" % (
                       d["joints"], a.get("rootBone", ""), d["triangles"], len(d["materials"]), len(d["images"]), format(a["bytes"], ","),
                       a.get("boundsGltf", {}).get("min"), a.get("boundsGltf", {}).get("max")),
                   "", "| index | animation | Malbers source | clip | length s | channels | loop |", "|---|---|---|---|---|---|---|"]
            for c in a.get("clips", []):
                anim = d["animations"][c["index"]] if c["index"] < len(d["animations"]) else {"channels": 0, "duration": 0}
                md.append("| %d | `%s` | `%s` | %s | %.3f | %d | %s |" % (
                    c["index"], c["name"], src_rel(c["source"]), c["clipName"], c["length"], anim["channels"], "yes" if c.get("loop") else "no"))
            md.append("")
        sheet = out / "characters" / "contact-sheet.png"
        if sheet.is_file():
            md.append("Contact sheet: `characters/contact-sheet.png` (cell names in `contact-sheet.json`).")
            md.append("")

    # ---- atlases (one row each; they are the licensed pixels)
    if atlases:
        md += ["## Atlases", "", "| file | pack | source texture | bytes | licence |", "|---|---|---|---|---|"]
        for f, info in sorted(atlases.items(), key=lambda kv: rel(kv[0])):
            sha = lotstage.sha256_of(f)
            assets.append({"name": f.name, "pack": info["pack"], "role": "atlas", "source": info["texture"], "file": rel(f),
                           "bytes": f.stat().st_size, "sha256": sha, "license": LICENCE})
            rows.append({"pack": info["pack"], "src": src_rel(info["texture"]) or f.name, "dst": rel(f), "bytes": f.stat().st_size,
                         "sha256": sha, "purpose": "atlas shared by every %s .gltf in the folder" % info["pack"], "license": LICENCE})
            md.append("| `%s` | %s | `%s` | %s | %s |" % (rel(f), info["pack"], info["texture"], format(f.stat().st_size, ","), LICENCE))
        md.append("")

    if not assets and not problems:
        print("lot3d-manifest: nothing under %s yet (no static/manifest.json or characters/manifest.json)" % out)
        return 1

    for p in problems:
        print("PROBLEM: " + p)

    header = [
        "# LOT 3D manifest -- content/art/lot-3d/",
        "",
        "Generated by `tools/lot-pipeline/lot3d-manifest.py` after `render-lot.ps1 -Mode gltf|glb`; do not hand-edit.",
        "Every file here is a derivative of a purchased asset-store pack (Synty POLYGON meshes/atlases, Malbers Animations clips "
        "baked into the rigs): licence `%s` -- ships inside a built game, never as raw files in a public repo. "
        "`content/art/lot-3d/` is gitignored; `manifest.json` beside this file is the machine twin (sha256 per file)." % LICENCE,
        "",
        "%d assets, %s bytes%s." % (len(assets), format(sum(int(a.get("bytes", 0)) for a in assets), ","),
                                  ("; %d PROBLEMS (see the tool output)" % len(problems)) if problems else ""),
        "",
    ]
    if not dry_run:
        out.mkdir(parents=True, exist_ok=True)
        (out / "MANIFEST.md").write_text("\n".join(header + md), encoding="utf-8", newline="\n")
        (out / "manifest.json").write_text(json.dumps({
            "schemaVersion": 1, "tool": TOOL, "license": LICENCE,
            "provenance": "Exported by tools/lot-pipeline/unity/LotSpriteRenderer.cs from Lord of Trojia assets (licensed, not redistributable); ledger by lot3d-manifest.py",
            "problems": problems, "assets": sorted(assets, key=lambda a: a["file"]),
        }, indent=2) + "\n", encoding="utf-8", newline="\n")
    docs = lotstage.write_manifest_section(
        root, TOOL, "3D exports -- content/art/lot-3d/ (render-lot.ps1 -Mode gltf|glb + lot3d-manifest.py)",
        "Synty POLYGON prefabs as `.gltf`+`.bin` (one atlas copy per pack folder) and Synty humanoids as `.glb` with Malbers clips "
        "baked onto their own bones (UnityGLTF), for the raylib renderer. Staged paths are relative to `content/art/lot-3d/` "
        "(not `content/art/lot/`); the `.gltf` rows' bytes include the sibling `.bin`, their sha256 is the `.gltf` JSON. "
        "Licence: asset-store-eula -- inside a build yes, raw in the public repo no. Jobs: `tools/lot-pipeline/unity/jobs/docks-3d-*.json`.",
        rows, dry_run=dry_run)
    print("lot3d-manifest: %d assets, %d rows -> %s, %s, %s" % (len(assets), len(rows), out / "MANIFEST.md", out / "manifest.json", docs))
    return 1 if problems else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", type=Path, default=None, help="the lot-3d output root (default: <repo>/content/art/lot-3d)")
    ap.add_argument("--repo", type=Path, default=None, help="repo whose docs/asset-manifest-lot.md gets the section (default: this file's repo)")
    ap.add_argument("--lot-root", type=Path, default=lotstage.DEFAULT_LOT_ROOT)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    root = args.repo.resolve() if args.repo else lotstage.repo_root()
    out = args.out.resolve() if args.out else root / "content" / "art" / "lot-3d"
    if not out.is_dir():
        print("lot3d-manifest: no such output root: %s" % out)
        return 1
    return run(out, root, args.lot_root, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
