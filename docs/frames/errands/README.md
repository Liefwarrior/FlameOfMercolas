# Errands a human can finish -- frames (2026-10-09)

Shot headless off the session's own Linux build (raylib `PLATFORM=Memory`,
`OPENGL_VERSION=Software`, commit noted in the session log). The 3D art packs are not
in git, so the world behind the HUD is the placeholder rule's boxes; read these for the
HUD, the copy and the layout, not the scene.

| Frame | Command | What it proves |
|---|---|---|
| `1-ask.png` | `granadad --radiant=ask` | a stranger (Lisbet Hempson), asked TELL ME ABOUT > A QUEST > WHO NEEDS A HAND?, names a live giver and where they were: *"NEEDLE, IN THE UPPER WARD, WANTS SOMEBODY TO CARRY 4 FLOWER."* |
| `2-shelf.png` | `granadad --radiant=shelf` | a taken errand on the casebook's CASES shelf: RUN 3 DUST / HAVE 0/3, BRING IT TO OLD COBB, LAST SEEN IN THE LANES UP THE SLOPE, PAYS 21C, and the foot line |
| `3-settled.png` | `granadad --radiant=settled` | Onna, found in the crowd at the Mission's night pot, answering PASS THE WORD; the board reads Paid and the purse moved by exactly 24C |

Every line prints its beats and exits non-zero when it falls short: `ask` owes 2,
`shelf` 4, `settled` 7. `settled` also prints `walked=yes|no` -- the giver and Onna
stand on different bands in this world, and the line does not climb, so it places the
body and says so.
