# Architecture Decision Records

ADRs record decisions that materially affect architecture, protocols, dependencies, security, persistence, performance, or contributor workflow.

## Naming

Use sequential files:

```text
0001-dependency-acquisition.md
0002-cpp-test-framework.md
0003-decimal-representation.md
0004-bridge-batch-encoding.md
0005-json-parser.md
```

## Status values

- Proposed
- Accepted
- Superseded by ADR-NNNN
- Rejected

## Template

```markdown
# ADR-NNNN: Short decision title

Status: Proposed  
Date: YYYY-MM-DD

## Context

What decision is required, and which constraints matter?

## Options considered

List serious options and their tradeoffs.

## Decision

State the selected option precisely.

## Consequences

Describe positive, negative, operational, migration, and testing effects.

## Evidence

Link measurements, experiments, official documentation, and relevant issues.
```

Do not use an ADR to bypass product approval or silently expand scope.
