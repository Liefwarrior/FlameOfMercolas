# Ship note — read this first

Gate: **green, both halves**, revision `efe9463`.
World hash: **byte-identical** to where this program started.

## Run the demo

```
.\dist\granadad.exe --demo
```

Two minutes, plays itself, ends on a card and closes its own window. ESC stops
it early. `dist\` already holds the gate-certified binary — nothing to build.

Want the stills too? `.\dist\granadad.exe --demo-capture=DIR`
Want one section? `.\dist\granadad.exe --demo=case` (`quay saltgate case map night end`)

Play it normally with `.\dist\granadad.exe`. On the character screen the
fastest way in is **2 (ANSWER FOR YOURSELF) → ENTER**, answer ten questions,
type a name, ENTER, then BEGIN. In the world: **M** map, **TAB** casebook,
**E** talk, **F1** keys.

## What this program fixed

| | |
|---|---|
| The ward's ten questions | Every prompt stood on a dangling `You —` and every answer was a lowercase fragment. All ten rewritten to stand alone, all thirty answers second-person declarative. Two prompts were being silently clipped at 320x180; none are now. |
| The conversation panel | Had no frame, no header, an `>` arrow the spec forbids, and **six of nine topic labels clipped** (`7 PICK THEIR POCK.`). Now framed, headed, whole labels, selection is an inverted fill in the speaker's own accent. |
| Input parity | D-pad down/left/right were bound to nothing; the stick was dead under every page; every trigger was dead under every page; and a left click on a casebook lead **threw a punch**. All fixed; mouse hover and click now work on the map and casebook. |
| Polish | Empty panes were flat black holes — now a stippled field with a centre. Four ground-alpha constants became one, killing the world-bleed on the controls page. The Weighhouse got a name (on the cursor, not the map face). A street sign stopped being cut in half by the crosshair. |
| The demo | New: a two-minute curated route that plays itself and ends on a card. |
| **The shutdown crash** | **Found and fixed by this pass.** The windowed client exited `0xC0000374` (heap corruption) or `0xC0000005` (access violation) — every time, on the way out. Pre-existing since the audio wiring pass. See below. |

## What did NOT get fixed, and why

1. **A controller cannot start the game.** The character sheet needs a typed
   name and there is no on-screen keyboard, so a pad-only player is stuck on
   the first screen. Needs an OSK — a feature, not a polish item.
2. **The quiz's master pane is ~two-thirds empty at 640x360**, and it is the
   first screen anyone sees. Textured now rather than black, but large. Fixing
   it means sizing the quiz body to content, which fights the "hold height
   where a cursor swaps content" rule — a real design decision, not a bug.
3. **The casebook is near-empty on a new game** (one lead). It looks superb
   once you have five or six. First impression only.
4. **Six pages have no mouse hit-test** — keys, options, grimoire, wait, pause,
   conversation. They swallow clicks rather than punching through, which is
   honest but not clickable.
5. **`menu_view.cpp`'s four tiled panels still draw the old arrow highlight** —
   the last surface in the build not converted to the terminal grammar.
6. **`--demo-capture` drops `creation-origin.png` on most runs.** A shutter
   race on the character screen; the demo itself plays the beat every time.
7. **No camera motion anywhere in the demo.** It cuts, walks and turns; it
   never dollies on a still.

## The three things I would do next, in order

1. **Size the quiz's master pane to its content** (or move the detail pane's
   summary into the empty half). It is the first screen of the game and the
   largest single "unfinished" tell in the build. Everything else on this list
   is seen later or by fewer people.
2. **An on-screen keyboard for the name field.** It is the difference between
   "controller supported" and "controller supported except you cannot start".
3. **Lift the input router out of `main.cpp`'s anonymous namespace** into
   `granadad-render` so a suite can link it. `route_menu_key`, the stick latch
   and the pointer code are the least-tested and most-recently-changed input
   code in the build, and today **no test touches a line of it**.

## The shutdown crash, in one paragraph

`run_client()` held the audio engine in a function-scope `unique_ptr`, so it was
destroyed on `return` — *after* `SDL_Quit()` had already torn down the audio
subsystem and the callback thread the engine's stream belonged to. Closing a
stream SDL has freed is a use-after-free. `session.hpp`'s own `setAudio()`
header already stated the contract ("main.cpp detaches before its engine goes
away"); main.cpp never did. It stayed invisible because `--smoke` and every test
are headless and never open a device, and a human closing the window got the
crash *after* the window had gone, where it reads as Windows tidying up.
`--demo` is what made it reproducible, because it plays to the end and then
quits on its own. Fixed in `efe9463`: detach, drop the engine, then let SDL go.
All 23 demo frames are byte-identical to the crashing build's, so nothing about
what you see changed — only whether the process survives the exit.
