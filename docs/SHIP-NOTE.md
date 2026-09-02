# Ship note — read this first

**The three moves landed, and the tutorial reads right on both hands now.** The
retry's four lanes merged clean (twelve builder commits + three merges + the
integrator's compile/drive fixes) and every claim in their reports was
re-verified here **by driving the built game and measuring the frames**, not by
reading the reports back. J opens the journal, the seven silent pages answer the
mouse, the B that closed the casebook no longer crouches, a pad player's first
world screen is pad-worded before they touch anything, and the sheet's NAME row
holds still while you type. The second authored case — **THE QUIET TENANT**, the
courier's tutorial errand — plays end to end, teaches through device-aware
prompts, and does not move the population baseline a bit.

**One seam found by driving, and closed this phase:** the courier case's two
notice plates (`A MISSION SHEET … YOUR LETTERS`, `THE ERRAND IS PAID … YOUR
CASEBOOK`) named the key with `keyName(primary[Menu])` — the raw keyboard slot —
so a **pad** player being taught the game was told to press **J**, a key their
pad has not got. The device-aware `promptLabel(…, promptDevice_)` the Bloodletter
plate uses three lines away is the fix; both plates now read `D-PAD UP YOUR
LETTERS` / `… CASEBOOK` on a pad and the unchanged `J …` on a keyboard.
Render-only, keyboard-byte-identical, sim untouched. Photographed before and
after: `docs/frames/parallel4/courier-plate-SEAM-keyboardworded.png` vs
`…-FIXED-deviceaware.png`.

Gate: **green, both halves**, native digest `ae5a7d15…`.
World hash: **`0x2646C1AAA2BA38DF`** — regenerated, rebaked, twin-gated, unmoved.
Demo: **run twice, exit 0 both**, 5780 frames, body (150,63,z19) both. Demo
frames: **22/22 byte-identical** to the committed set (the courier is off under
`--demo`, so the fix cannot touch them).

## Run it

```
.\dist\granadad.exe --demo      # two minutes, plays itself, ends on a card
.\dist\granadad.exe             # play it: DOWN DOWN ENTER, name, UP, ENTER, twelve ENTERs, UP, ENTER
.\dist\granadad.exe --case      # drive THE QUIET TENANT end to end, headless, and print the errand's state
.\dist\granadad.exe --case-watch  # WATCH the same errand: the identical drive at the demo's pacing, ~3 minutes, ESC leaves
```

