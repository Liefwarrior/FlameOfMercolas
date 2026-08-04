# Granadad: The Darkstreets

*(project codename: Flame of Mercolas. The engine's marketing name is the **Mercolas Engine** —
a name only: nothing in the source tree is called that, and nothing should be renamed for it.)*

A first-person investigation **sandbox** set in the Docks district of **Granadad**, capital of
Trojia, from the novel *Lord of Trojia*. The player is canonically Devin but plays as whoever
they choose, on the trail of a hidden bloodletter, with Elder-Scrolls-style freedom.

Underneath it is a deterministic tile simulation of the district and the ~692 people in it, who
have jobs, homes, relationships and an economy, and who carry on whether the player is watching
or not.

## Where the code is

The game is being rewritten in C++. **`native/` is the live tree**; read
[`native/README.md`](native/README.md) for the two ways to build it and what the gate covers.

```
docker compose run --rm --build build      # reproducible build -> dist/
.\dist\granadad.exe                        # the game
.\dist\granadad.exe --smoke=120 --screenshot=frame.png   # a frame, no window needed
.\dist\granadad.exe --nemesis --screenshot=rival.png    # lose three fights, watch him rise
.\dist\granadad.exe --ward                # the compounds, two years, as text
.\scripts\verify-windows.ps1               # finishes the cross-toolchain determinism gate
```

`granadad.exe --help` lists every scripted line the build plays back with no window:
`--trail`, `--flame`, `--skyrun`, `--contract`, `--roofs`, `--nemesis`, `--burgle`.

**Start here: [`PLAY.md`](PLAY.md)** -- how to launch it, what to try in the first ten
minutes, and an honest account of what is and is not fun yet.

You spawn on the Tarwalk six tiles off the door of the Gilded Gull, the captains' tavern, at
dawn, with a case open and one lead in it: a body came up against the outfall grate at low
tide. `Q` looks at what is in front of you, `J` opens your casebook, `F1` lists every key.
The trail is twelve leads long, two of them dead ends that are the point, and it runs from
the Mission's back room to the doors of a warehouse that has been condemned for nine years.

Walk into the Gull: `E` talks and does business, `F` throws a punch, `R` sleeps in a room you
have rented. There are sixteen people in there who keep hours, and two of them will put you
out of the door if you make them.

Lose a fist fight in there and the man who put you on the floor gets something for it: a rung
on his own guild's ladder, weight in the ward that its rival loses, and -- if you keep going
back -- a trade house of his own with a permanent cut of what a mug costs you, and his name on
the compound roll as the Den Duke of the Gullet. You get up on the quay a few hours later. He
keeps all of it.

The Java tree (`sim-core/`, `client-observer/`, `tools/`, `headless/`) stays in place, untouched,
as the behavioural reference until the C++ reaches parity. It is not the thing being built.

| Directory | What it is |
|---|---|
| `native/` | The C++ game: format reader, simulation, software first-person renderer, SDL3 client. |
| `content/` | **The owner's authored canon. Read-only.** Raws, barks, notables, names, rumors, factions, skills, spells, jobs, quests, the Tiled sources and the baked worlds, and the art packs. |
| `docs/design/` | Binding rulings (`DECISIONS.md`), the setting bible (`DOCKS-GAZETTEER.md`), the feel bar (`COMBAT-FEEL-REFERENCE.md`). |
| `ARCHITECTURE.md` | The 2026-07-12 design, reconciled against the code on 2026-07-31. Every claim is marked BUILT / PARTIAL / NOT BUILT / SUPERSEDED. **Trust the markings.** |

## The constraints that are not negotiable

- **No float or double in simulation state, or in any math that affects it.** Integers and
  fixed-point only. Floats are legal in the renderer and nowhere else. This is what makes the
  world hash identical across toolchains, and it is proven across two of them on every build.
- **The player's position is sub-tile fixed-point and lives in the simulation** — `(tile << 8) | sub`,
  256 steps across a tile. Never client-only float state. Ruled by Eli 2026-07-31 after a
  frame-by-frame Barony study; the reasoning is in `docs/design/COMBAT-FEEL-REFERENCE.md` §2.
- **No `std::unordered_map` or friends anywhere in `native/`.** Iteration order would become a
  property of the standard library, and the twin-run gate is structurally blind to that. Enforced
  by grep in the build.
- **`content/` is never modified.** New files may be added; nothing existing is edited or deleted.

## Design north star

> Social power is maxed from the start; physical power starts near zero and grows through
> exploitable systems — never through levels. Authority and law are first-class domain concepts.

Visually: **Barony-grade, sharper.** Chunky readable sprites over an authoritative tile
simulation, committed dark — torchlit pools against near-black, architecture as silhouette,
harbour fog.

## Canon

Lore lives in the sibling repo `..\LordOfTrojia-MVP` (read-only reference; the novel always wins;
the `Lore\*.html` files there are non-canon). Derived game data in this repo carries provenance
notes.
