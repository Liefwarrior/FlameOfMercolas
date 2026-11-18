# Phase 3 (GPU renderer foundation): before and after

Same scenario, viewport, clock and state in both sets. Only the renderer differs.

| | Revision | Renderer |
|---|---|---|
| `before/` | `main` at `b6d3e5bc` (after phase 2) | raylib's `rlsw` software rasterizer, headless (`-DPLATFORM=Memory`), the old default shader |
| `after/` | this PR | raylib on OpenGL 3.3 core, the scene and post GLSL shaders, offscreen target, headless on Mesa llvmpipe (`-DPLATFORM=SDL`, `llvmpipe (LLVM 21.1.8, 256 bits)`) |

Each capture is the same command line on both builds, 1280x720, `--time` as named:

```
granadad --smoke=0 --hold --time=8                       01-street-morning
granadad --smoke=0 --hold --time=19                      02-street-dusk
granadad --smoke=0 --hold --time=22                      03-street-night
granadad --smoke=0 --hold --threshold=gull --time=20     04-taproom
granadad --smoke=0 --hold --threshold=piers --time=9     05-piers
granadad --smoke=0 --hold --time=8 --pitch=45            06-look-up
granadad --smoke=0 --hold --time=8 --pitch=-45           07-look-down
```

Taken on Linux (WSL Ubuntu) with the stock checkout: no licensed art, so the
building pieces draw nothing and the chunk meshes stand (the placeholder rule),
exactly as in the existing `3d-world-synty` and `3d-slice-one` baselines.

The phase is meant to look the same: the shader is a pass-through by default
(vertex colour x texture x tint, no lighting term, no fog), so the difference is
rasterizer rounding. Measured per pair, after against before:

| capture | mean abs error / channel | pixels more than 24 apart |
|---|---|---|
| 01 street morning | 0.65 | 0.26 % |
| 02 street dusk | 0.64 | 0.33 % |
| 03 street night | 0.55 | 0.03 % |
| 04 taproom | 0.47 | 0.04 % |
| 05 piers | 0.52 | 0.005 % |
| 06 look up (+45) | 0.57 | 0.23 % |
| 07 look down (-45) | 0.48 | 0.04 % |

The pixels that moved are triangle edges. The `scene` hash printed by each
capture differs between the two builds on purpose: the description now carries
vertex normals and the shader's uniforms.
