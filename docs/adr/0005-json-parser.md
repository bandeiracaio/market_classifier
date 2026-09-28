# ADR-0005: C++ JSON parser for venue adapters

Status: Accepted (owner, 2026-09-27)  
Date: 2026-09-27

## Context

The MVP (packet `docs/milestones/MVP_BTC_TERMINAL.md` §10) moves venue payload
parsing into C++ adapters so the same fixtures drive native and WASM replay
tests. The engine has no JSON parser. Requirements: permissive license,
exception-free API (engine is built without relying on exceptions for control
flow), bounded input size and nesting depth enforceable by us, small WASM size,
active maintenance, FetchContent-pinnable (ADR-0001). Numbers must be readable
as raw text so prices go through `domain::Decimal::parse` (ADR-0003), never
through `double`.

## Options considered

WASM size measured 2026-09-27 with Emscripten 3.1.67, `-O3 -fno-exceptions`,
a hello-parse program (parse object, read one string field). Baseline program
without a parser: 6,115 bytes.

| Parser | Version | License | `.wasm` bytes | Delta | Notes |
|---|---|---|---|---|---|
| yyjson | 0.10.0 | MIT | 98,621 | +92.5 KB | C99, 2 files, error codes, no exceptions; raw-number read flag (`YYJSON_READ_NUMBER_AS_RAW`) keeps exact text; no built-in depth limit (wrapper enforces during traversal / pre-scan) |
| simdjson | 3.10.1 | Apache-2.0 | 64,075 | +58.0 KB | Exception-free `error_code` API available; built-in max depth; SIMD gives no benefit on WASM without SIMD flag; requires padded input copy; 7.4 MB single-header source slows builds; raw number text needs `raw_json_token` |
| nlohmann/json | 3.11.3 | MIT | 86,474 | +80.4 KB | Header-only; with `JSON_NOEXCEPTION` errors abort unless using `parse(..., allow_exceptions=false)`; numbers parsed to `double`/int by default (lossy for our purpose unless custom SAX) |

## Decision

**yyjson 0.10.0**, pinned via FetchContent to the release tag (immutable) per
ADR-0001, built as a static C library `mc_yyjson` with warnings isolated from the
project's `-Werror` set. Only `engine/src/json/json.cpp` includes `yyjson.h`.
Reading uses `YYJSON_READ_NUMBER_AS_RAW` so every number is surfaced as its
source text and fed to `Decimal::parse`. The wrapper enforces `max_bytes` before
parsing and rejects nesting depth > 64 with an iterative pre-scan.

Why not simdjson: smallest measured delta, but padded-copy requirement, huge
single-header compile cost, and no SIMD benefit on our target. Close second —
acceptable fallback if the owner prefers the size.
Why not nlohmann: number handling is lossy by default and the no-exception mode is
awkward.

## Consequences

- +~92 KB WASM before gzip.
- New third-party C source compiled into the engine; license notice added to
  `docs/toolchain.md`.
- All adapter numeric fields are exact decimal text.

## Evidence

- https://github.com/ibireme/yyjson (README, API docs `doc/API.md`), read 2026-09-27
- https://github.com/simdjson/simdjson, read 2026-09-27
- https://github.com/nlohmann/json, read 2026-09-27
- Size probe sources: built ad hoc in the session scratchpad (not committed).
