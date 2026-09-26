# Claude Code Instructions

You are the primary implementation agent for Market Classifier. Work as a careful senior engineer inside the architecture defined by this repository.

## Required reading order

Before changing code, read:

1. `SPECIFICATION.md`
2. this file
3. the current milestone packet under `docs/milestones/`
4. relevant ADRs under `docs/adr/`
5. relevant protocol/calculation documents and existing tests

If these sources conflict, stop and report the conflict. Do not silently pick one.

## Source of truth

- `SPECIFICATION.md` defines product scope and system-level requirements.
- Accepted ADRs define architectural decisions.
- The active milestone packet defines the current bounded assignment.
- Tests describe verified behavior but do not override the specification.
- Official upstream documentation is authoritative for exchange and tool behavior.

Do not implement deferred roadmap features unless the owner explicitly moves them into an active milestone.

## Before coding

Report:

```text
Milestone / slice:
Acceptance criterion being addressed:
Expected files/modules:
Assumptions or specification conflicts:
Tests to add first:
Official documentation consulted:
```

Ask before proceeding if:

- product behavior is genuinely ambiguous;
- observed upstream behavior contradicts the specification;
- a new dependency is needed;
- a protocol or persisted schema must change;
- the task requires work outside the active milestone;
- completing it would weaken a security, correctness, or resource bound.

## Implementation rules

- Work only on the active milestone or explicitly assigned slice.
- Use test-first development where practical.
- Preserve unrelated user changes.
- Do not weaken assertions to make tests pass.
- Keep venue adapters, normalized domain logic, processors, storage, and rendering separate.
- UI panels consume read models, never raw exchange JSON.
- External data is untrusted: validate shape, size, numeric bounds, identity, and sequence semantics.
- Bound every queue, cache, series, retry loop, log, and diagnostic sample.
- Use fixed-point/decimal domain values for price and quantity; floats are presentation-only unless an ADR states otherwise.
- Do not add authenticated exchange endpoints, credentials, trading, analytics, or telemetry.
- Do not add dependencies without approval and an ADR entry.
- Avoid unrelated refactors and mass formatting.
- Comments explain protocol invariants, decisions, and non-obvious math—not syntax.
- Link venue/tool behavior to official primary documentation and record the verification date.
- Format, lint, and test all touched code.
- Never claim completion without running the relevant acceptance checks.

## Architecture constraints

- C++20 domain/terminal engine.
- Dear ImGui docking terminal compiled by pinned Emscripten to WASM.
- WebGL 2/OpenGL ES 3 rendering path.
- Minimal SvelteKit/TypeScript host and browser bridge.
- Browser-facing APIs stay behind a narrow, typed, versioned boundary.
- Calculations are deterministic with injected clocks.
- Network callbacks submit bounded batches; they do not mutate render-owned state.
- V1 persists layouts/settings only, not raw market streams.
- Missing, stale, partial, gap-affected, and unsupported data are distinct states.
- Start single-threaded in WASM. Pthreads require measurements and an accepted ADR.

## Git discipline

- Keep commits small, coherent, and buildable.
- Use imperative Conventional Commit-style messages, for example:
  `feat(runtime): add bounded ingress queue`.
- Do not combine unrelated formatting with behavior changes.
- Do not rewrite shared history, force-push, or discard user changes.
- A milestone is not ready for acceptance with an unexplained dirty tree.

## Handoff format

Every implementation handoff must contain:

```text
Milestone / slice:
Outcome:
Changed files:
Tests added or updated:
Commands run and results:
Performance impact:
Protocol/docs consulted:
Decisions or deviations:
Known risks / follow-ups:
Suggested commit message:
```

If a command could not run, state the exact blocker. Separate environmental failures from product failures.

## Review severities

Codex reviews implementation findings as:

- `S0`: safety, security, or data-corruption blocker
- `S1`: correctness or architecture blocker
- `S2`: material reliability, performance, or maintainability issue
- `S3`: minor improvement

Resolve S0/S1 findings before milestone acceptance. Address S2 or explicitly record its deferral. Keep fixes targeted and rerun relevant checks.

## Definition of done

A task is complete only when:

- it matches the specification and accepted ADRs;
- appropriate normal, boundary, malformed, stale, and reconnect cases are tested;
- native and relevant WASM/browser checks pass;
- formatting, lint, and static analysis pass;
- data provenance and failure states remain explicit;
- memory/queue behavior stays bounded;
- documentation and fixtures are current;
- unrelated files were not modified;
- the handoff is complete;
- no S0/S1 findings remain.

