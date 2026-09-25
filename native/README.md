# Granadad: The Darkstreets — native C++ build

The C++ rewrite — the **Mercolas Engine**, which is a marketing name and not a
directory: nothing in here is called that and nothing should be renamed for it.
The Java build stays in place, untouched, as the behavioural reference until
this reaches parity.

There are two ways to build. Use docker when you want a binary you can trust and
hand to someone; use the CMake presets when you are writing code and want a
fast loop.

---

## 1. Docker — the reproducible build

From the repo root:

```
.\scripts\gate.ps1         # scripts/gate.sh off Windows
```

That is the whole thing. It is `docker compose run --rm --build build` with
`GRANADAD_REVISION` set to the commit you are on (`-dirty` appended when a
tracked file differs from it, the rule `git describe --dirty` uses), so
`dist/GATE-STAMP.txt` and `granadad.exe --version` name the tree that was built.
The container cannot find that out for itself: `.git` is not in the build
context. Type the compose command by hand and it builds exactly the same; the
stamp just says `unknown`.

Either way it compiles the C++ inside a pinned container, runs the test suite,
and puts a native Windows binary in `dist/`. Then, on Windows, natively:

```
.\dist\granadad.exe --selftest     # no window, just proves the binary works
.\dist\granadad.exe                # the game
.\dist\granadad.exe --smoke=120 --screenshot=frame.png   # a frame, no window
```

`--screenshot` opens no window, needs no GPU and needs no display server,
because the renderer is software. That is the point of it: every sprint has to
be able to prove visually that it works, and a capture path that needs a
desktop is a capture path nobody runs. `--help` lists the rest — `--time`,
`--spawn`, `--yaw`, `--fov`, `--width/--height/--scale`.

`--talk`, `--topic=N[,N...]`, `--offer=N`, `--again` and `--flame` drive a
conversation before the shutter goes, through exactly the calls a keypress
makes. That is how a sprint photographs the thing it built rather than
describing it. **`--topic` is one-based**, matching the numbers printed beside
the topics on screen; it was zero-based in S3 and the review caught it.

`--flame`, `--roofs` and `--skyrun` play whole scripted lines and capture
wherever they finish, walking the body with real movement steps along routes the
ROOM'S OWN pathfinder produced. **They report how many beats landed and exit
non-zero when a run falls short** — S4's did neither, and a run that landed zero
of six stages photographed the wrong person and exited 0. The PNG is still
written when a run falls short, because a frame of a short run is evidence of
the shortfall.

```
granadad --smoke=0 --hold --time=20 --flame --screenshot=flame.png
granadad --smoke=0 --hold --time=20 --roofs --screenshot=roof.png
granadad --smoke=0 --hold --time=23 --skyrun --screenshot=skyrun.png
granadad --burgle=lock --screenshot=burgle.png
```

`--flame` needs no `--spawn`: it walks in from the authored spawn on the Tarwalk
like anybody else. It did not in S4, and this README documented a command that
could not work.

`--roofs` climbs onto the roof of the Gilded Gull and looks down at the ward —
in at the door, up the stair with `SPACE`, across the guest floor, out over the
north wall. `--roofs=leap` carries on across two tiles of alley onto the next
house; `--roofs=street` takes the two-storey drop back down.

`--skyrun` plays the Skyrunner line end to end at eleven at night: sign on with
Finch in the snug, take two purses, crack a guest's box above the stair, get on
the roof, cross the alley, work the roofs until they will have you as a
Cutpurse, sell what you took, lean on somebody, and carry a bale out of the door
past Watchman Cull. Nine stages, and it reports what it earned:

```
roofs stages=9 rank=2 Cutpurse standing=100 watch=-63
climbs=1 lifts=2 cracks=1 leans=1 fences=1 runs=1 heat=60 warrant=yes skyrunning=5
```

```
granadad --smoke=0 --hold --time=21 --spawn=155,69,19 --yaw=225 \
         --topic=5 --again --screenshot=caught.png
```

— walk up to Sella Brinewall at nine at night, put a hand in her purse, get
caught, and say hello again. She is (HOSTILE) in red and greeting you out of a
different authored table.

`--trail` **walks the bloodletter trail** — the investigation the district is
about. Every beat is the same two calls a keyboard makes: the district's own
breadth-first router to a lead's site, then `Q`. It reports how many leads it
READ against how many it WALKED TO, and those differing is a hard failure,
because it means a site in `content/raws/quests/casebook.json` cannot be reached
on foot:

