# M1 — Domain Core and Bridge Contract

Status: In progress; Slices 1 through 6 implemented
Parent specification: `SPECIFICATION.md`, sections 9, 18, 19, and 22  
Prerequisite: M0 accepted locally at commit `b8582a3`; first remote CI run pending  
Scope owner: project owner  

## Objective

Establish the deterministic, bounded, versioned foundation through which future venue
adapters submit normalized market data to the C++ engine and the browser host exchanges
control and diagnostic messages with WebAssembly.

M1 defines contracts and proves them with synthetic fixtures. It does not connect to an
exchange, render production market panels, or choose behavior that belongs to later
milestones.

## Required outcomes

1. A reviewed decimal/fixed-point policy with checked parsing, comparison, rescaling, and
   arithmetic behavior.
2. Strong normalized identifiers, timestamps, event metadata, event kinds, and data-quality
   states.
3. A versioned C++/TypeScript bridge envelope with explicit compatibility and rejection
   rules.
4. Bounded ingress batches and queues with observable accepted, rejected, and dropped
   counters.
5. An injected monotonic/wall-clock interface and deterministic fake clock.
6. A replay harness that feeds timestamped synthetic normalized events without sleeping.
7. A minimal dummy read model or panel seam proving deterministic ingress-to-render flow.

## Non-goals

- No Binance or Hyperliquid HTTP/WebSocket code.
- No parsing of raw venue JSON.
- No order-book synchronization, reconnect state machine, or subscription manager.
- No production market explorer, chart, tape, or book panel.
- No persistence or IndexedDB schema.
- No worker, pthread, `SharedArrayBuffer`, or cross-origin-isolation requirement.
- No classifier features, history recorder, alerts, trading, authentication, or telemetry.
- No new third-party dependency without owner approval and an accepted ADR.

## Decisions required before implementation

### ADR-0003 — Decimal representation

Compare at minimum:

- project-owned signed integer mantissa plus bounded decimal scale;
- a proven decimal library that satisfies WASM size, license, overflow, and determinism
  requirements.

The decision must specify:

- accepted textual grammar and maximum input length;
- mantissa and scale bounds;
- canonical zero and trailing-zero policy;
- checked overflow behavior;
- comparison and rescaling rules;
- rounding modes and where rounding is permitted;
- serialization representation;
- invalid-operation reporting without exceptions crossing the bridge.

Binary floating point remains presentation-only.

### ADR-0004 — Bridge batch encoding

Measure at least a simple versioned typed-array/byte-buffer envelope before considering a
schema framework. The decision must document:

- ownership and lifetime of memory passed across JS/WASM;
- protocol version and message-kind representation;
- length and count limits before allocation;
- endianness and integer-width assumptions;
- unknown-version and unknown-kind behavior;
- malformed, truncated, oversized, and duplicate-field behavior;
- measured payload and decode cost for synthetic batches;
- upgrade and compatibility policy.

Choosing a format that adds a dependency requires separate owner approval.

## Proposed module boundaries

```text
engine/include/market_classifier/
  domain/       decimal, identifiers, timestamps, quality, normalized events
  runtime/      clocks, bounded ingress, counters, replay contracts
  bridge/       C++ protocol envelope and validation
engine/src/
  domain/       non-trivial domain implementations
  runtime/      queue and replay implementations
  bridge/       decoding/encoding implementation
engine/tests/
  domain/       normal, boundary, malformed, and arithmetic tests
  runtime/      capacity, ordering, clock, and replay tests
  bridge/       golden cross-language fixtures and rejection tests
bridge/src/     matching TypeScript protocol types and codec
bridge/tests/   TypeScript fixture and compatibility tests
fixtures/m1/   small synthetic semantic fixtures; never copied venue payloads
docs/adr/       ADR-0003 and ADR-0004
```

Domain types must not include ImGui, SDL, Emscripten, JSON, or venue-specific headers.
Rendering consumes a read-model seam and never the ingress queue directly.

## Invariants

- Every externally supplied byte, length, count, enum, timestamp, decimal, and identifier is
  validated before entering domain state.
- Every queue and batch has a compile-time or configuration maximum with a tested overflow
  policy.
- Queue overflow is observable and never silently represented as complete/live data.
- Source time, receive time, and local sequence remain distinct.
- Instrument identity always includes venue plus native symbol.
- Missing, stale, partial, gap-affected, unsupported, and failed states remain distinct.
- Tests use injected clocks and deterministic event ordering.
- Protocol decoding cannot allocate based solely on an untrusted declared length.
- Unknown protocol versions and message kinds fail closed with a categorized diagnostic.

## Test-first implementation slices

### Slice 1 — Decimal ADR and core

1. Write ADR-0003 and owner-review it before implementation.
2. Add failing tests for canonical parsing, signed values, scale limits, leading/trailing
   zeros, malformed text, maximum length, overflow, comparison across scales, exact
   rescaling, and prohibited lossy operations.
