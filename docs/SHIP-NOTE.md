# Ship note — read this first

**Fast travel is real, and it was driven, not read about.** The ward map's
detail pane now carries a second commit above `FACE IT`: `T - TRAVEL (2 MIN)`
on a keyboard, `X - TRAVEL (2 MIN)` on a pad, the cost restated in the number
ink. A press charges the clock by exactly the restated minutes through the wait
machinery (`skipSeconds` — `Tavern::skipTo` + `syncClockAfterSkip`, the same
pair every WAIT pick spends), relocates the body to the destination's door
snapped to standable ground, dips the screen fully black and eases it up on the
arrival with the threshold plate and a `WALKED TO THE ROYAL COUNTING-HOUSE.
08:01.` line already on it. Refusals hold: carrying Finch refuses in the ward's
voice (`NOT WITH THE FINCH. WALK HIM.` — driven, photographed), the Watch
closing refuses, and the wait page's own list refuses in its own words. All of
it verified this phase **by playing the windowed build with a real keyboard and
a real virtual pad**, and by the headless probe run twice byte-identical.

**Two programs landed while this phase ran, and both are green.** The travel
merge (`c099b08` + frames `710b616`) was gated in a fresh isolated worktree by
this phase: digest `df62622b…89af6f6`, ctest **1011/0 failed**, verify-windows
PASS. The concurrent case-watch program then landed `c59f88f` (the owner's
"teleporting through walls / double vision" bug: view snapping on relocation,
one dressed seam shared with travel's fade) + frames `24e0f11`, with **its own
gate stamp** (`ba3b34ff…`, ctest **1016/0**). This phase re-verified the tip:
verify-windows PASS, twin-gate baseline unmoved, demo/case/case-watch exit 0,
and the travel probe's figures **identical on both builds** — the cuts commit
did not move travel by a byte of behaviour.

Gate: **green, both halves, both revisions.**
World hash: **`0x2646C1AAA2BA38DF`** — regenerated, rebaked, twin-gated twice
on each build, unmoved.

## Run it

```
.\dist\granadad.exe                # play it; M for the map, T on a named place travels
.\dist\granadad.exe --demo         # two minutes, plays itself, ends on a card
.\dist\granadad.exe --case         # THE QUIET TENANT end to end, headless
.\dist\granadad.exe --case-watch   # watch the same errand at the demo's pacing
.\dist\granadad.exe --map-overlay --travel="The Ropewalk"        # the probe: | travel ... charged=2min
.\dist\granadad.exe --case=taken --travel="Mission of the Flame" # the carry refusal, man in hand
```

In the world: **M** map (arrows walk the named places, four presses cross the
ward), **T** travels to the selection, **ENTER** faces it. On a pad: **SELECT**
map, D-pad walks places, **X** travels, **A** faces. `--travel=NAME` wants the
roster's exact name (`"The Ropewalk"`, not `"Ropewalk"`).

> Still true: the creation window does not answer F12, and `--demo` swallows
> everything but ESC. New this phase: the windowed harness can drive creation
> only through `--padcreation` (SendInput keyboard creation never confirmed a
> sheet in four attempts — a harness limitation to fix, not a game defect: the
> pad path proves the flow, and by hand the keyboard flow is the documented
> DOWN DOWN ENTER line).

---

## The gate, in full

| half | where | revision | result |
|---|---|---|---|
| `docker compose run --rm --build build` | fresh worktree `C:\repositories\granadad-ship-710b616` (four audio dirs real-copied, 374 files) | 710b616 | **exit 0**, stamp 2026-09-02T15:32:31Z |
| `scripts\verify-windows.ps1` | fresh worktree | 710b616 | **=== PASS ===** |
| `docker compose run --rm --build build` | main repo (the case-watch program's own landing gate) | 24e0f11 | **exit 0**, stamp 2026-09-02T15:37:08Z |
| `scripts\verify-windows.ps1` | main repo, re-run by this phase | 24e0f11 | **=== PASS ===** |

```
travel merge:     c099b08 (8 lane commits on 9910bec, 2 conflicts resolved) + 710b616 frames
cuts landing:     c59f88f + 24e0f11 frames (the concurrent program's, its own gate)
native/ digest:   df62622b0b853f5a2a50eb9e9cdb841a9425c28bf3d9219640207568289af6f6 (@710b616)
                  ba3b34ff119c2a92085d1110673ce142ff6291c1827b864f53119e21ce58767b (@24e0f11 tip)
ctest cases:      1011 @710b616, 1016 @tip (floor 537), 0 failed either
comparators:      world-hash+sim 1791 bytes sha256 924F6EA6…B7B8468B, linux/gcc == mingw, both revisions
```

## The world hash did not move — regenerated, rebaked, twin-gated

```
1. regenerate   python tools\scripts\gen_docks_surface.py
                docks_surface.tmx sha256 CCEDA566…237B4D1C, tree clean (reproduced the committed TMX)
2. rebake       gradlew :tools:run --args="import-map <abs>\docks_surface.tmx <out-file> --raws <abs>\content\raws"
                (paths must be absolute, and the second arg is the output FILE, not a dir)
                17,954 bytes sha256 E47DA3AE…E474C2AC == content/maps/baked/docks_surface.trojsav
3. twin-gate    dist\granadad-twin-gate.exe --population --population-hour 16 --ticks 7200
                run twice on the 710b616 build AND twice on the tip build:
                0x2646C1AAA2BA38DF all four runs, each pair's console output byte-identical
4. content diff git diff --name-only 9910bec..HEAD -- content/maps content/art  ->  EMPTY
```

## Driving it, this phase — fast travel played, at 640x360

Frames in `docs/frames/ship-travel/` (this phase, windowed, real input via
`drive-windowed.ps1` + `--padcreation`/`--padscript`) and
`docs/frames/travel1/` (the integrator's headless four).

**As a pad player, end to end:** virtual-pad creation (A takes a calling,
twelve A's answer the past, one letter + START names the sheet, UP wraps to
BEGIN), then SELECT opens the map. The pane's verb reads **`X - TRAVEL
(1 MIN)`** over `A - FACE IT` — pad-worded with no keyboard press anywhere
(`pw2-north`). Selecting the place you stand in reads `YOU ARE STANDING IN IT`
and X does nothing, correctly (`pw3-east`, `pw4-traveled`). A far pick (`THE
ROYAL COUNTING-HOUSE`, SE 34 paces, `px1-far`) travels on X: the screen dips
black and eases up on the destination street with the plate and `WALKED TO THE
ROYAL COUNTING-HOUSE. 08:01.` on the message row, the HUD clock moved 08:00 →
08:01 — exactly the restated minute (`px2-fade`, `px3-arrived`).

**On the keyboard, same session shape:** M re-words the page live to keyboard
vocabulary — `T - TRAVEL (1 MIN)` over `ENTER - FACE IT (SE)`, nav band
`ARROWS NEXT PLACE  TAB OVERVIEW  M CLOSE` (`kw2-verb`). T commits; the shot
taken on the press's heels caught the fade mid-ease, the street readably
darker with the plate already up (`kw3-fade`), fully lit a second later
(`kw4-arrived`). **Is it dressed, or the teleport the owner complained about?
Dressed — honestly.** The origin is never seen after the press, there is no
raw cut, and the plate + arrival line + moved clock land the "you went
somewhere" read. (The owner's separate "through walls / double vision"
complaint was the *case-watch replay's* seam — the concurrent program
diagnosed and closed it at `c59f88f` on true consecutive frames.)

**The refusals:** the carry refusal driven for real (`--case=taken
--travel=…`): `moved=no refusal="NOT WITH THE FINCH. WALK HIM."`, case parked
at beats=7/8, clock untouched — and the integrator's `travel-refused-carry`
shows the line sitting in the verb row's place. Watch-closing and the five
wait-list refusals are each pinned by `test_travel` under the green gate; a
windowed brawl refusal was attempted but the crosshair never found a body on
the street, so those five stand on the tests, said plainly.

**Determinism:** the probe run twice, full console output **byte-identical**
(`| travel to="Mission of the Flame" found=yes route=54 units=604 walk=37s
charged=1min clock=72000->72060 moved=yes plate="ROPEWYND" up=yes`).

**The measured costs, and they are small:** Mission of the Flame 37s → 1 MIN;
The Quayward Compound 67s → 2 MIN; The Ropewalk 95s → 2 MIN. The worst legs
this phase found are **2 MIN**, not the lane's estimated 4-5. Travel is nearly
free — which sharpens the minutes-vs-journey ruling below.

## Found by driving, this phase

1. **The zero-paces re-travel** (`kw6-mapafter`, `kw7-press`): the arrival
   ring lands you at the door but *outside* the place's footprint, so
   reopening the map on your own destination reads `FROM YOU N. 0 PACES` —
   and still offers `T - TRAVEL (1 MIN)`. The press charges a real minute,
   moves you a step, and re-fires the plate. Honest by every rule it was
   built under (`contains()` is strict, the floor is one minute), but it
   reads as a vending machine selling you the doorstep you stand on. One
   predicate (suppress the verb when the plan's landing is within a pace or
   two) closes it. **Flagged as the top polish item.**
2. **`quay-spawn.png` was a stale bless** — it read `TAB YOUR NOTES` because
   every prior capture ran beside the player's own `granadad-controls.cfg`
   (`bind menu TAB PAD_UP`, the repo-root file .gitignore #77 calls the
   player's, not the repo's). Fresh defaults say `J YOUR NOTES` (the owner's
   own J-for-journal ruling). Re-blessed this phase from a twin-verified
   fresh-default capture, identical on both builds; the other 21 demo frames
   were already byte-identical without the cfg. Capture rule going forward:
   **set the root cfg aside before any bless.**

## Copy review — the register holds

Every travel line read against the city register: `NOT WITH THE FINCH. WALK
HIM.` is the best of them — brief, urbane, load-bearing. `NOT WITH THE WATCH
CLOSING.`, `NO WAY THERE ON FOOT.`, `NO GROUND TO STAND ON.`, `WALKED TO …
08:01.` all hold; the verb row's `T - TRAVEL (2 MIN)` matches the reference's
`e - Establish (Cost: 200*)` grammar exactly. Nothing clunks; nothing reads
AI-ish. The dormant `ABOUT AN HOUR` phrasing only ever prints if a journey
scale lands (below).

## Rulings awaiting the owner's veto — all built, all working

- **Minutes, not hours — and now measured at 1-2 MIN worst case.** The ward is
  one district; the honest walk is small. If travel should *feel* like a
  journey, an integer scale is one line (×8 ≈ 8-16 min on the measured legs;
  the `ABOUT AN HOUR` label already handles ≥60). As shipped it is honest and
  nearly free.
- **Arrival = the aim-point door, snapped to standable ground; plate fires.**
  Note: the plate names the ground you land ON (`ROPEWYND`, the street at the
  Mission's door) while the arrival line names the destination — consistent,
  but worth your eye once.
- **The zero-paces re-travel** (this phase's find, above) — veto the current
  behaviour and one predicate suppresses the verb at your own door.
- **Heat and warrants deliberately do NOT refuse** — waiting one out is a
  tactic, your own wait-page ruling.
- **The black-dip fade** is new render vocabulary (and the cuts landing now
  shares its seam) — driven, and it reads as travel, not teleport.
- **HUD rename `FINCH IN HAND` → `CARRYING FINCH`** (you found the old label
  unclear). Carry refusal + arrival lines are new register literals.
- **Keyboard T, pad X** (Attack's pad half — Interact's pad half IS the FACE
  IT confirm; the pad gate keeps mouse-left from double-firing).
- Carried forward from the courier case, unchanged: courier=Onna,
  author=Maell, target=Finch, site=the Gull; the tarry-jek line resolved as
  Finch; Gabri stays off-map word.

## The verdict: the reach machinery is done — the blocker is content alone now

The standing bar is "clean and near ready for early access," and the standing
blocker was **content and reach**. Reach is now honestly answered: a
stranger's first sixty seconds still land (creation on one grammar, the
casebook hint, Onna at your elbow by six seconds), and the first thirty
minutes now include a map you can read, walk by places, and *travel* — the
courier's own "find the Gull, wait for dark" beats can be reached in two
presses, and every arrival is a named threshold. The ward finally plays like
a place you get around in rather than a corridor you re-walk.

But the honest distance did not move: **two authored cases is an evening**,
and the scripted lines are system demos. Fast travel makes the well easier to
drink from; it does not fill it. The worst surface today is no longer a
screen — it is the zero-paces re-travel wart above, one predicate deep, and
behind it the options overlay still runs a HUD strip. **Not near ready — the
reason is unchanged: not enough game behind the screens yet. Everything in
front of the content is now ready to carry it.**

## The three things to do next

1. **Authored content, in bulk — still the whole remaining distance.** A
   spread of cases with the reach to drain them over more than an evening.
   Nothing else blocks early access.
2. **Close the travel wart and the courier's half-simulated verbs:** suppress
   the zero-paces travel offer; then the carried-body render and a
   stay-down/bound state, so the kidnap is a haul rather than a prompt-race.
   (If the owner wants journey-feel, land the one-line cost scale in the same
   pass.)
3. **The options page onto the master/detail card**, the one surface still on
   a HUD strip — and the demo camera dolly so the trailer moves.

## Still open, unchanged

`--demo-capture` drops `creation-origin` (the shutter race; `--creation`
substitute is byte-exact); the creation window does not answer F12; the input
router lives in `main.cpp`'s anonymous namespace, unlinkable by any suite; no
camera motion in the demo; the 1920 sheet sits 1px left of its old centring.
New: the windowed harness cannot yet drive keyboard creation blind (pad path
covers it); `--travel=NAME` wants the roster's exact mixed-case name.