```
trail read=10/10 leads=12 cold=3 unreached=0 dread=100 closed=yes
flame=3 called=WIELDER OF THE FLAME notes=shut
```

Ten leads close the case without leaving the quayside plane; the remaining two
sit one band down on the strand and this line does not climb, so it says so.
`--trail=start` captures the opening page of a new game, `--trail=keys` the
in-game controls, `--trail=notes` the casebook, and `mission`/`weighhouse`/`hold`
stop after that lead.

`--burgle` robs the Gull, and it sets its own clock to **two in the morning**
because that is the only state of this building a burglary is possible in: the
doors have just been barred, the lanterns and the table candles are out, and the
night staff are still inside to be crept past. Seven beats — crouch, a dark
doorway nobody can make you out in, a hand in a coat, up the stair, the wire into
a guest's box, the lock open, the box emptied — and it prints which ones landed
as a bitmask plus which way the lift went:

```
burgle beats=7/7 mask=127 lift=tried light=2 noise=88 hidden picks=0
locks open=4 jammed=4 forced=4 cracked=4 cracksmanship=3 skyrunning=1
```

`jammed=4 forced=4` is the game working, not the demo failing: a scripted
burglar starts with CRACKSMANSHIP at zero, so he searches the pins blind, breaks
all five picks, jams the lock and has to put a shoulder to it — loudly, and for
half the coin a clean pick would have paid. `--burgle=lock` leaves the wire in
the next box so the lockpicking row itself is on screen; `--burgle=taproom` walks
back down among the people who did hear him.

### Controls

| | |
|---|---|
| `W A S D` | walk and strafe |
| mouse | look |
| `Left` / `Right` | keyboard turn, 65 deg/s |
| `Shift` | run |
| `E` | talk to whoever is in front of you |
| `F` | throw a punch. Under a roof with bouncers in it, that is an offence |
| `R` | sleep, if you have rented a room and are standing in it |
| `Space` | **up.** Mantle onto the ledge you are facing, or leap the gap in front of you — the geometry decides which. It also takes a stair |
| `X` | **down.** Step off the ledge in front and take the fall |
| `G` | put hands on what is here: a guest's strongbox at a bed-foot (**locked since S9** — this puts the wire in), the bale in the snug, a downed rat, or a set of picks off Finch |
| `C` | **crouch.** Half speed, and worth more than twenty levels of skill |
| `T` | **lift.** A hand in the coat of whoever is at your elbow, with no conversation open |
| `Q` | **look at what is here.** The investigation verb — see `--trail` |
| `J` | **your casebook.** Every lead you have been told about, and where to go |
| `F1` | **the keys**, in the game, paged nine at a time. You should not need this table |
| `Tab` | release the mouse |
| `F12` | screenshot to `granadad-screenshot.png` |
| `Esc` | quit |

**While the wire is in a lock the keyboard belongs to the lock.** Same shape
the conversation and the workbench already use: a mode the *simulation* is in,
which the client reads.

| | |
|---|---|
| `W` / `S` (or `Up` / `Down`) | raise and lower the pick along the nine-depth track |
| `Space` | probe at that depth. A pin drops, or the wire strains |
| `F` | put a shoulder to the lid. Always works, loudest thing in the building, costs half the coin |
| `Esc` | take the wire out. The lock relocks |

**While somebody is talking to you the keyboard belongs to the conversation.**

| | |
|---|---|
| `1`–`9` | pick the topic printed with that number |
| `0` | turn to the next page — the row that says `0 MORE (2/3)` |
| `Up` / `Down` (or `W` / `S`) | move the cursor; it turns the page with you |
| `E` / `Enter` | pick the topic under the cursor |
| `Esc` | end the conversation (it does not quit the game) |
| `F` | punch them, which also ends the conversation |

**The list is PAGED, nine to a page, and that is a fix rather than a style.**
S3 drew a twelve-slot grid numbered `1`–`9` and then three rows whose number was
a full stop, reachable only by the arrow keys with nothing on screen saying so —
and Master Venn already filled all twelve, so the thirteenth topic would have
vanished with no ellipsis at all. Nine is the page because those are the keys;
the tenth row names the key that turns it; and a list of any length is now
completely addressable from the keyboard.

**The workbench** replaces the topic list when the priest opens it. Five fields
— what is moved, how it is moved, how much, for how long, and across what link —
priced live by canon's own cost model:

| | |
|---|---|
| `Up` / `Down` (or `W` / `S`) | walk the five fields |
| `Left` / `Right` (or `A` / `D`) | change the value under the cursor |
| `E` / `Enter` | make it |
| `Esc` | put the tools down |

