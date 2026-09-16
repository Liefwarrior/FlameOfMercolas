# UI-EA-SPEC — the word diet and the flow grammar

**Program:** wip/district-phase-d-presentation, base 5b072dc. **Brief, verbatim and binding:** "reduce the number of words on the screen by 50% in the UI and make the whole UX flow like a modern video game." **The bar:** ready for EA, judged at 640x360, screenshots not claims.

Ground truth: the word census (41 frames, transcribed), the pattern map (every seam source-verified), the flow scout (every loop driven on both devices). This spec cites them by number; lanes implement this spec, not their taste.

---

## 0. Measurement law

- **WORD** = whitespace-delimited token containing an alphanumeric, exactly as the census counted (`5/10` is one word; `T` is a word; `⏎`, bars, bullets, motifs, border texture are GLYPHS and count zero).
- **Budgets bind the AT-REST frame** captured by the census script's own args (`scratchpad/census-capture.sh` set, default cfg, 640x360). Rest = after every disclosure countdown has expired. LANE FLOW adds `--settle-steps=N` to the screenshot path (default: enough steps to expire the tutor bands, ~4s worth; `0` photographs the raised state).
- **No surface may exceed its census count in ANY state.** Raised bands, plates, and focus modes must still land under the old number.
- **Global cap: 1,785 words** across the census's distinct surfaces (census 3,570; −50.0%). The plan below sums to **1,783**. On the raw 41-frame sum the plan lands at 1,899 vs 3,880 (−51.1%) — both denominators clear the brief.
- Budgets are ceilings. A lane needing slack takes it from deeper cuts inside its own surface group, never from the global.
- Ship re-counts by transcription at 2x, same as the census. Root `granadad-controls.cfg` set aside before any capture or bless (SHIP-NOTE capture rule).

## 1. Word budgets, per surface

Cut order everywhere: DECORATIVE/REDUNDANT first, EXPLANATORY second, LOAD-BEARING last and only by glyph, bar, dedup, paging, or disclosure — never deletion of content.

### 1.1 Creation — census 1,078 → **536** (−50%)

The instrument is the register's own prescription (UI-REFERENCE-TERMINAL, "Why this layout matters here"): **master/detail for every question — answers as short list stubs, the highlighted answer's full text and consequence in the right pane.** Nothing is deleted; it moves behind the highlight. Consequences render as **stat bars with signed numbers** ("watching STREETWISE move"), not sentences.

| # | Surface | Was | Budget | What goes |
|---|---|---:|---:|---|
| 1 | Door | 89 | **36** | Instruction prose (13E), breadcrumb (4D), pane labels (D); explainer prints for the highlighted row only, trimmed 25→14 and **naming no question counts** (kills the 12-vs-10 contradiction); nav band → keycaps. Keeps: 5 rows, commit verb, header. |
| 2 | Calling | 106 | **56** | Instruction 13E, breadcrumb 5D, pane labels; detail 45→24 (stat bars + skill names + a 4-word tide phrase + commit). List of 28 untouched. |
| 3 | Quiz question | 148 | **70** | Two unhighlighted answers collapse to stubs (69→~35 visible); consequence pane 29→8 as bars+deltas; tallies → `5/10` counter riding the header rule; breadcrumb 9D; nav 9→keycaps. Question prose 21 untouched. |
| 4 | Verdict | 96 | **44** | Detail 41→28 (facts and bars); instruction 17→4; breadcrumb 8D; nav → keycaps. |
| 5 | Past question (×12) | 123 | **54** | Stubs law (47→24 visible); cost list 25→8 as bars/deltas for the highlighted answer; header/resources 13→8; hint 9→2; breadcrumb, nav words. Question untouched. |
| 6 | Review | 62 | **62** (was 112) | Instruction+explainer 21E→0; name pane 26→8; breadcrumb, nav words. The 47-word sheet is the point — untouched. |
| 7 | Customize | 122 | **60** | Tier prose 17→0 (bars show tier); budget hint 9→4; labels, breadcrumb, nav words. List of 37 untouched. |
| 8 | Devin sheet | 94 | **48** | Name-pane prose 25→6; instruction 9→0; breadcrumb; nav → keycaps. Stats 37 untouched. |
| 9 | Gabri sheet | 95 | **48** | Same. |
| 10 | OSK | 93 | **58** | Explainer 11→4; prose 10→0; breadcrumb. **Floor: the A–Z grid (29) and the device-aware entry feet stay whole — accessibility is not diet.** |

