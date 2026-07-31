# Granadad: The Darkstreets — native C++ build

The C++ rewrite of the simulation. The Java build stays in place, untouched, as
the behavioural reference until this reaches parity.

There are two ways to build. Use docker when you want a binary you can trust and
hand to someone; use the CMake presets when you are writing code and want a
fast loop.

---

## 1. Docker — the reproducible build

From the repo root:

```
docker compose run --rm --build build
```

That is the whole thing. It compiles the C++ inside a pinned container, runs the
test suite, and puts a native Windows binary in `dist/`. Then, on Windows,
natively:

```
.\dist\granadad.exe --selftest     # no window, just proves the binary works
.\dist\granadad.exe                # the game
```

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
| `granadad-tests` — fixed-point, wrapping, floor div/mod | 5 | 425 |
| `granadad-content-tests` — TROJSAV reader vs. the real baked worlds | 57 | 902,044 |
| **ctest total** | **58** | |

The build prints both lines every run, so "tests passed" never has to be taken
on faith. It also asserts a **floor** on the ctest count and checks by name that
the cases which load `docks_surface.trojsav` are registered — the suite used to
be one test (fixed.hpp) while `ctest` printed `100% tests passed, 1 tests out of
1`, and it must not be able to shrink back there quietly.

### Output

| File | What it is |
|---|---|
| `dist/granadad.exe` | The game. Self-contained — no DLLs to ship beside it. |
| `dist/granadad-tests.exe` | The doctest suite, as a Windows binary. |
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
GRANADAD_REVISION=$(git rev-parse --short HEAD) docker compose run --rm --build build
BUILD_TYPE=Debug docker compose run --rm --build build
```

`GRANADAD_REVISION` gets baked into the binary and printed on startup, so a bug
report names the commit it came from.

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
  include/granadad/sim/       public headers
  src/sim/                    simulation. no floats, no SDL.
  src/client/                 SDL3 observer. floats legal here and only here.
  tests/
```

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

Skeleton. What exists is the build surface and the deterministic primitives it
compiles: fixed-point math, wrapping arithmetic, build stamping, and a client
that opens an SDL window and clears it. The simulation itself is not written
yet. `content/` is read-only canon and is reused verbatim — never retype,
regenerate or "improve" anything under it.