A composition the rules refuse is refused **out loud**, in the priest's own
authored voice with the reason bracketed after it, and the bench stays open so
it can be fixed. Being told why is the lesson.

**Haggling** replaces the topic list with a counter:

| | |
|---|---|
| `Left` / `Right` | name a lower or higher number (`Shift` moves it five at a time) |
| `Enter` / `E` | say it |
| `T` | take their price without arguing |
| `Esc` | walk away, which they remember |

**The roofs are a road.** `SPACE` mantles you one band onto the top of the wall
you are facing — a solid face to grip, a surface on top of it, room over your own
head, and one band only, so the street cannot climb a two-storey frontage. When
there is nothing to grip it leaps instead, across up to three tiles of air,
aiming at the far roof and taking the alley only when there is no far roof. `X`
steps off a ledge and falls up to three bands; past what your legs and the
Skyrunners' teaching allow, it hurts. Nothing in any of the three rolls a die:
what `skyrunning` and the roofs' own `roof` token buy is a band of safe drop and
a tile of carry.

The district was two thirds shut before that. 8,132 standable cells of the baked
Docks — the compound roof decks and the rooftop slums — could not be got to at
all, which `docs/design/DOCKS-GAZETTEER.md` §2.6 filed as deliberate "until the
law/economy layers learn to climb (S5+)". Reachable-from-spawn is **24,960**
tiles now against 17,054 on foot, and the roof-slum plane goes from **zero**
reachable cells to 1,664.

You spawn on the Tarwalk six tiles off the door of **the Gilded Gull** (K03), the
captains' tavern. Walk in. It has sixteen people in it who keep hours: Master
Venn on the stair, Gerta Saltcotte behind the bar, two bouncers on a rota that
overlaps for the loud hours, Father Maell for an hour in the evening, Captain Ivo
Wake of the *Kestrel* from seven, Watchman Cull off duty from nine, and Finch the
quiet tenant in the snug after ten, who will not talk to you until you have
stopped being a stranger. The fire is lit from ten in the morning until three. Come at
five and the room is black and empty.

**Every word any of them says was written by the owner**, in
`content/raws/barks/barks.json`. Nothing in the C++ writes dialogue; it picks an
authored key and reads a row. Which key depends on their trade, on the hour, and
on what they think of you — so the same docker greets you out of a different
table once you have robbed him. What they will TALK about depends on what the
raws say they are allowed to know: Master Venn is party to one of the ward's
fifteen authored micro-histories and licensed by
`content/raws/rumors/rumors.json` to repeat two more, so he has three stories on
his list and Captain Wake has none. **That is the whole gate. There is no
persuasion check anywhere in the game** — if you cannot get an answer you are
standing in front of the wrong person.

They remember. Stand a docker a drink and he warms to you; put a hand in his
purse and get caught and he will not speak civilly to you again, everyone in the
room saw it, and the ward hears about it (top right corner). A hostile landlord
does not pour, whatever you offer him. A friendly one charges less for the same
bed, with no haggling at all — and if you do haggle, how far he comes down is
your **streetwise** against his, and your streetwise goes up by haggling.

Throwing a punch gets you warned to your face and then physically put out of the
door. That is a brawl, and it resolves in the world with no transition. Draw a
blade and it stops being a brawl — see `--help` and
`native/include/granadad/sim/brawl.hpp` for the rule, which is explicit and
tested on its own. The room remembers that too, and remembers it harder than
anything else on the list.

> **`run`, not `up`.** `docker compose up` exits **0 even when the container
> inside it exits 1** — it prints `build-1 exited with code 1` and then hands
> your shell a success. A build gate that cannot go red is worse than no gate,
> because it gets trusted. `docker compose run` propagates the container's exit
> status by construction, with no flag to forget. (`docker compose up --build
> --exit-code-from build` is equivalent if you type all of it every time.)
>
> If you type `up` from muscle memory anyway, you still get a red exit. The
> compose file carries a second service, `guard`, whose only job is to depend on
> `build` completing successfully — and compose *does* fail when a dependency
> fails, even though it forgives a container that exits 1 on its own. Belt and
> braces; `run` remains the documented command.

### What docker is and is not doing here

Docker is the **build surface, not the runtime**. The container compiles and
exits. It never runs the game, never opens a window, never needs a GPU and never
wants an X server. What comes out is a **native Windows `.exe` (PE32+)**,
cross-compiled with mingw-w64 — not a Linux ELF, not a WASM/browser target.