### 1.2 Street HUD — census 206 → **120** (−42%)

The quiet-HUD program (section 2). At rest the street is nearly wordless; plates announce change.

| # | Surface | Was | Budget | What goes at rest |
|---|---|---:|---:|---|
| 11 | Rest | 24 | **8** (+ the followed lead, ≤ 7) | Clock/temp, street label, case row, room, objective/guild all sleep (each wakes on its own change, sec. 2); `GRANADAD 0.10.0` stamp **deleted from the HUD** — it rides the pause title rule instead. Compass stays. **PULL PACK (owner ruling D8):** while a lead is followed, ONE line rides the ribbon's sub-label row — `NE 40  THE WEIGHHOUSE` — bearing, paces, the place in the sign's own words, `BELOW` / `ABOVE` off-plane (the book keeps `BAND n`; the street cannot see its own band); at most seven words (2 + a four-word place + 1), pinned in test_hud_diet over every authored lead from a body on three bands. It is the one line the street grew by, and it earns its place by moving: the paces run down as you walk. Never a person, never a clue. The named-place ticks on the ribbon cost zero words. |
| 12 | Busiest | 54 | **36** | Cast 4, stealth 4, message 13, room 4 stay (they are the context); rank 6→3; case row asleep; stamp gone; clock 3 woken by the activity. |
| 13 | Crosshair prompt | 25 | **14** | Actor plate 3 + rank 3 + verb + compass; case/clock/street asleep. |
| 14 | Threshold | 34 | **20** | Plate, prompt, stealth, room, compass; the rest sleeps. |
| 15 | Travel arrival | 27 | **14** | Plate + `WALKED TO … 20:02.` + clock (woken by the charge) + compass. |
| 16 | Lead notice | 42 | **28** | Notice 6→4 (`3 NEW LEADS - J`); message row 13 (narration, untouched); crosshair line 7; case row asleep — the notice IS the case news. |

### 1.3 Conversation — census 129 → **95** (−26%)

| # | Surface | Was | Budget | What goes |
|---|---|---:|---:|---|
| 17 | Root | 45 | **33** | `WHAT YOU SAY TO <NAME>` label (6D — the header names them); world plates sleep under dialogue (4D); header 7→5. Options and speech untouched. |
| 18 | Topic | 41 | **29** | Same cuts. |
| 19 | Street (ward body) | 43 | **33** | Same cuts. |

### 1.4 Ward map — census 624 → **300** (−52%)

The selection is named **once**: the detail pane's title row carries name + bearing (`TARWALK - NE 12`). The separate selected line dies — that satisfies the reference's "named in prose underneath" while killing the TARWALK-times-four duplication.

| # | Surface | Was | Budget | What goes |
|---|---|---:|---:|---|
| 20 | Overview | 94 | **46** | Selected line 9→0 (pane title owns it); breadcrumb 5D; nav 9→keycaps; detail 31→16 aligned facts; **plan labels zoom-gated**: default zoom labels only the two highest legend categories (≤14 words), full labels at max zoom — render filter only, zero map edits. |
| 21 | Place selected | 98 | **54** | Same chrome cuts; commit verbs keep the canon grammar `T - TRAVEL (1 MIN)` / `ENTER - FACE IT (W)` untouched; detail 33→14. |
| 22 | People tab | 79 | **44** | Chrome cuts as #20; occupant roster pages at 10 rows + `+N` riding the rule (kills the 38-row wall the flow scout hit). |
| 23 | Index tab | 151 | **70** | Paged ×3 (~50 words visible); place names never abbreviated; chrome cuts. |
| 24 | Legend | 98 | **36** | Chips + category names only (35→18); sentence notes die; plan labels sleep under any full pane. |
| 25 | Case route | 104 | **50** | As #21 plus a 3-word route line. |

### 1.5 Casebook — census 539 → **274** (−49%)

| # | Surface | Was | Budget | What goes |
|---|---|---:|---:|---|
| 26 | Fresh | 107 | **42** | 50-word instructional detail → 20-word case brief; empty state ≤6 words; prose 14→0; nav 12→keycaps. READ/DREAD stays. |
| 27 | Leads walked | 170 | **96** | List pages at 8 rows + `+N` on the rule (50→~34 visible); detail keeps the 43-word body prose whole, provenance/labels 44→9; nav → keycaps. |
| 28 | Lead detail | 153 | **88** | Prose 26 untouched; provenance 14→10 terse `•` bullets; commit 8→4; list as #27. |
| 29 | THE CASE tab | 109 | **48** | Brief 19 + tallies 20→16 stay; empty state ≤6; nav → keycaps. |

