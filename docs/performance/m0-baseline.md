# M0 Performance Baseline

Status: **RECORDED FOR M0** — payload, automated browser checks, and repeatable headless and headed runtime samples are verified.

Date: 2026-09-25  
Commit: initial working tree (pre-commit)  
Milestone: M0

## Reference environment

- OS: Windows 10.0.19045 x64
- Compiler: MSVC 19.50.35717
- Browser: Playwright-managed Chromium 129.0.6668.29 (Playwright 1.47.2)
- Local Node.js: 24.11.0; CI/project pin remains 22.9.0
- pnpm: 9.11.0
- Build: Emscripten 3.1.67, CMake Release, WebGL 2, single-threaded
- CPU: Intel Core i5-10300H, 4 cores / 8 logical processors
- GPU: NVIDIA GeForce GTX 1650 plus Intel UHD Graphics; the headed WebGL run used Intel UHD Graphics through ANGLE/D3D11
- RAM: 8,412,778,496 B reported by Node.js (`os.totalmem()`)

## Recorded payload

Measured from a production web build. Gzip values use level 9 and are calculated independently per served file.

| Artifact | Uncompressed | Gzip |
|---|---:|---:|
| `market_classifier.js` | 217,604 B | 49,504 B |
| `market_classifier.wasm` | 1,028,753 B | 393,093 B |
| Complete static site (20 files) | 1,307,925 B | 469,509 B |

The terminal JS + WASM payload is 442,597 B gzip, comfortably below the 12 MB warning threshold.

## Runtime evidence

- Eight Chromium smoke tests pass as a suite.
- Tests verify host boot, WASM ready state, a progressing render heartbeat, visible canvas, hidden loading overlay, no page errors, and an actionable missing-WASM failure state.
- The smoke tests use a 30-second readiness ceiling; they are correctness evidence, not a startup benchmark.

### Automated runtime sample

`pnpm measure:runtime` was run against the local Vite development server using
Playwright-managed headless Chromium 129.0.6668.29 with SwiftShader/Vulkan. Five fresh
browser contexts form the cold sample; a primed context with five subsequent pages forms
the warm sample. Values are browser `performance.now()` milliseconds from navigation time
origin to the first completed WASM frame.

| Metric | Min | Median | Max |
|---|---:|---:|---:|
| Cold first completed frame | 184.5 ms | 187.0 ms | 207.8 ms |
| Warm first completed frame | 156.7 ms | 157.9 ms | 160.5 ms |
| Mean render-step time over 120 frames | 1.682 ms | 2.151 ms | 2.214 ms |
| Render-step p95 over 120 frames | 1.8 ms | 2.4 ms | 2.5 ms |
| WASM heap after sampling | 17,039,360 B | 17,039,360 B | 17,039,360 B |

These figures are reproducible automation evidence, not interactive GPU measurements. They
include the current headless software-rendering configuration and should not be compared
directly with native-GPU DevTools traces.

### Headed hardware-WebGL runtime sample

The same command was run with `MC_PROFILE_HEADED=1`, without the headless
SwiftShader/Vulkan flags. WebGL reported `ANGLE (Intel, Intel(R) UHD Graphics
(0x00009BC4) Direct3D11 vs_5_0 ps_5_0, D3D11)`.

| Metric | Min | Median | Max |
|---|---:|---:|---:|
| Cold first completed frame | 212.7 ms | 215.3 ms | 453.1 ms |
| Warm first completed frame | 185.9 ms | 188.7 ms | 210.6 ms |
| Mean render-step time over 120 frames | 2.983 ms | 3.008 ms | 3.244 ms |
| Render-step p95 over 120 frames | 3.5 ms | 3.6 ms | 3.8 ms |
| WASM heap after sampling | 17,039,360 B | 17,039,360 B | 17,039,360 B |

The demo changes its frame counter continuously, so this is an active-render measurement,
not the specification's no-active-motion idle case. The active-render p95 is well within
the 16.7 ms budget. Warm readiness is well within the 2.5 second budget. A single earlier
headed run contained a 1,083.7 ms cold-launch outlier; it also remained within the startup
budget and was not substituted into the table above, which records the renderer-verified run.

## Measurement limitations and follow-up

Browser UI automation was unavailable during verification, so no manual DevTools trace was
captured. The headed run nevertheless uses a visible browser and confirms the hardware WebGL
renderer. A future profiling pass may use the browser Performance and Memory panels to
cross-check these in-app measurements. The true no-active-motion idle case remains deferred
until the render loop exposes an idle state; the M0 demo updates its frame counter every frame.

## Budget checkpoints

| Metric | Limit | Result |
|---|---:|---|
| Compressed terminal payload | 12 MB warning | PASS — 442,597 B |
| Idle frame time | 3 ms average | NOT APPLICABLE — M0 demo has continuous frame-counter motion |
| Active render frame | 16.7 ms p95 | PASS — 3.6 ms headed median p95 |
| Warm app interactive | 2.5 s | PASS — 188.7 ms headed median |

The p95 16.7 ms active-frame budget and other budgets from `SPECIFICATION.md` section 18 apply from M4 onward.