The build will not let that degrade quietly. Before publishing anything it
checks each artifact is a real PE32+ binary *and* that its import table contains
no MinGW runtime DLLs. If either check fails the build stops rather than putting
a broken file in `dist/`.

The 1.2 GB `content/` tree does not enter the container — with one deliberate
exception: `content/maps/baked/`, three files totalling 21 KB. Those are the
owner's real shipped worlds, and the TROJSAV reader's tests open them and assert
against facts read out of the actual bytes. Without them in the context those
tests cannot run at all, and the build gate shrinks to "fixed.hpp compiles"
while still printing green. Everything else, `content/art` included, stays out;
the game reads it at runtime, from the repo, on your machine.

Nothing in the build writes to `content/`. It is mounted nowhere and copied
read-only into the image.

### What the gate actually covers

`ctest` runs on a **host (Linux) build of the same sources** — a cross-compiled
`.exe` cannot execute on the build machine, so correctness is proven on a native
build and reproducibility on the cross build.

| | cases | assertions |
|---|---|---|
| `granadad-tests` — RNG, wrapping, world hasher, engine, gate, angles, tile queries, the body, the roofs, lamps, atlas, renderer, the room, the ward's voice, the guilds, the crimes | 288 | 355,000+ |
| `granadad-content-tests` — TROJSAV reader vs. the real baked worlds | 57 | 902,056 |
| `granadad-twin-run-gate` — two entries: bare, and with the tavern registered | 2 | — |
| `granadad-content-fingerprint`, `granadad-world-hash-fingerprint` | 2 | — |
| **ctest total** | **349** | |

Some of those cases **render frames of the real Docks**, inside the container,
with no window and no GPU — the renderer is software, so a frame is an ordinary
testable artifact. The build also checks by name that the first-person case and
the case that loads `content/art/custom` are registered, because a renderer
silently falling back to procedural tiles would still be green.

The build prints the assertion counts every run, so "tests passed" never has to
be taken on faith. It reads them out of ctest's own log rather than running both
suites a second time — that duplicate run was described in a comment as "half a
second", which was true in M1 and had become the whole sim suite twice by #80.
It also asserts a **floor** on the case count and checks by
name that four things are registered: the cases which load
`docks_surface.trojsav`, the case that compares the C++ world hash against the
JVM's, and the twin-run gate. The suite used to be one test (fixed.hpp) while
`ctest` printed `100% tests passed, 1 tests out of 1`, and it must not be able
to shrink back there quietly.

Note the shape of the M1 jump from 58 to 128: `granadad-tests` was registered
with a plain `add_test()`, so it counted as **one** entry no matter how many
cases it held — adding thirty would have moved the floor by zero. It was
`doctest_discover_tests`-ed per case after that, and per **file** since #81.

### One ctest entry per test FILE, and where the time goes (#81)

`doctest_discover_tests` registers a ctest entry per `TEST_CASE`, and ctest runs
an entry by **launching the binary again** with `--test-case=`. 520 cases meant
520 processes, each re-reading the baked world, re-parsing the raws and
re-baking a 661-body ward. Worse, it made a shared fixture impossible — a
file-static is built once per *process*, so five cases reading one soak paid for
that soak five times, and #80 had to fold five named claims into one case to
stop paying.

The entry is now the test file: `--source-file=*test_x.cpp --order-by=file`,
36 sim entries and 6 content ones. Cases in a file share one process (which is
what `tests/support/ward_fixture.hpp` is for); a crash still only takes down its
own file; the order cases run in is the order they are written in.

Two things move with it:

* **the floor counts doctest CASES, not ctest entries.** `--list-test-cases`
  out of each binary, plus the standalone entries ctest still owns. The same
  arithmetic over the old suite gives 537, which is what ctest said it was — an
  entry count can be inflated by registering entries, a case count cannot.
* **`granadad-test-partition-is-complete`** adds up what every per-file entry
  actually matches and requires it to equal the binary's own total. A filter
  that matches nothing runs nothing and exits 0; that is now arithmetic instead
  of trust.

Measured on the Debug host build the gate compiles, `ctest` serial:

| | before | after |
|---|---|---|
| `ctest` wall clock | 1,229 s | 700 s serial / ~245 s at `-j 8` |
| the single most expensive case | 439.6 s | 5.2 s for the whole file it moved into |

