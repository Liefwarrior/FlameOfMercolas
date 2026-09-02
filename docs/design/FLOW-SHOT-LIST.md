# FLOW lane shot list — what Ship photographs, and how

Lane: `lane/ui-flow` (base 5b072dc). Everything below is evidence for
UI-EA-SPEC sec. 3 (transition grammar) and sec. 4 (enter/back law). Judge at
640x360 first. **Transitions need CONSECUTIVE frames — use `--framedump=DIR`**,
which now works in the ordinary windowed session (this lane widened it; it was
demo/watch-only). Keep framedump runs short: every presented frame is a PNG.

Set the root `granadad-controls.cfg` aside before any capture (SHIP-NOTE rule).

## Consecutive-frame sequences (`--framedump`, windowed, drive-windowed.ps1)

1. **The boot veil, both windows.** Launch plain; dump from process start.
   Expect: the creation door easing up from black over ~12 frames; later, on
   BEGIN, black falling over the sheet BEFORE the window swap, then the world
   easing up from black (Session::dressInstantCut through the travel-dip
   machinery). The 2s window gap should read as one dressed cut. Frames:
   first ~15 of the creation window, last ~15 before teardown, first ~40 of
   the world window.
2. **A page opening with motion.** On the street, press M; dump ~12 frames.
   Expect the page's ease (kPageEaseSteps = 8) — never a first-frame flash
   (EasedToggle's opening bump). Repeat for J (casebook) and ESC (pause).
3. **Creation step-change ease.** In the creation window, commit the door;
   dump ~12 frames. Expect the roster easing up from the clear rather than
   swapping — the retired `alpha = 1.0F` seam (creation.cpp:1374).
4. **The tiles close honesty.** Open J (tiled menu), press J again; dump the
   ~10 tail frames. Expect the FOUR TILES fading out — not the empty
   single-panel ghost the old handover drew (session.cpp's old :7455 seam).
5. **Back-to-opener.** ESC → CONTROLS → ESC; dump across the second ESC.
   Expect the pause menu back on screen, cursor on CONTROLS, with the panel
   never closing (one content swap, no close/open fade pair). Then ESC once
   more → street.
6. **The commit beat.** With the map up, press ENTER on a selection; dump ~10
   frames. Expect one restrained pulse on the inverted fill decaying over 8
   steps (PAGES renders it off Session::commitPulseValue). Same beat on a
   tab step (TAB on the map or casebook).

## Single stills

7. **The armed door.** At the creation door press ESC once: the armed row
   (`ESC AGAIN - LEAVE`, PAGES' copy off `CreationFlow::quitArmed()`). A
   second ESC leaves; any other press disarms.
8. **Keys↔options as siblings.** Keys page up, press TAB: the options page,
   same panel, no street flash between. And the map's tab row clicked with
   the mouse (violation #8): the view swaps.
9. **At-rest vs raised.** The same page captured twice headlessly:
   `--screenshot` default (kCaptureRestSteps = 240, the at-rest frame the
   word budgets bind) vs `--settle-steps=0` (the raised state). The pair is
   the census's own before/after.

## Determinism notes for Ship

- The boot veil is OFF under `--demo` and `--case-watch` by construction —
  their committed frames and byte-stable replays must not move. Twin-run,
  `--case`, exit codes: unaffected by this lane (no sim writes anywhere).
- Headless creation captures never advance the flow, so they draw settled at
  full alpha — byte-identical to the old hard-coded 1.0F. Only windowed play
  shows the eases.
- The keyboard's ENTER/arrow keycaps and the pad's D-pad wordings are motif
  sentinels (0x01-0x06) at the promptKey choke points now. Until PAGES'
  drawPanelText lands, an un-taught drawer shows a blank cell where the
  motif goes — photograph AFTER integration for the finished feet.
