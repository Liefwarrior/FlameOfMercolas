# Shops -- frames (2026-10-09)

Shot headless off the session's own Linux build (raylib `PLATFORM=Memory`,
`OPENGL_VERSION=Software`). The 3D art packs are not in git, so the shops behind the
HUD are the placeholder rule's boxes; read these for the conversation surface, the
Kit page and the copy.

| Frame | Command | What it proves |
|---|---|---|
| `1-brann-wares.png` | `granadad --shop=wares` | Brann at his own counter, in his authored greeting (*"EARLY CUSTOM'S BEST CUSTOM. WHAT'LL IT BE?"*), with SEE THE WARES opened: LANTERN 8C, ROPE 5C, KNIFE 3C, BOAT-HOOK 4C, BOTTLE 1C, each the item's authored royals through the counter's guild term |
| `2-kit-lantern.png` | `granadad --shop=bought --character --tile-page=3` | the lantern in the Kit after the purchase: LOAD 35 / 240 DRAMS, LANTERN 30DR 9C |
| `3-fenner-pawn.png` | `granadad --shop=pawn` | Fenner at his pawn (*"JUST OPENED. SCALE'S CLEAN. TILL'S EMPTY. HOPES ARE UP."*), PAWN SOMETHING open on the one thing he would take: LANTERN 4C |
| `4-sold.png` | `granadad --shop` | the round trip finished: bought for 8C, sold back for 4C, the purse 40 -> 36 |

`--shop` owes six beats (Brann, the shelf, the purchase, Fenner, his list, the sale)
and exits non-zero short; each purchase and sale is checked against the Kit, the
player's purse AND the keeper's purse, before and after.