Where the remaining time goes, and it is nearly all one thing: **a ward tick
costs what the hour costs.** Measured, /600 ticks, Debug: 1.2 ms at 08:00,
6.2 ms at noon, 12.4 ms at 20:00, **31 ms between 22:00 and 04:00** — and it
tracks A\* expansions per tick exactly (420 at 08:00, 11,884 at 01:00, with a
0.3% search-failure rate, so it is work and not thrashing). A tavern tick is
5 µs and a compound-roll tick rounds to zero. Anything expensive in this suite
is the ward, at night.

That is why `the tavern empties itself between closing and dawn` was 36% of the
entire gate: it ran five hours of world at 01:00 through a `render::Session`,
which registers the ward, for four assertions about a taproom. It runs on a
`Tavern` on its own engine now — same objects, same hours, same assertions.

What is left is honest cost: the ten-hour food-chain soak (238 s) and the
ward-behaviour cases that have to run the district for real. **A case whose
subject is not the hour should buy the cheapest hour there is.**

It greps all of `native/` for `std::unordered_map` and friends and fails on a
hit. That ban cannot be enforced by the twin-run gate: an unordered container's
iteration order is a function of the keys and the insertion sequence, so two
runs in one process agree with each other and diverge only against another
machine or another standard library. The gate covers what grep cannot; grep
covers what the gate cannot.

And it checks the **compile lines themselves**, out of `compile_commands.json`,
printing them as it goes:

- `miniz_tinfl.c` must carry `-fwrapv`, `-ffp-contract=off`, `-fno-fast-math`
  and `-fno-strict-aliasing`
- our own `trojsav.cpp` must carry `-Werror` and at least one `-isystem`
- third-party source must **not** carry `-Werror`

Every one of those was wrong at some point, and none of them is visible in a
green test run.

### What the gate does *not* cover: the toolchain that ships

Everything above runs under Linux/GCC. The binary you actually run is built by
mingw-w64. sim-core bans `float`/`double` precisely so those two decode the same
bytes to the same world state — and until task #75 that claim had **never been
tested across two toolchains**. The cross build compiled
`granadad-content-tests.exe`, linked it, and threw it away, because the content
directory was a compile-time constant naming a path inside the container.

It is no longer a constant. `fixtures.hpp` reads `$GRANADAD_CONTENT_DIR` at run
time (falling back to the configure-time path), the `.exe` ships in `dist/`, and
you finish the gate on the host:

```
powershell -ExecutionPolicy Bypass -File .\scripts\verify-windows.ps1
```

Or run **both halves as one command**, which is the version that does not
depend on you remembering:

```
powershell -ExecutionPolicy Bypass -File .\scripts\verify-windows.ps1 -Build
```

Suites passing is the weak half of that — `57 passed` here and `57 passed`
there are equal strings no matter what the two binaries decoded. So both sides
emit **two reports**, and the script compares each pair byte for byte:

```
.\dist\granadad-content-tests.exe --fingerprint <file>   # decoded state
.\dist\granadad-twin-gate.exe     --fingerprint <file>   # hash + a run
```

Per shipped world, that report carries the section CRCs of what miniz actually
inflated, a CRC32C over **every decoded lane**, and per-form / per-material /
per-flags / per-fluid histograms over all ~2.7 million tiles. Anything that
differs in the decode — byte order, shift signedness, `char` signedness, integer
promotion, miniz's output — moves a number in it. The report deliberately
contains no paths, no timestamps and no platform banner, is formatted without
libc, serialises multi-byte values little-endian by hand, and is written in
binary mode, so that comparing the bytes means what it says.

The second report is M1's. The first proves the two toolchains read the same
bytes; the second proves they then **hash** those bytes to the same 64 bits and
that a fixed 240-tick simulation run over them ends in the same place. Both
claims needed proving on both platforms, not one.

**A difference there is a real finding, not a flaky test.** Do not regenerate
either side to make them agree.

The container cannot run this itself: executing a PE binary under wine would
prove something about wine. `publish.sh` prints the command as the next step,
and the build fails if `granadad-content-tests.exe` or the Linux report is
missing from `dist/` — but nothing forces you to type it.

### Output

