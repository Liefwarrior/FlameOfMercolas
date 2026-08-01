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
rm -rf "${DEST:?}/"* 2>/dev/null || true

cp -a "$SRC"/. "$DEST"/

echo ""
echo "Published to dist/:"
echo ""
cd "$DEST"
for f in *; do
    [ -f "$f" ] || continue
    printf '  %-28s %10s bytes\n' "$f" "$(wc -c < "$f" | tr -d ' ')"
done
echo ""
echo "The build gate is only HALF done. This container cannot execute a PE"
echo "binary, so the mingw/Windows side of the determinism check — the content"
echo "suite, and the byte-for-byte comparison of the decoded world state"
echo "against content-fingerprint-linux-gcc.txt — has to run on the host:"
echo ""
echo "    powershell -ExecutionPolicy Bypass -File .\\scripts\\verify-windows.ps1"
echo ""
echo "Run the game natively on Windows:"
echo "    .\\dist\\granadad.exe --selftest"
echo "    .\\dist\\granadad.exe"
echo ""
