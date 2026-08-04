#!/bin/sh
# Copy the built artifacts out of the image onto the bind mount at dist/.
#
# Runs as the entrypoint of the `build` compose service. All the actual
# compiling already happened during `docker compose build`; this step exists
# purely to hand the results to the host.
set -eu

SRC=/artifacts
DEST=${GRANADAD_DIST:-/out}

if [ ! -d "$SRC" ]; then
    echo "FATAL: no artifacts in the image at $SRC" >&2
    exit 1
fi

if [ ! -d "$DEST" ]; then
    echo "FATAL: $DEST is not mounted. Run this through docker compose, which" >&2
    echo "       bind-mounts the repo's dist/ directory there." >&2
    exit 1
fi

# Clear stale output so a renamed or deleted artifact does not linger and get
# mistaken for part of this build.
#
# EXCEPT PNGs, and that exception is a bug fix rather than a convenience. This
# used to be `rm -rf "${DEST:?}/"*`, and dist/ is where a sprint's captured
# frames land and is gitignored -- so running the documented build command
# DELETED the evidence the previous sprint proved itself with, unrecoverably.
# The S3 review lost six frames to it and had to regenerate its own. Anything
# this build produces is still cleared; a picture somebody took is not this
# build's to throw away.
find "${DEST:?}" -mindepth 1 -maxdepth 1 ! -name '*.png' -exec rm -rf {} + 2>/dev/null || true

cp -a "$SRC"/. "$DEST"/

# ---------------------------------------------------------------------------
# WHAT THIS GREEN IS WORTH
# ---------------------------------------------------------------------------
# The compile-and-test layer is one cached RUN. On an unchanged source tree
# docker reuses it, and this command then exits 0 having executed nothing --
# which is a green that proves nothing, and the S3 review caught exactly that.
#
# Docker's own cache key is the source tree, so a reused layer does mean the
# tests passed against THIS tree; it does not mean they ran just now. The gate
# therefore stamps itself, and this prints the stamp so nobody has to guess
# which of the two they are looking at.
if [ ! -f "$DEST/GATE-STAMP.txt" ]; then
    echo "FATAL: no GATE-STAMP.txt in the image. The compile-and-test layer did" >&2
    echo "       not run, or did not finish. This is not a green build." >&2
    exit 1
fi
echo ""
echo "=============================== GATE STAMP ==============================="
cat "$DEST/GATE-STAMP.txt"
echo ""
echo "  If the time above is not NOW, docker reused a cached compile-and-test"
echo "  layer and NOTHING was executed during this invocation. The tests did"
echo "  pass against the source tree whose digest is printed above -- that is"
echo "  what the cache key is -- but 'I ran the gate' means the tree digest"
echo "  matches yours, not that anything ran while you watched."
echo "=========================================================================="

echo ""
echo "Published to dist/:"
echo ""
cd "$DEST"
for f in *; do
    [ -f "$f" ] || continue
    printf '  %-34s %10s bytes\n' "$f" "$(wc -c < "$f" | tr -d ' ')"
done

# printf, not echo. /bin/sh here is dash, whose echo expands backslash escapes
# with no way to turn it off: `echo ".\scripts\verify-windows.ps1"` prints
# ".\scripts<VT>erify-windows.ps1", because \v is a vertical tab. The one thing
# this block exists to do is print a command the owner can copy, so it cannot
# be allowed to mangle Windows paths.
printf '%s\n' ""
printf '%s\n' "The build gate is only HALF done. This container cannot execute a PE"
printf '%s\n' "binary, so the mingw/Windows side of the determinism check - the content"
printf '%s\n' "suite, and the byte-for-byte comparison of the decoded world state"
printf '%s\n' "against content-fingerprint-linux-gcc.txt - has to run on the host:"
printf '%s\n' ""
printf '%s\n' "    powershell -ExecutionPolicy Bypass -File .\\scripts\\verify-windows.ps1"
printf '%s\n' ""
printf '%s\n' "Run the game natively on Windows:"
printf '%s\n' "    .\\dist\\granadad.exe --selftest"
printf '%s\n' "    .\\dist\\granadad.exe"
printf '%s\n' ""
