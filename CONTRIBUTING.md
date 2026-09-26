# Contributing to Market Classifier

Market Classifier is an Apache-2.0 open-source project. The repository is in its architecture and feasibility phase; implementation must follow the active milestone and accepted ADRs.

## Before contributing

Read:

1. `SPECIFICATION.md`
2. `CLAUDE.md` (its engineering constraints apply to human and AI contributions)
3. the active file under `docs/milestones/`
4. relevant ADRs and tests

Discuss changes that add dependencies, alter protocols/persistence, expand scope, or modify core architecture before implementation.

## Change expectations

- Keep changes focused and reviewable.
- Add tests for behavior changes.
- Validate untrusted external data at boundaries.
- Keep memory, queues, retention, and retries bounded.
- Preserve explicit unsupported/stale/gap states.
- Use official primary documentation for exchange behavior.
- Update documentation and fixtures with protocol or calculation changes.
- Do not add telemetry, authenticated exchange access, or trading features in V1.

## Commits

Use small imperative Conventional Commit-style messages, for example:

```text
build(wasm): pin Emscripten toolchain
test(runtime): cover ingress queue overflow
feat(ui): add subscription diagnostics panel
```

Avoid unrelated formatting changes and do not rewrite shared history.

## Pull-request checklist

- [ ] Scope matches the active milestone.
- [ ] Tests were added or updated.
- [ ] Relevant checks pass locally.
- [ ] New resources are bounded.
- [ ] Failure and stale-data behavior is explicit.
- [ ] Documentation and ADRs are current.
- [ ] No unrelated files changed.
- [ ] Dependency/license implications were reviewed.
- [ ] Performance impact was measured or explained.

Exact setup commands will be added by M0 once the toolchain is selected and pinned.

