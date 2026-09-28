# MVP performance report

Date: 2026-09-27. Packet §12 criterion 9 (measured and reported, not blocking).

## Setup

| Item | Value |
|---|---|
| Machine | Intel Core i5-10300H @ 2.50 GHz, 7.8 GB RAM, NVIDIA GeForce GTX 1650 + Intel UHD Graphics |
| OS | Windows 10 Home Single Language 10.0.19045 |
| Browser | Chromium 129.0.6668.29 (Playwright 1.47.2, **headed**, hardware GL) |
| Build | `wasm-release` (Emscripten 3.1.67, `-O3`) + `vite build`, served by `vite preview` |
| Commit | `ed2987f` (engine) — measured from a clean worktree of `mvp/btc-terminal` |
| Data | **Live** public streams from Binance USD-M `BTCUSDT` and Hyperliquid `BTC` |
| Tool | `apps/web/tools/measure-runtime.mjs --presets` / `--soak` |

Frame times are `requestAnimationFrame` intervals over 30 s per preset after a 2 s settle,
both venues `Live`. The display refresh is 60 Hz, so 16.67 ms is the floor.

## Frame time per preset

| Preset | fps | mean ms | p95 ms | p99 ms |
|---|---:|---:|---:|---:|
| Overview | 60.0 | 16.67 | 16.9 | 17.0 |
| Tape Reader | 60.0 | 16.67 | 16.9 | 17.0 |
| Footprint | 60.0 | 16.67 | 16.9 | 17.0 |
| Liquidity | 60.0 | 16.66 | 16.9 | 17.0 |
| Derivatives | 60.0 | 16.67 | 16.9 | 17.0 |

Before commit `f6dc65c` the Liquidity preset measured **44.6 fps (p95 33.4 ms)** and the WASM
heap jumped 17 → 70 MB within minutes: the heatmap panel emitted one draw rect per stored
level per visible column. It now rasterizes into ≤ 480 × 256 cells (`processors::rasterize`),
bounding draw work independently of history.

## Size

| Artifact | Raw | gzip |
|---|---:|---:|
| `market_classifier.wasm` | 1,404,582 B | 540,332 B |
| `market_classifier.js` (Emscripten glue) | 223,033 B | 50,699 B |
| SvelteKit app JS (`build/_app/**/*.js`) | — | 27,155 B |
| Whole static site (`build/`) | 1,700,921 B | — |

## One-hour soak (Liquidity preset, live data)

~593 k Binance and ~50 k Hyperliquid normalized events over 60 minutes.

| Minute | WASM heap | JS heap |
|---:|---:|---:|
| 0 | 17.0 MB | 27.9 MB |
| 10 | 20.4 MB | — |
| 25 | 24.6 MB | — |
| 60 | 24.6 MB | 36.9 MB |

- WASM heap grows in two steps while the bounded rings fill (heatmap columns, 24 h series,
  tape) and is **flat from minute 25 to 60** — no unbounded growth.
- JS heap saw-tooths between 20.2 and 39.0 MB (GC); its 10-minute average rose from 24.8 MB
  (first 10 min) to 34.2 MB (last 10 min).

Raw samples: `apps/web/tools/measure-runtime.mjs --soak` output (reproducible with
`MC_PROFILE_HEADED=1 MC_SOAK_MINUTES=60`).

## Follow-ups (non-blocking)

- JS heap trend: confirm with a longer (3–6 h) soak whether the rising average plateaus;
  suspects are per-frame `document.body.dataset` writes and driver frame buffers (both
  bounded but allocation-heavy).
- Measure on a 120/144 Hz display and on integrated graphics only; this run was capped by
  60 Hz vsync.
