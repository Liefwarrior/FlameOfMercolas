#!/bin/bash
# usage: shoot.sh <granadad binary> <out dir> [jobs]
#
# Runs every row of lines.txt (one scripted --screenshot line of `granadad --help`
# per row) through <binary> and leaves, per row, NAME.png and NAME.log in <out dir>,
# plus results.tsv: name, exit code, sha256 of the PNG, the 3D shutter's scene hash.
# Run it once on the base commit's binary and once on the branch's, then diff the two
# results.tsv files.
set -u
bin=$(readlink -f "$1"); out=$2; jobs=${3:-8}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
export GRANADAD_CONTENT_DIR="${GRANADAD_CONTENT_DIR:-$root/content}"
mkdir -p "$out"; out=$(readlink -f "$out"); rm -f "$out"/results.tsv
run_one() {
    name=$1; shift
    ( cd "$out" && eval "timeout 600 \"$bin\" --smoke=0 --hold --width=640 --height=360 --scale=2 $* --screenshot=$name.png" > "$name.log" 2>&1 )
    code=$?
    sha=-; [ -f "$out/$name.png" ] && sha=$(sha256sum "$out/$name.png" | cut -c1-16)
    scene=$(grep -o 'scene 0x[0-9A-F]*' "$out/$name.log" | head -1 | cut -d' ' -f2)
    printf '%s\t%s\t%s\t%s\n' "$name" "$code" "$sha" "${scene:--}"
}
export -f run_one; export bin out
grep -v '^#' "$here/lines.txt" | grep . | while IFS='|' read -r name args; do
    name=$(echo "$name" | xargs); printf '%s\0%s\0' "$name" "$args"
done | xargs -0 -n2 -P "$jobs" bash -c 'run_one "$0" $1' | sort > "$out/results.tsv"
awk -F'\t' '$2!=0{bad++} END{printf "%d rows, %d with a non-zero exit\n", NR, bad}' "$out/results.tsv"
