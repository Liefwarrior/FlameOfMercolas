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