3. Implement the minimum decimal type needed to pass those tests.
4. Add property tests using deterministic generated cases without adding a dependency.

### Slice 2 — Identity, metadata, and quality

1. Add tests for venue/instrument identity and stable equality/order semantics.
2. Add explicit source, receive, and local-sequence types or wrappers.
3. Define all specification data-quality states and legal status transitions needed by M1.
4. Define normalized event structures without venue-specific fields leaking into common
   semantics.

Events not exercised in M1 may be represented by contract-complete structures plus focused
construction/validation tests; no speculative processor behavior is required.

### Slice 3 — Clock and bounded ingress

1. Add production and fake clock interfaces.
2. Define batch byte/event limits and bounded queue capacity.
3. Test exact capacity, overflow, recovery, stable ordering, and diagnostic counters.
4. Ensure submission and render-owned consumption are separate operations even in the
   single-threaded build.

### Slice 4 — Bridge ADR and cross-language contract

1. Benchmark the minimal candidate and write ADR-0004.
2. Define the same envelope fields and limits in C++ and TypeScript.
3. Generate or hand-maintain small golden byte fixtures with a documented update rule.
4. Test valid round-trips where appropriate and rejection of truncated, oversized,
   unsupported-version, unknown-kind, invalid-enum, and invalid-decimal inputs.

### Slice 5 — Deterministic replay seam

1. Add a replay harness driven by the fake clock.
2. Replay synthetic normalized events into the bounded ingress path.
3. Build a minimal read model containing counts, last sequence/time, and quality state.
4. Expose that read model to a dummy diagnostic panel or equivalent observable seam.
5. Prove identical fixtures produce identical final state and diagnostics.

### Slice 6 — WASM/browser proof and documentation

1. Exercise at least one valid and one invalid bridge batch in a browser test.
2. Verify malformed input leaves the render loop alive and exposes a categorized diagnostic.
3. Measure bridge payload/decode cost and update the performance notes.
4. Run native, WASM, TypeScript, lint, build, and browser acceptance checks.

## Acceptance checklist

- [x] ADR-0003 is accepted before decimal implementation lands.
- [x] Decimal tests cover normal, boundary, malformed, overflow, and cross-scale cases.
- [x] No domain price or quantity uses binary floating point.
- [x] Normalized identifiers and metadata preserve venue, native symbol, three time/sequence
      concepts, and quality state.
- [x] All required normalized event contracts are defined without venue JSON coupling.
- [x] ADR-0004 is accepted before the final bridge encoding lands.
- [x] C++ and TypeScript agree on protocol version, message kinds, limits, and fixture bytes.
- [x] Malformed, truncated, oversized, and unsupported bridge messages reject safely.
- [x] Ingress batch and queue memory are bounded and exact-capacity behavior is tested.
- [x] Accepted, rejected, and dropped counters are observable.
- [x] Queue overflow cannot leave downstream state labeled `Live` and complete.
- [x] Fake-clock replay performs no real sleeping and is deterministic.
- [x] A synthetic reference replay feeds the dummy read model/panel seam deterministically.
- [x] Native build/tests pass with warnings as errors.
- [x] WASM build and browser bridge tests pass.
- [x] TypeScript check, formatting, lint, and production build pass.
- [x] Performance notes record bridge batch size and decode methodology.
- [x] No exchange, telemetry, authenticated, or deferred-roadmap network request exists.
- [x] Review finds no unresolved S0/S1 issue.

## Initial resource limits to decide and test

The implementation proposal must choose explicit M1 values for:

- maximum bridge message bytes;
- maximum events per ingress batch;
- maximum queued batches and/or bytes;
- maximum native symbol and identifier lengths;
- maximum decimal text length and scale magnitude;
- maximum retained diagnostics samples.

Values must be conservative, configurable where future venue measurements may change them,
and enforced before allocation or iteration. Changing their semantics later requires review;
tuning measured numeric values does not necessarily require a protocol change.

## Stop conditions

Stop and request owner/architecture review if:

- decimal semantics require lossy conversion for a required event;
- the selected bridge representation needs a new dependency;
- C++ and TypeScript cannot share deterministic golden fixtures;
- a proposed API exposes Emscripten internals outside the bridge module;
- bounded ingress cannot report data loss without weakening quality semantics;
- the dummy proof requires real exchange data or a venue adapter;
- implementation pressure expands M1 into subscription, persistence, or production-panel
  scope;
- a protocol choice materially increases the M0 payload or frame-time baseline without an
  explained measurement and review.

## Required handoff

Use the handoff template in `CLAUDE.md` and additionally include:

- accepted ADR summaries;
- all chosen resource limits and their rationale;
- cross-language golden fixture inventory;
- malformed/oversized rejection coverage;
- replay determinism evidence;
- bridge payload/decode measurements;
- exact native, WASM, web, and browser commands and results;
- any contract question intentionally deferred to M2 or later.