| File | What it is |
|---|---|
| `dist/granadad.exe` | The game. Self-contained — no DLLs to ship beside it. |
| `dist/granadad-bake-lamps.exe` | Re-derives a fixture's baked light sources from its authored `.tmx`. Run by hand; the output is committed beside the world. |
| `dist/granadad-tests.exe` | The sim suite — RNG, hasher, engine, gate — as a Windows binary. Needs `$env:GRANADAD_CONTENT_DIR`. |
| `dist/granadad-content-tests.exe` | The TROJSAV reader suite + `--fingerprint`, as a Windows binary. Needs `$env:GRANADAD_CONTENT_DIR`. |
| `dist/granadad-twin-gate.exe` | The twin-run determinism gate + `--fingerprint`. Needs `$env:GRANADAD_CONTENT_DIR`. |
| `dist/content-fingerprint-linux-gcc.txt` | The Linux/GCC decoded-state report, to compare against. |
| `dist/world-hash-linux-gcc.txt` | The Linux/GCC world-hash + simulation-run report, to compare against. |
| `dist/BUILD-MANIFEST.txt` | Revision, toolchain versions, sha256 of each artifact. |

### How reproducible, exactly

Pinned hard:

- the base image, by **sha256 digest** — not the `:bookworm-slim` tag, which moves
- every dependency, by **full commit SHA** — not a tag, which can be moved under you
- build paths, via `-ffile-prefix-map`, so `/work` and `C:/repositories` give identical bytes
- the PE link timestamp, via `-Wl,--no-insert-timestamp`, which otherwise alone
  makes two identical builds hash differently

Verified: two full `--no-cache` builds produced **byte-identical** binaries.

Not pinned: the exact Debian package versions of gcc/cmake/ninja. Pinning them
breaks the build outright the moment Debian ships a security update and drops
the old version from the mirror — a bad trade for a command that is supposed to
work whenever you type it. The versions in use are printed by the build and
recorded in `BUILD-MANIFEST.txt`, so drift shows up instead of hiding.

### Options

```
BUILD_TYPE=Debug scripts/gate.sh
GRANADAD_REVISION=$(git rev-parse --short HEAD) docker compose run --rm --build build
```

Anything in the environment passes through the wrapper (`$env:BUILD_TYPE='Debug'`
then `.\scripts\gate.ps1` on Windows). The second line is what the wrapper does
for you, minus the `-dirty` check; `GRANADAD_REVISION` gets baked into the
binary and printed on startup, so a bug report names the commit it came from.
Unset, it is `unknown`.

---

## 2. CMake presets — the fast local loop

Needs cmake ≥ 3.24, Ninja, and a C++20 compiler on your PATH.

```
cd native
cmake --preset sim-only          # configure
cmake --build --preset sim-only  # build
ctest --preset sim-only          # run the tests
```

| Preset | For |
|---|---|
| `sim-only` | Simulation + tests, **no SDL fetched at all**. The quickest loop; use this by default. |
| `local` | Everything including the SDL3 client, RelWithDebInfo. |
| `local-debug` | Same, Debug. |
| `mingw-cross` | Cross-compile to Windows. Needs a mingw-w64 toolchain; on Windows just use docker instead. |

`sim-only` is the one to reach for. SDL3 is by far the heaviest dependency and
deterministic integer math does not need a window to be tested.

**Every preset is Ninja.** The hidden `base` preset they all inherit from sets
`"generator": "Ninja"`, and a preset's generator wins over the `CMAKE_GENERATOR`
environment variable — so exporting that does nothing here. This is deliberate:
Ninja is what the docker build uses, and one generator across both paths is one
fewer difference between "works locally" and "works in the gate". If you need
another (Visual Studio, Xcode, Makefiles), do not edit this file — write a
`CMakeUserPresets.json` next to it, inherit the preset you want, and override
`generator` there. That file is yours, is git-ignored, and cannot break anyone
else's build.

`CMakePresets.json` declares schema `"version": 5`, which is CMake **3.24** —
the same floor as `cmake_minimum_required(VERSION 3.24)` in `CMakeLists.txt`. It
used to say `6`, quietly requiring 3.25 from anyone who used a preset while the
project claimed to support 3.24. Keep the two numbers in step: raising one means
raising the other on purpose.

> **These preset paths are unverified.** The machine this was authored on has no
> cmake, ninja or C++ compiler installed, so nothing outside the container was
> ever executed. The presets are believed correct but have not been run. The
> docker path, by contrast, is verified end to end.

---

## Layout

```
native/
  CMakeLists.txt
  CMakePresets.json
  cmake/
    Dependencies.cmake        pinned deps, by commit SHA
    Determinism.cmake         the flags that protect the world hash
    StaticRuntime.cmake       self-contained, timestamp-free Windows exes
    toolchain-mingw-w64.cmake Linux -> Windows cross-compile
  content/                    the TROJSAV / world-format reader. links nothing of ours.
  include/granadad/sim/       simulation headers
  include/granadad/render/    renderer headers
  src/sim/                    simulation. no floats, no SDL.
  src/render/                 the software first-person renderer. floats legal. no SDL.
  src/tools/                  bake tools, run by hand
  src/client/                 the SDL3 window, and nothing else
  tests/
```

