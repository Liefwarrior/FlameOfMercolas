#!/usr/bin/env bash
# Reshoot the phase 8 set. Usage:
#   shoot.sh BEFORE_BINARY AFTER_BINARY   (both a Linux `granadad`, headless on Mesa llvmpipe)
# BEFORE is `main` at 128af014 (after phase 7), AFTER is this branch. Every frame is the same command
# line, 960x540, scale 1, drawn by the real verbs of the named scripted line; the run prints its own
# summary and `granadad` exits non-zero when a line falls short.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
before="$1"
after="$2"
shoot() { # binary outdir name flags...
  local bin="$1" out="$2" name="$3"; shift 3
  "$bin" --smoke=0 --hold --width=960 --height=540 --scale=1 "$@" --screenshot="$out/$name.png" 2>&1 \
    | grep -a "pack beats\|kit beats"
}
# THE COMPARISON: the same state, `--kit=equip` (coat on the back, knife in the hand, the rope, lantern
# and picks in the pack), before on the sheet and after on the pack.
shoot "$before" "$here/before" equip-sheet --kit=equip
shoot "$after"  "$here/after"  equip-sheet --kit=equip
shoot "$after"  "$here/after"  equip-pack  --kit=pack
# The pack line, after only (the line does not exist before).
shoot "$after" "$here/after" pack-take   --pack=take --settle-steps=0
shoot "$after" "$here/after" pack-theirs --pack=theirs --settle-steps=0
shoot "$after" "$here/after" pack-list   --pack=list
shoot "$after" "$here/after" pack-use    --pack=use
shoot "$after" "$here/after" pack-ate    --pack=ate
shoot "$after" "$here/after" pack-load   --pack=load
