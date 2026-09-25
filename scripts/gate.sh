#!/bin/sh
# Granadad: The Darkstreets -- the docker half of the gate, with the commit stamped.
#
#     scripts/gate.sh
#
# is `docker compose run --rm --build build`, run from the repo root with
# GRANADAD_REVISION set to `git rev-parse --short HEAD`, plus -dirty when a
# tracked file differs from HEAD -- the rule `git describe --dirty` uses. The
# container cannot work that out for itself: .git is not in the build context
# (see .dockerignore), so without this dist/GATE-STAMP.txt and
# `granadad.exe --version` say `unknown`. scripts/gate.ps1 is the same thing
# on Windows. Anything else -- BUILD_TYPE, say -- passes through the
# environment as before. The exit code is the compose command's.
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

rev="$(git rev-parse --short HEAD)" || {
    echo "FAIL: git cannot name HEAD here, so there is no revision to stamp." >&2
    echo "      Run \`docker compose run --rm --build build\` by hand if you mean to build without one." >&2
    exit 1
}
# Refresh stat info first so a touched-but-unchanged file does not read as
# dirty; then ask whether any tracked file differs from HEAD, staged or not.
# Untracked files do not count, same as `git describe --dirty`.
git update-index -q --refresh >/dev/null || true
if ! git diff-index --quiet HEAD --; then
    rev="$rev-dirty"
fi

echo "=== granadad: gate for revision $rev ==="
GRANADAD_REVISION="$rev" exec docker compose run --rm --build build