The dependency arrow only ever points one way: `client -> render -> sim ->
content`. `granadad-render` links no SDL on purpose, which is what makes
`--screenshot` and the in-gate frame tests possible; `granadad-sim` links no
renderer, which is what keeps a float out of the world hash.

---

## Two constraints the build enforces for you

Neither of these is left to discipline, because both fail silently and both
corrupt the cross-platform world hash.

**Signed overflow.** It is undefined behaviour in C++ but well-defined
wraparound in Java, and the Java reference build leans on int wrap in places.
Left alone, the optimiser assumes overflow cannot happen and deletes the very
branches that handle it. So: `-fwrapv` is on, and `fixed.hpp` provides explicit
`wrap_add` / `wrap_sub` / `wrap_mul`. Use the helpers — treat `-fwrapv` as the
second line of defence, not the first.

**Floats.** Banned from simulation state and from any math that affects state.
Fixed-point (Q16.16) and integers only; floats are legal in the renderer and
nowhere else. `-ffp-contract=off` stops the compiler fusing `a*b+c` into an FMA
that rounds differently on someone else's machine, and fast-math is off.

`fixed.hpp` also has `floor_div` / `floor_mod`, because C++ truncates toward
zero and tile grids want floor — tile −1 belongs in chunk −1 at offset 31, not
chunk 0 at offset −1.

The compiler flags are necessary but not sufficient. They will not stop anyone
typing `double` into a sim struct; that is the lint pass's job, and it does not
exist yet.

### Who gets which flags

`Determinism.cmake` splits them, because the two halves have opposite audiences:

| | codegen (`-fwrapv`, `-ffp-contract=off`, `-fno-fast-math`, `-fno-strict-aliasing`, `-ffile-prefix-map`) | warnings + `-Werror` |
|---|---|---|
| our code | yes | yes |
| **miniz** | **yes** | no |
| SDL3 | no | no |
| header-only deps (json, doctest, stb) | inherited — they compile inside our TUs | n/a |

miniz is on that list because it is the zlib inflate behind every TROJSAV
section: the bytes it returns *are* the world. Until #74 it compiled with none
of our flags. `-fwrapv` and `-fno-strict-aliasing` are the two that earn their
keep there; the floating-point flags are provably inert (miniz 3.1.2 contains no
`float` or `double` at all) and applied anyway, because they cost nothing and
nobody will re-run that grep after the next version bump.

`-Werror` stops at code we wrote. Pointing it at somebody else's C90 turns every
upstream bump into a build break, and that ends with `-Werror` being switched off
for everybody. Dependency headers are marked `SYSTEM` so their warnings never
reach us — miniz alone leaks 19 `-Wunused-function` warnings out of `miniz.h`.

`-DGRANADAD_WERROR=OFF` exists for one case: a compiler this project has never
seen inventing a warning in a system header, where the alternative is a developer
who cannot build at all. The docker gate passes `-DGRANADAD_WERROR=ON`
explicitly, so a stale cache cannot switch it off behind anyone's back.

---

## Dependencies

All fetched by CMake FetchContent, all pinned to full commit SHAs in
`cmake/Dependencies.cmake`.

| Dep | Version | For |
|---|---|---|
| SDL3 | release-3.4.12 | window, input, audio — client only |
| miniz | 3.1.2 | zlib inflate for TROJSAV sections |
| nlohmann/json | v3.12.0 | raws loading |
| doctest | v2.5.3 | tests |
| stb | pinned commit | PNG decode |

## Current state

**Playable enough to walk around.** `granadad.exe` loads the owner's baked
Docks, puts a body on Tarwalk and draws the district in first person: walls,
floors, roofs, the harbour, the sky, the 27 authored lamps, a day/night curve
and harbour fog. `W A S D` and the mouse move it; walls stop it; ramps and
stairs take it between the three walk bands.

What is real underneath:

