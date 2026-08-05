"""How much of the frame is interface. See docs/HUD-REAL-ESTATE.md.

INK      pixels the interface actually changed, against the same scene captured
         with --nohud. No estimating: it is a diff.
CLAIMED  the ink mask closed up horizontally by one glyph advance and
         vertically by one pixel row, so the gaps inside and between letters
         belong to the row that owns them, then the bounding box of every
         connected blob unioned. That is the screen real estate a row holds,
         which is the thing you actually lose to a HUD.

Wants <dir>/{before,after,nohud}-{street,talk,roofs}.png and needs Pillow. It
is a measuring tape and not part of the build; nothing in the gate runs it.

    python scripts/hud-real-estate.py docs/frames/polish-1 15 3
"""
import sys
from collections import deque
from PIL import Image

def mask(a_path, b_path):
    a = Image.open(a_path).convert("RGB")
    b = Image.open(b_path).convert("RGB")
    assert a.size == b.size, (a.size, b.size)
    w, h = a.size
    pa, pb = a.load(), b.load()
    m = bytearray(w * h)
    for y in range(h):
        row = y * w
        for x in range(w):
            if pa[x, y] != pb[x, y]:
                m[row + x] = 1
    return m, w, h

def close(m, w, h, gx, gy):
    out = bytearray(m)
    # horizontal
    for y in range(h):
        row = y * w
        runs = [x for x in range(w) if m[row + x]]
        for i in range(len(runs) - 1):
            if runs[i + 1] - runs[i] <= gx:
                for x in range(runs[i], runs[i + 1]):
                    out[row + x] = 1
    # vertical
    src = bytearray(out)
    for x in range(w):
        col = [y for y in range(h) if src[y * w + x]]
        for i in range(len(col) - 1):
            if col[i + 1] - col[i] <= gy:
                for y in range(col[i], col[i + 1]):
                    out[y * w + x] = 1
    return out

def boxes(m, w, h):
    seen = bytearray(w * h)
    out = []
    for start in range(w * h):
        if not m[start] or seen[start]:
            continue
        q = deque([start])
        seen[start] = 1
        x0 = x1 = start % w
        y0 = y1 = start // w
        while q:
            i = q.popleft()
            x, y = i % w, i // w
            x0, x1 = min(x0, x), max(x1, x)
            y0, y1 = min(y0, y), max(y1, y)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    j = ny * w + nx
                    if m[j] and not seen[j]:
                        seen[j] = 1
                        q.append(j)
        out.append((x0, y0, x1, y1))
    return out

def report(label, shot, base, gx, gy):
    m, w, h = mask(shot, base)
    ink = sum(m)
    closed = close(m, w, h, gx, gy)
    union = bytearray(w * h)
    for (x0, y0, x1, y1) in boxes(closed, w, h):
        for y in range(y0, y1 + 1):
            row = y * w
            for x in range(x0, x1 + 1):
                union[row + x] = 1
    claimed = sum(union)
    total = w * h
    print(f"{label:22s} ink {ink:7d} ({100.0*ink/total:5.2f}%)   "
          f"claimed {claimed:7d} ({100.0*claimed/total:5.2f}%)")
    return ink, claimed, total

if __name__ == "__main__":
    d = sys.argv[1]
    gx, gy = int(sys.argv[2]), int(sys.argv[3])
    for scene in ("street", "talk", "roofs"):
        for era in ("before", "after"):
            report(f"{scene} {era}", f"{d}/{era}-{scene}.png", f"{d}/nohud-{scene}.png", gx, gy)