### 1.6 Papers menu — census 503 distinct → **236** (−53%)

The hub becomes a hub. Four **summary tiles** (name/calling/3 bars; ward + case pin; letter count + newest sender; active lead + count), ENTER on a tile opens the real surface (actor sheet, map, letters, casebook page). With a letter open, **reading mode**: the letter full, other tiles collapse to their one-word names, the HUD clock sleeps — which also fixes the census's `…DIVINE LIGHT 40 C` overdraw collision. The hub gains the standard header/keycap band (it is today the only page without one).

| # | Surface | Was | Budget | What goes |
|---|---|---:|---:|---|
| 30 | Fresh hub | 159 | **58** | Full panels → summary tiles (char 68→20, chart 27→12, letters 19→6, casebook 42→16). |
| 33 | Letter open | 344 | **178** | Chrome 169→3. **Letter prose 175: untouched, every word.** |

### 1.7 Pause stack — census 485 → **222** (−54%)

Options, wait, and grimoire leave the HUD strip for the composed master/detail card (SHIP-NOTE's standing item, extended to the family).

| # | Surface | Was | Budget | What goes |
|---|---|---:|---:|---|
| 34 | Pause | 33 | **15** | Flavor 10E, key legend 4E; plates sleep. Rows, clock, title stay. |
| 35 | Quit armed | 35 | **17** | `5 - QUIT -- SURE? ⏎`. |
| 36 | Controls | 133 | **50** | Grouped + paged ~14 rows visible (74→~34); both intro proses (10E + 21E) die; the foot's duplicate of `ENTER - REBIND` dies (the pane keeps it); `0 BACK` dies (sec. 4). |
| 37 | Options | 73 | **38** | Composed card; instruction 22→4-word value hint. |
| 38 | Wait | 81 | **34** | Rows to `1 - DAWN 06:00` form; flavor 15→4; **coverage extends to 24h over two pages** (kWaitHours 12→24 — same skipSeconds machinery per pick, rows offered only, no sim change). |
| 39 | Grimoire | 82 | **38** | Composed card; instruction 19→2; empty state ≤6 words. Spell names/costs untouched. |
| 40 | Quickbar toast | 48 | **30** | Rides the dieted busy HUD; toast → `SLOT 3 - CLEAR THE HEAD`. |

### 1.8 The sums

536 + 120 + 95 + 300 + 274 + 236 + 222 = **1,783 ≤ 1,785**. Verified by addition, re-verified at ship by transcription.

### 1.9 First sixty seconds (own budget, binding)

| Path | Census | Cap | Plan |
|---|---:|---:|---:|
| Quick (door → Devin → street → Onna → casebook) | ~360 | **170** | 167 (−53%) |
| Guided, before the street (door, calling, 12 questions, review, OSK) | ~1,876 | **880** | 860 (−54%) |
| Answer-for-yourself, before the street (door, 10 questions, verdict) | ~1,665 | **800** | 780 (−53%) |

## 2. Disclosure — the Law of Earned Text

**Text prints when it changes, when it is aimed at, or when the player hesitates — never merely because it is true.** Three tiers, all on existing machinery (EasedToggle + countdown = the quickBar pattern `session.cpp:6653`; edge detection = the `lastPlaceName_` pattern `:6687`; device change = `noteInputDevice`):

- **STATE** — always up at rest: compass, health/fatigue bars, the active tab row, list rows, the highlighted row's detail, stealth while indoors and nontrivial.
- **EVENT** — rises on its edge, holds ~2.5s, eases down: place plate, case plate (every book change now: leads heard, a sheet or writ handed over, a stage moved on, an errand taken — one plate, one `CaseNews` cue), the skill-up toast, clock on any time charge or hour tick, purse on coin delta, room on entry, objective/guild/rank on change, message-row narration (~4s hold). The plate announces; the reference row sleeps.
- **TUTOR** — nav bands, key legends, instruction copy. Raised in full for ~3s on page open, on device change, on any unrecognized press, and after ~5s of idle on a page (hesitation is the request for help). At rest: **keycaps only** (`TAB  M`), no verb words. On the street at rest: nothing.

Per-row HUD table (LANE HUD, one target-condition line each in `syncPanelAnim`):

| Row | Rest | Wakes on |
|---|---|---|
| compass, bars | up | — |
| clock/temp | asleep | hour tick, time charge, any priced page open |
| purse | asleep | coin delta |
| street label under compass | **deleted** | placePlate already announces crossings |
| followed lead under compass (PULL PACK) | up while a lead is followed | STATE tier: the ONE lead the player chose to FOLLOW (default: the authored next lead); the line changes every step you walk |
| skill-up toast, top-left (PULL PACK) | asleep | a skill level rising (`SKYRUNNING RISES TO 12`), EVENT hold, queues behind itself |
| case row | asleep | beat/lead change, casebook close (2.5s) |
| room | asleep | room entry, loudness change |
| objective / guild | asleep | change |
| build stamp | **deleted** | pause title rule only |
| Q-hold hint | asleep | first two wheel holds: `Q HOLD - WHEEL` toast |

Per-band: map band, casebook foot, keys foot, creation feet, pause legend — all tutor tier. The pause legend and both keys-page proses die outright (grammar is universal, sec. 4). Digits never need teaching: **`1-5 PICK` and its kin are deleted everywhere** — digits pick what they print, and the rows print them.

## 3. Transition grammar

**Everything is a veil or a plate; nothing snaps.** One rule, existing vocabulary only:

1. **Page open/close:** EasedToggle, 8 steps (~0.13s, the `anim.hpp` constant), alpha plus a 2-3px rise derived from `openAmount` (the signed-drift convention, DECISIONS UI rule 4) — every page opens with motion, one derived offset per composition. Background dims to `kPageGroundAlpha` (full pages) / `kBandGroundAlpha` (bands) — already in the register.
2. **Within a page** (tab step, list move): instant content swap + ImpactPulse on the inverted fill. Panes hold their height; only text swaps (reference law).
3. **World seams** (travel, creation→world, boot): the veil. Creation's unconditional `alpha = 1.0F` (`creation.cpp:1374`) is replaced: step changes ease on the shared toggle, and creation→world is dressed with `dressInstantCut` — black falls before the SDL window teardown and the world eases up from black, so the 2s gap reads as one cut, not an app restart.
4. **Close honesty:** a page fades as what it was — the tiled menu's empty-panel close (`session.cpp:7455`) is fixed to fade its tiles.
5. **Commit beat:** ImpactPulse on the inverted fill at every commit (FACE IT, GO TO IT, REBIND, BEGIN, quit-confirm), paired with the existing accept one-shot.

Durations are constants, named once, shared: `kPageEaseSteps = 8`, `kPlateHold ≈ 2.5s`, `kTutorHold ≈ 3s`, `kIdleWake ≈ 5s`, travel dip unchanged. All render-side, steps-based, unhashed.

## 4. Enter/back grammar

**The law:** ENTER/A commits the highlighted thing. ESC/B backs out exactly one layer, to wherever you came from. The key that opened a page closes it. TAB and LB/RB (and digits) step sibling tabs. Digits pick what they print. `0` pages long lists — MORE, never BACK. Any game verb over a page dismisses the page first (already true).

Violations and fixes, every one from the flow map:

| # | Violation | Fix | Owner |
|---|---|---|---|
| 1 | Casebook tabs on LEFT/RIGHT, no hotkey printed (`main.cpp:1834-1861`) | TAB / LB RB / digits, same as the map; LEFT/RIGHT freed for in-view movement | FLOW |
| 2 | Controls page tabs on F1/F2 + `0 BACK` | TAB / LB RB; `0` pages the binding list; ESC backs | FLOW + PAGES (foot copy) |
| 3 | PadUp opens the menu but is eaten as cursor-up inside it (w1≡w2) | The page-toggle key always toggles; in-page cursor-up rides the left stick; d-pad up is toggle-reserved while a PadUp-opened page is up | FLOW |
| 4 | ESC from Keys/Options/Wait lands on the street, skipping Pause (`session.cpp:4688-4722`) | Back returns to the opener: Pause if entered from Pause, street if entered by F1/F2 | FLOW |
| 5 | F1/F2 hard-coded outside the binding table (`main.cpp:4196-4221`) | Become bindable Actions (defaults F1/F2), so they print in the controls list and the pad reaches them via Pause | FLOW |
| 6 | `0` swallowed-and-dead on pause/casebook/map | `0` = MORE where a list pages; inert elsewhere | FLOW |
| 7 | Creation door ESC quits to desktop unarmed (c0) | Arms like the in-world quit: `ESC AGAIN - LEAVE` | FLOW (edge) + PAGES (row copy) |
| 8 | Map tab row clicks taken-and-dropped (`main.cpp:1971-1977`) | Wire `tabRowTabAt` into `session_pointer`, as the casebook already does | FLOW |
| 9 | Grimoire tap-vs-hold undiscoverable | Kept (modern idiom); taught by the tutor toast, sec. 2 | HUD |
| 10 | Map header prints the hour only (08:00 vs HUD 08:01) | Print the live minute | PAGES |
| 11 | Spawn hint band advertises dead `< > MORE PAGES` (`session.cpp:258-260`) | Band retired; replaced by the tutor tier naming live verbs only | HUD |
| 12 | Zero-paces re-travel sold at your own door (SHIP-NOTE top polish item) | Suppress the travel verb when the plan lands within 2 paces; the state label `HERE` replaces verb+distance in the pane — the reference's own `Owned`-label pattern | PAGES (map foot, `map_view.cpp:1543-1580`) |

## 5. Glyph substitutions

New motifs enter through the sanctioned channel only — panel.cpp's motif table, same 4x6 cell/advance/shadow, **no font change**: `▲ ▼ ◀ ▶` arrows, `⏎` return, `✚` d-pad cross. Encoding: sentinel bytes `0x01-0x06` inside label strings; the panel text drawer maps sentinel → motif (no printable-character collisions). **Every substitution routes through the promptKey choke points** (`promptMoveKeys`, `promptKeyName`, `promptConfirmKey`, `controls.cpp:338-411`) so device-awareness survives by construction — a pad still prints `A`, `B`, `X`.

| Today | EA form | Words saved where |
|---|---|---|
| `ENTER` as keycap | `⏎` keycap | every foot and commit echo |
| `ESC`, `A`, `B`, `X`, letters | unchanged | short and iconic |
| `UP DOWN` / `LEFT RIGHT` | `▲▼` / `◀▶` | promptMoveKeys, all feet |
| `D-PAD`, `D-PAD UP` | `✚`, `✚▲` | pad wordings |
| `ARROWS NEXT PLACE` | raised band: `▲▼◀▶ PLACE`; rest: keycaps | map band |
| `ZOOM 2/3` | `+/- 2/3` | map band |
| `M CLOSE`, `J CLOSE`, `TAB <name>` | rest: bare keycaps | all bands |
| `1-5 PICK`, `1-9 PICK` | deleted | every numbered list |
| `0 BACK` | deleted (`0` = MORE) | keys page |
| Header legends (`ENTER SELECTS ESC RESUMES`) | deleted | pause, strips |
| `QUESTION 5 OF 10 / ANSWERED 4 OF 10` | `5/10` riding the header rule | creation |
| `YOU ARE STANDING IN IT` + `FROM YOU N 0 PACES` | `HERE` | map detail pane |
| `3 NEW LEADS J YOUR CASEBOOK` | `3 NEW LEADS - J` | street notice |
| `SLOT 3 -- READY: CLEAR THE HEAD.` | `SLOT 3 - CLEAR THE HEAD` | quickbar |
| Pane type-labels (`A TRADE`, `WHO YOU ARE`, `WHAT IT COSTS`, `WHAT YOU SAY TO <NAME>`…) | deleted | creation, conversation |
| Stat/cost delta sentences | bars + signed numbers | creation, sheets |
| `GRANADAD 0.10.0` on the HUD | rides the pause title rule | HUD |

**Not substituted:** commit verbs keep the canon grammar `KEY - VERB (COST)` — `T - TRAVEL (1 MIN)` matches the reference's `e - Establish (Cost: 200*)` exactly and the ship note's copy review blessed it. Breadcrumb law: **one header line per page** — on tabbed pages the tab row (title left, tabs, resource right, the master/detail reference header) IS the breadcrumb; separate breadcrumb lines die; task pages carry one instruction phrase instead. This satisfies the reference's header rule while deleting the duplicate line.

## 6. Lane assignments

Gate discipline unchanged: one Docker build at a time project-wide; lanes work only in their pre-created worktrees; the Integrate phase waits for a quiet repo.

### LANE HUD — the at-rest diet
**Files:** `native/src/render/hud.cpp`, `native/include/granadad/render/hud.hpp`. **session.cpp regions:** spawn hint (`:250-400`), HudState assembly, `syncPanelAnim` + plates + quickbar countdown (`:6500-6700`).
**Owns:** budgets #11-16 and the busy-HUD base of #40; the sec. 2 HUD row table; stamp removal; hint-band replacement; Q-hold tutor toast.
**Hands off:** page draw returns (`session.cpp:6890+`), input routing, all page files.

### LANE PAGES — the per-page diets and disclosure
**Files:** `map_view.cpp/.hpp`, `casebook_page.cpp/.hpp`, `creation.cpp` + `creation_page.cpp/.hpp`, `menu_view.cpp/.hpp`, `keys_page.cpp/.hpp`, `dialogue_view.cpp/.hpp`, `actor_sheet.cpp/.hpp`, `panel.cpp/.hpp` (motif art, header/tab vocabulary, pulse render on inverted fill). **session.cpp regions:** page copy/assembly only — pause rows `:1805-1812`, pause prose `:5302-5346`, wait rows/`kWaitHours` `:2654` area, strip→card conversions.
**Owns:** budgets #1-10, #17-33, #34-39 content and #40's toast copy; the stubs law; papers tiles + reading mode; zoom-gated labels; all paging; selection-named-once; empty states ≤6 words; zero-paces predicate; map minute; bars-for-deltas; door count-claim fix.
**Hands off:** `main.cpp`, `controls.cpp`, `syncPanelAnim`.

### LANE FLOW — grammar, transitions, feedback
**Files:** `native/src/client/main.cpp` (whole file: routing, tab unification, `0` law, PadUp toggle, F1/F2 Actions, door ESC arm, pointer tab-clicks, `--settle-steps`), `controls.cpp/.hpp` (choke-point motif emission, `pageBackRemap`, new Actions), `anim.cpp/.hpp` if a shared helper is needed. **session.cpp regions:** transition seams only — `dressInstantCut` `:2411-2414`, dip `:6890-6901`, close handovers `:7283-7463`, back-to-opener `:4688-4722`. **creation.cpp region:** the transition seam only (`:1374` + step-change ease).
**Owns:** all of sec. 3 and sec. 4; commit-pulse triggers; creation→world veil.

**Shared-edge protocol:** `session.cpp` is split three ways and `creation.cpp` two ways by the named regions above; an edit outside your region goes through the integrator, never around him. **Cross-lane contracts, exactly three:** (a) motif sentinels 0x01-0x06 — FLOW emits from the choke points, PAGES renders in `drawPanelText`; (b) commit pulse — PAGES renders when a page's ImpactPulse is armed, FLOW arms it at commit routing; (c) tutor bands — HUD lands the countdown/toggle helper (the quickBar pattern generalized), PAGES instantiates per band, FLOW signals the wake events (device change, unrecognized press).

## 7. Deliberately NOT cut

- **World words:** letter prose, lead body prose, actor speech, message-row narration, refusal lines, question and answer text (relocated behind the highlight, never deleted), place and actor names (never abbreviated — density-gated by zoom only).
- **Values:** stats, costs, times, distances, tallies, prices. A number never becomes vaguer.
- **The device-aware promptKey system** — every label substitution routes through it; nothing goes around it.
- **The OSK grid and its device feet** — accessibility floor.
- **Commit-verb grammar** `KEY - VERB (COST)`, the `+~-~-` rules and `|`/`!` edges, inverted-fill selection, READ/DREAD, the font, the diction canon ("bloodletter" only from priests and the well-studied; "covenant" never "charge" in new copy).
- **The sim:** integer math, world hash `0x2646C1AAA2BA38DF`, population baseline, maps, content, art. All new UI state is render-side, steps-based, unhashed.
- **Out of scope for this program:** the shown-skill-check block and journey-scale travel costs (gameplay, owner said after), the carried-body render, demo camera motion.

## 8. Ship gate checklist

1. Census script re-run, same args, `--settle-steps` default: every surface ≤ its ceiling, global ≤ 1,785, first-sixty-seconds caps met on all three paths, no surface above its census count in any state.
2. Twin-run, `--demo`, `--case`, `--case-watch`, `--eviction` byte-stable; `verify-windows.ps1` PASS; Docker gate green. Committed frames that legitimately moved are re-blessed deliberately, with counts and reasons, root cfg set aside.
3. Screenshots at 640x360, looked at, described as seen: the quiet street, a raised band easing down, the stubs quiz, the tile hub, reading mode, the `HERE` label, the dressed creation→world cut.
4. The blunt EA judgment, as every prior ship has given it.