| | |
|---|---|
| format reader | TROJSAV container, chunk codec, lanes, overlays. Verified against the shipped bytes on two toolchains. |
| simulation | counter-based RNG proven bit-equivalent to the JVM, wrapping helpers, world hasher, phased tick loop, twin-run gate. |
| movement | Q8 sub-tile body, integer trig, axis-separated collision against the real tile geometry, a climb rule that only goes up where a ramp or a stair was authored. |
| renderer | software voxel-column first person, the owner's own tile art, lamp glow, billboards, a HUD that hugs the edges. |
| capture | `--smoke=N --screenshot=PATH`, with no window anywhere in it. |

On top of that, as of S4: the Gilded Gull with fourteen people in it who keep
hours and remember what you did; conversation as a list of topics assembled from
the owner's authored content; five factions with a ladder each, standing, rank
and an influence over the ward that moves what a counter charges and how long a
bouncer waits; and the Priest of the Flame line — six stages, playable in one
evening, ending in a crafting you composed yourself out of canon's own effect
vocabulary.

And, as of S5, the roofs and the trade that runs on them. **Six criminal acts** —
a purse lifted, a guest's strongbox cracked, a bale of contraband carried out
past a watchman, stolen property sold to a fence, somebody leaned on for coin,
and being on a roof at all — every one of which moves the same four numbers
through the same call: a questline's tally, the **heat** the Watch keeps, the
roofs' regard for you, and the garrison's, by the mirror the ladders declared in
S4. Heat is what the Watch HEARD rather than what you did, it cools five minutes
a point including through a night asleep, and past sixty there is paper out on
you and the HUD says so in red. Four dead unlock tokens got readers, the Watch
got a recruiter a player can stand in front of, and **the Skyrunners got a
nine-stage line** that walks you through every one of the six acts once.

And, as of **S9, being unseen and getting in.** Light crossed the line: the
simulation now keeps its own *integer* field over the same authored lamps, on
the same radius, peak and falloff curve the renderer draws with, and every
question about who saw you goes through one rule that weighs six things against
each other —

| moves the read | moves the cover |
|---|---|
| how close they are | crouching (`C`), which halves your speed |
| how much light is falling on you | your SKYRUNNING, capped |
| whether you are inside their forward arc | how loud the *room* is |
| whether they are on duty | |
| the noise you are making | |

— with masonry taking a great deal off the read and not all of it, because
blind is not deaf. `witnessCount`, `spreadWitness` and the Watch's own
`canSeePlayer` are all the same rule now, so a crime cannot be witnessed by
somebody the heat never counted.

**The four strongboxes above the stair are locked.** CRACKSMANSHIP used to be
read once, after a box had opened itself, to scale what fell out; it is now what
opens it. A lock's pins are a pure function of the world seed and the lock's own
id — no draw consumed, never re-rolled — so walking away from a half-picked box
and coming back is the same box. Skill buys the *feel* (whether the wire tells
you which way you were wrong) and the *tolerance* (how far off a probe may be),
and never buys success. The wire snaps under strain, picks are finite and bought
off Finch, a lock with no wire left is **jammed for good**, and force is always
there, always works, is the loudest thing in the building and costs half the
coin.

**And a hand goes in from behind.** `T` lifts from whoever is at your elbow with
no conversation open: CRACKSMANSHIP against their STREETWISE, exactly as the
dialogue topic has resolved it since S3, moved by a bounded amount in whichever
direction the light, the crowd and your own posture point. Both skills are
charged whether the hand came out full or not — Morrowind's rule, and this
project's standing steer.

**Casting is real now** (S13, extended by the held-effects build): `C` spends
the equipped crafting through a linkcraft check that never buys certainty, the
vitality rows land as doses and trickles with a punch's own consequences when
they harm, and a WHILE_ACTIVE **self tuning holds** — a live, hashed row on the
player whose ±1 flows through the same runtime attribute readers the fatigue
build crossed (the gait, the climb costs, the pool ceiling, the punch, the next
cast and its recovery), refreshed whole by a recast and lapsing on the room's
own clock, HUD countdown and all. Still refused, out loud: **temperature**
(nothing in the live sim reads heat on a body — the honest boundary of this
pass), a tuning laid on **another body** (no actor carries an attribute sheet),
and a **forged** tuning (the bench cannot yet name which string it tunes).

What is NOT here yet: items, **the dedicated combat screen** — still the
largest hole, and it is what blocks the second and third wins of S8's own
nemesis arc — save/load to disk, and any of the ward outside the Gull's walls.
Those are the sprints after this one.

`content/` is read-only canon and is reused verbatim — never retype, regenerate
or "improve" anything under it. New files may be added; the baked lamp sidecars
are the only ones this build has added, and `granadad-bake-lamps` re-derives
them from the authored Tiled sources on demand.
