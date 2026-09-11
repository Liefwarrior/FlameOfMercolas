# The justice build, photographed

Every frame here is the real `dist\granadad.exe` playing the court through the real verbs
(`--court[=WHERE]`, `--flame --court=WHERE`, `--creation`) on the gated Windows build --
nothing is staged, nothing is a harness. The line prints what it landed beside the picture
(`court beats=n/N ... judgment=... row="..."`), so a frame that fell short cannot pass as
one that did.

Eli, 2026-09-02: *"If the player is tagged as a criminal it should be like Daggerfall where
you can go to court and you can face jail or execution (game over)."*

| frame | line | what it shows |
| --- | --- | --- |
| `court-wanted-960x540.png` | `--court=wanted` | TAGGED. Eight lifts in Watchman Cull's sight in the Gull at eleven; `WANTED  HEAT 64` on the HUD in the tag's red, a witness's own row on the alert. Paper out, nobody's hand on you yet. |
| `court-page-960x540.png` (and `-640x360`) | `--court` | TAKEN. Steel up in Cull's sight, taken at reach, the body at the Mission's door: the hearing page. The reading (the officer lays the paper, the charge off the sheet, what the paper asks), the three rows, the priest's opening (`court.paper`), and under the rows the officer who walked you in (`court.taken`). |
| `court-paper-640x360.png` | `--court=paper` | HEAR THE PAPER: the sheet as phrases, never numbers, before the plea. |
| `court-plea-960x540.png` (and `-640x360`) | `--court=plea` | I DID IT weighed: `24 THE FLAME - 20 THE WARD - 1 HEAT MAKES 3`, `+ 6 CONFESSED MAKES 9`, the lines, BOUND, `BONDSWORN 5 DAYS.`, the priest's word (`court.bound`), the one row `SERVE IT.` |
| `court-deny-960x540.png` | `--court=deny` | I DID NOT weighed: `- 9 THE PRIEST IS A MAN MAKES ...`, the same sheet doubled under the band, the lie in the priest's mouth (`court.lie`). |
| `court-flame-plea-960x540.png` | `--flame --court=plea` | What you gave at the door is the only coin the Flame reads: the flame line first (temple standing 100), the court waits for Cull through the wait page, `+ 24 THE DOOR` in the block, HELD -- `TWO NIGHTS. 16 ROYALS.` |
| `court-flame-deny-960x540.png` | `--flame --court=deny` | The same sheet denied and disbelieved: HELD doubled, `FOUR NIGHTS. 32 ROYALS.`, `court.lie`. |
| `court-serve-960x540.png` | `--court=serve --settle-steps=30` | JAIL SERVED, THE WORLD MOVED. BOUND taken by its row: `TURNED LOOSE ON THE TARWALK. DAY 6. 23:00.` -- five days on the world clock from day 1, the paper off, `HEAT 12` (`kHeatAfterSentence`, live) and no tag on the HUD. |
| `court-flame-serve-960x540.png` | `--flame --court=serve --settle-steps=30` | HELD served: the fine paid (34 -> 18 Royals), two nights on the clock, `TURNED LOOSE ON THE TARWALK. DAY 4. 00:00.` |
| `court-rope-960x540.png` (and `-640x360`) | `--court=rope` | EXECUTED. A killing at eight before the Watch drinks (two saw it), taken to a rope hearing, THE ROPE, the drop taken by the player's own row: `HANGED AT THE SALTGATE POST. / BY THE WARD. FOR TARN WRENHALE. / THE FIRST DAY. 22:00.` with `1 - A NEW MAN / 2 - LEAVE` under the plate. No save row: no save exists. |
| `court-newman-960x540.png` | `--court=newman` | A NEW MAN taken by its row -- the plate's last frame, the row lit, `end=new-man` in the summary: `runEnded()` with `RunEndChoice::NewMan` and no quit, the answer `main()` loops on. |
| `court-newman-creation-960x540.png` | `--creation` | The creation window `main()` opens next -- `run_creation_window` and this capture draw the same `CreationFlow` on its origin step. A fresh run starts with a clean ledger (asserted in `test_hearing_page.cpp`). |

## The two paths, kept apart

A combat defeat is the nemesis ruling and you live: `--nemesis` on the same build still lands
7/7 (`Tarn Wrenhale x3 ... holds THE GULLET`), the quay revive under it. THE ROPE is the one
place the player ends, and it is reached only through the bench. `test_court.cpp` asserts both
directions (a served sentence never touches the NemesisBook; a defeat after a sentence still
rises and revives; the rope alone sets `executed_`).

## What the drive cannot reach, said plainly

- **A pure FINE is not on these frames.** The smoke's own thief earns the paper by eight
  witnessed lifts, and the same witnesses take the ward's opinion to its floor (`- 20 THE
  WARD`); with the flame line's `+ 24 THE DOOR` the sheet reaches 33 (HELD), and coin buys
  nothing at the bench by ruling. HELD still charges the fine (34 -> 18 Royals on
  `court-flame-serve`). The FINED judgment itself -- `PAY IT.`, the coin, `TURNED LOOSE AT THE
  MISSION'S DOOR` -- is proved in `test_hearing_page.cpp` ("the check block shows the
  weighing ...") and `test_court.cpp` ("FINED and SPARED walk out of the Mission's door").
- **The creation window after A NEW MAN is photographed by `--creation`, not by a windowed
  loop.** The headless line ends where `main()`'s loop begins; the loop is fifteen lines and
  is read, not driven.

## Register literals authored by the justice build (for the owner's redline)

Fourteen `court.*` tables in `content/raws/barks/contract_barks.json`: `court.taken` (the
officer walking you in), `court.paper` / `court.blood` / `court.roofs` / `court.nothing` (the
priest's openings), `court.plead` (the priest pressing once a plea is armed), `court.spared`
.. `court.rope` (the seven judgments), `court.lie` (a denial disbelieved). One `court.blood`
row nearly says the learned word; no common-folk line does.

## Baselines on this exe

`granadad-twin-gate --tavern --ticks 900`: `0x837E94019BC49C25` (run A == run B) -- the ONE
declared tavern/gate-workload move of the justice build, `0x86E05F527E54E795` ->
`0x837E94019BC49C25`, re-blessed once in DECISIONS.md and BASELINE-WORLD-HASH.md.
`granadad-twin-gate --population --population-hour 16 --ticks 7200`: `0x2646C1AAA2BA38DF`
(run A == run B, 18,772 bytes) -- unmoved.