In the world: **J** casebook (per the owner's "use J for journal"), **M** map,
**E** talk, **ESC** pause, **F1** keys, **F2** rebind. On a pad the screen tells
you itself — the prompts name the hand that is holding the machine and re-word
live.

> Still true: the creation window does not answer F12 (photograph creation
> through `--creation=STEP` or `--padcreation`'s `shot:` beats), and `--demo`
> swallows everything but ESC.

---

## The gate, in full

`docker compose run --rm --build build` was run twice this phase: once on a
**fresh isolated worktree** at `C:\repositories\granadad-ship-7b1904e` (the four
gitignored audio dirs real-copied, 374 files) at the merged HEAD `7b1904e`, and
once in the **main repo** on the one-line fix. Both green; `verify-windows.ps1`
green in the fresh worktree and (both before and after the fix) in the main repo.

| half | where | result |
|---|---|---|
| `docker compose run --rm --build build` | fresh worktree @7b1904e | **exit 0** |
| `scripts\verify-windows.ps1` | fresh worktree | **=== PASS ===** |
| `docker compose run --rm --build build` | main repo, with the fix | **exit 0** |
| `scripts\verify-windows.ps1` | main repo, with the fix | **=== PASS ===** |

```
merged revision:  7b1904e (18 past 3181e52: 12 lane + 3 merge + 2 integrator + 1 frames)
ship fix:         1 commit on top (the courier plate device-aware seam)
native/ digest:   ae5a7d15df07c580b88a577c3bb30d800bf60874b4cdf3974ece7eef8126f049 (with the fix)
ctest cases:      995 (floor 537), 78 suites, 0 failed, ~146s
```

Comparators byte-for-byte identical linux/gcc vs mingw/windows, **and identical
across the fix** (the fix is render-side, which no gate hashes):
decoded world state 3884 bytes sha256 `97850DCB…C8B2179`; world hash + sim run
1791 bytes sha256 `924F6EA6…B7B8468B` — the same values before and after the
courier-plate fix. The four `dist\` binaries hashed main-repo vs fresh-worktree
at the merged HEAD: **byte-identical, all four** (proven before the fix; the fix
then rebuilt `dist\` in the main repo, which is now the certified set).

## The world hash did not move — regenerated, rebaked, twin-gated

```
1. regenerate   python tools\scripts\gen_docks_surface.py
                docks_surface.tmx sha256 CCEDA566…237B4D1C, tree clean (reproduced the committed TMX)
2. rebake       gradlew :tools:run --args="import-map …docks_surface.tmx <tmp> --raws content/raws"
                17,954-byte trojsav sha256 E47DA3AE…E474C2AC == content/maps/baked/docks_surface.trojsav
                (baked with BOTH new quest JSONs present in content/raws — they do not enter the bake)
3. twin-gate    dist\granadad-twin-gate.exe --population --population-hour 16 --ticks 7200
                run twice: 0x2646C1AAA2BA38DF both runs, reports IDENTICAL, the two console
                outputs byte-identical to each other. Re-run after the fix: same hash.
4. content diff git diff --name-only 3181e52..HEAD -- content/maps content/art  ->  EMPTY
                -- content/  ->  the two add-only quest JSONs and nothing else
```

`content/raws/quests/mission_sheet.json` + `mission_sheet_letters.json` carry
top-level `case`/`leads`/`letters`, never `stages`/`templates`, so every hashed
loader skips them. No re-bless: the baseline never had a way to move.

## The demo

```
run 1 (@7b1904e)   5780 frame(s), (150,63,z19)   exit 0
run 2 (@7b1904e)   5780 frame(s), (150,63,z19)   exit 0
run 3 (with fix)   5780 frame(s), (150,63,z19)   exit 0
```

`--demo-capture`: **22/22 byte-identical** to `docs/frames/demo/`, both at the
merged HEAD and with the fix. `creation-origin.png` reproduces byte-identical via
`--creation=origin --scale=1` (the known `--demo-capture` shutter-race drop; the
committed copy is the substitute). The Bloodletter demo route is intact.

## Driving it, this phase — every claim photographed at 640x360

Frames in `docs/frames/parallel4/`. Driven through the **real windowed build**
with real keyboard/mouse (`scripts\drive-windowed.ps1`, SendInput scancodes +
client-area pointer) and the **virtual pad** (`--padcreation` + `--padscript`).

**The courier case, driven headless twice** (`--case`, the errand's own scripted
line): `beats=8/8 mask=255 read=3/4 closed=yes carry=no`, the `| case …` summary
segment **byte-identical across runs**, the two output PNGs byte-identical. Every
beat looked at:
- `case-sheet` — Maell's sheet open on the Letters tile (parchment, `handed`).
- `case-gull` — casebook on THE QUIET TENANT, the door lead read, READ 1/3.
- `case-night` — crouched/HIDDEN on the dark guest floor, the box lead read.
- `case-down` — Finch on the boards, crosshair `FINCH — DOWN, AND COMING WITH
  YOU / E - TAKE HIM UP`, message row device-aware. **The brawl line held:** the
  drive carries fists, `mask=63` at the down shutter with `tenant=down` and no
  Watch — nothing edged, subdue by the house's own law (`classifyFight`).
- `case-done` — `THE ERRAND IS PAID … YOUR CASEBOOK`, book closed, at the
  Mission back room.

**As a pad player** (the tutorial read the owner asked for): `--padcreation`
takes a character off the DOOR/PATH/PAST/SHEET screens (A picks a calling, twelve
A's answer the biography, the 10x3 OSK spells the name, START commits, BEGIN);
`--padscript` then drives the world. The first world frame, **pre-press**, reads
`PICK A LEAD. A SHOWS YOU WHERE.` with foot `A - SHOW ME WHERE` and `A GO TO IT`
— pad-worded with no world press, the device seeded from creation
(`pad-world-prepress`). The courier hail then lands with the plate `A MISSION
SHEET  D-PAD UP YOUR LETTERS` — **the seam this phase fixed**, now naming the
pad's own key (`courier-plate-FIXED-deviceaware.png`; the pre-fix
`…-SEAM-keyboardworded.png` shows the `J` it used to print).

**The mechanical items:**
- **Pointer, the silent pages.** Photographed answering the mouse: pause (hover
  inverts the row under the pointer, `WAIT` filled), the wait page (click a row →
  the clock advances, `WAITED UNTIL 13:00`), the CONTROLS/keys page (hover moves
  the fill **and the detail pane follows the pointer** to the RUN row —
  `ptr-controls-hover`), the options page (SETTINGS click opens it), and the
  casebook TAB row — clicking `THE CASE` swaps the detail pane with the list and
  frame dead still (stable-geometry), the frame **byte-identical to the
  committed `ptr-tab-thecase.png`**, and `LEADS` clicks back. The tiled Menu's
  four tiles (`--character`) and the conversation topic list share the
  `menuTileHitAtPixel` / `dialogueTopicAtPixel` inverses proven on their
  siblings and green under `test_menu_view`/`test_tavern_render`.
- **Regression:** a left-click on the open street with nothing up **punches**
  (`NOBODY IN REACH` — the world verb, not swallowed): the pointer branch only
  runs on `pointer_page_open` (`ptr-street-punch`).
- **J opens the journal**, and the copy says J: a fresh-defaults windowed run
  writes `bind menu J PAD_UP`, the keys page reads `MENU  J`, the opening hint
  reads `J YOUR NOTES  < > MORE PAGES  E USE`, and pressing J closes the
  auto-opened new-game casebook to that hint. (`E USE` is the honest fallback
  under a cfg with no pad `interact` half — the other-hand rule.)
- **The B seam, closed:** on a pad, one **B** closes the casebook to the street
  with **no CROUCHED banner** (`pad-after-b-nocrouch`).
- **The NAME wiggle, gone:** the pad OSK typed to 2 / 6 / 12 glyphs holds its
  outer frame at exactly x[114..497] all three times (`pad-name12-nowiggle`); the
  keyboard sheet is proven by the green `test_creation` cap-measure case and by
  `creation-name.png` staying byte-identical.

## Copy review — the case reads in the city register

Every player-facing line of THE QUIET TENANT was read against the register
(brief, urbane, salted, no AI-ish over-explaining). **It holds, and at its best
it is very good.** The strongest lines earn their place: *the close* "THE MISSION
HAS ITS MAN. THE FLAME KEEPS ITS OWN COUNSEL."; *the delivery* "MAELL DOES NOT
THANK YOU FOR IT, AND DOES NOT PRETEND IT WAS NOT ASKED FOR."; *the brawl teach*
"A BRAWL IS THE HOUSE'S OWN LAW, STEEL IS THE WATCH'S. THE BOUNCER ANSWERS THE
FIRST SWING, SO ANSWER HIM FIRST."; *the follow-up* "MAELL DOES NOT WRITE TWICE."
Maell's sheet ("I know what this sheet is. I have written it anyway…") takes the
weight of the order in the author's own hand, exactly as the brief asked.

The lines that teach a mechanic carry the most instructional load — "EVERY PROBE
IS NOISE. … CROUCH, AND KEEP OFF THE LIT TILES." is the closest any line comes to
a tutorial voice — but they stay imperative and in-fiction, and none clunk. No
line reads as AI-ish or over-explained. **Nothing flagged for a rewrite.**

## The canon choices stand, flagged for the owner's veto (unchanged)

The case lane chose from existing actors/buildings and flagged each; nothing here
overrode them. For the owner to accept or veto:
- **Courier = Onna**, **author = Father Maell**, **target = Finch** (the
  Skyrunner on the Gull roster), **site = the Gilded Gull**, delivery at the
  Mission back room. All existing.
- **⚠ The one canon ADDITION:** the case resolves casebook.json's deliberately
  ambiguous tarry-jek line ("A THIN FELLOW WALKED IN THROUGH A GAP NO MAN FITS")
  as **Finch**, paid to open the Drowned Hold from inside. Clean fit, but it
  *interprets* an open Bloodletter thread. Reject it and only the snug-stool
  `found` line needs softening; the case still works.
- **Gabri never appears** — "word has come from Gabri" is word, through the
  Mission, from off-map; the sheet says outright it will not put down from where.
  (Pre-existing tension the case lane raised and did not touch:
  `bloodletter_letters.json`'s `gabri-dispatch` is written in-ward at the Drowned
  Hold. Reconcile at your discretion.)

## Honest leftovers (the case lane's own flags, still true)

- **No carried-body render.** TAKE HIM UP sets a bale-shaped session flag (`FINCH
  IN HAND` on the HUD) and the delivery closes on arrival; there is no
  over-the-shoulder body drawn. Follow-up render work.
- **No stay-down state.** A downed patron recovers at quarter health, so the
  kidnap is honestly "put him down and take him up promptly" — the crosshair
  prompts the instant he is down; dawdle and he rises and you re-subdue. Once
  taken, the carry flag persists.
- **No manual case-switcher.** The courier case takes over the active-case
  surfaces until delivered, then the Bloodletter returns (`sheetCaseLive()`).
  Correct for a focused tutorial; no third tab this pass.
- `case-night`'s crosshair rests on the bouncer (`E - PICKPOCKET`) rather than
  the box — a scripted-drive aim, not a bug; the box lead is read regardless
  (READ 1/3 in `case-gull`).

---

## The verdict: closer again — the first minutes now teach and pull — and still not near ready

The standing verdict was "closer by a real step — still not near ready; the
blocker moved to content and reach." This phase moved the blocker's near edge:
there is now a **second authored case that is a real tutorial**, and it works.

**A stranger's first sixty seconds, today:** creation on one measured grammar
(door, calling, quiz, the sheet — the NAME wiggle now gone), then the world with
the casebook waiting and the `J YOUR NOTES` hint. And roughly six seconds in, the
courier: *"ONNA, AT YOUR ELBOW: PAPER FOR YOU, OUT OF THE MISSION."* — an
inciting incident, in the register, that hands the newcomer a sheet and a reason.
That is a genuinely better first minute than a lone Bloodletter waiting to be
noticed.

**The first thirty minutes, now:** the courier's errand is a guided line — read
the sheet, find the Gull on the map, wait for the dark hours, cross the guest
floor crouched, put Finch down with fists, carry him to the Mission — and each
lead teaches one system in the ward's own words. It hands off to the Bloodletter,
the twelve-lead investigation. So the arc is real: a short linear tutorial that
opens into the sandbox's one open case. That is the shape an early-access opening
wants.

**But the bar is "clean and near ready for early access," and the honest
distance is still content and reach.** Two authored cases — one four-lead
tutorial, one twelve-lead investigation — is an evening, maybe two, and then the
authored well is dry; the scripted lines (skyrun, nemesis, contract, burgle,
roofs) are system demos, not narrative. The tutorial is linear and short and
gives up its own last page early (the sheet names the delivery before you take
him). No line clunks and no screen is broken — the **presentation debt is paid**
— but a sandbox built for a season still holds an evening of authored play. **Not
near ready.** The reason is no longer the screens or the prompts; it is that
there is not enough game behind them yet.

**The worst screen** is no longer a screen with a defect — the sheet's wiggle is
gone, the Menu is on the register, every page answers the mouse. The least
polished surface left is the **options overlay** (F2/SETTINGS): a bottom-strip
HUD list rather than a full master/detail card like the CONTROLS page beside it —
functional, in-register, but the one place the reference's card grammar is not
fully spent.

## The three things to do next

1. **Authored content, in bulk — the whole remaining distance.** Not one more
   case: a spread of them, and the reach to drain them over more than an evening.
   Every screen and prompt is ready to carry it; nothing else is the blocker.
2. **Finish the courier case's honest leftovers into real mechanics** — a
   carried-body render for TAKE HIM UP, and a stay-down (or bound) state so a
   kidnap is a haul across the district rather than a prompt-race. These are the
   two places the tutorial teaches a verb the sim only half-simulates.
3. **The options page onto the master/detail card**, to spend the reference's
   grammar in the one surface that still runs a HUD strip — and, cheaply, a
   camera dolly in the demo so the trailer moves.

## Still open, unchanged

`--demo-capture` drops `creation-origin` (the shutter race; the `--creation`
substitute is byte-exact); the creation window does not answer F12; the input
router lives in `main.cpp`'s anonymous namespace, unlinkable by any suite; no
camera motion in the demo; the 1920 sheet sits 1px left of its old centring
(invisible, integrator-found, noted so nobody hunts it as drift).
