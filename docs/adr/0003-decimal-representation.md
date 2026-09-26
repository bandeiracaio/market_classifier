# ADR-0003: Fixed-Point Decimal Representation

Status: Accepted  
Date: 2026-09-25

## Context

Market prices, quantities, rates, and notionals arrive as decimal text and must remain exact
through normalization and deterministic replay. Binary floating point cannot represent many
decimal fractions exactly. The representation must compile consistently with MSVC, Clang,
and Emscripten, reject untrusted input before unbounded work, and add no dependency.

The initial normalized model proposed a signed 64-bit mantissa and decimal scale but left
parsing, overflow, comparison, rescaling, and canonical serialization unspecified.

## Options considered

### Option A — Project-owned `int64_t` mantissa and bounded decimal scale

- Small value type with no allocation for arithmetic or comparison.
- Identical representation across native and WASM targets.
- Requires carefully tested checked arithmetic and cross-scale comparison.
- The finite mantissa range is sufficient for venue prices and quantities but must be
  enforced at ingress.

### Option B — Wider project-owned integer representation

- A 128-bit mantissa increases range.
- Standard C++20 has no portable 128-bit integer. MSVC and Emscripten support different
  extensions, which would weaken cross-target determinism and increase implementation cost.

### Option C — Third-party decimal library

- May provide broader arithmetic and rounding facilities.
- Adds dependency, WASM payload, license, maintenance, and cross-toolchain risk before M1
  requires those facilities.

## Decision

Use a project-owned decimal value consisting of:

- signed `int64_t` mantissa;
- unsigned decimal scale in the inclusive range `0..18`;
- numeric meaning `mantissa × 10^-scale`.

The accepted external grammar is `[+-]?[0-9]+(?:\.[0-9]+)?`. Whitespace, exponent notation,
digit separators, a leading decimal point, and a trailing decimal point are rejected. Input
is limited to 64 bytes and at most 18 fractional digits before parsing the mantissa. Both
positive and negative `int64_t` limits are accepted; overflow is rejected.

Parsing removes fractional trailing zeros. All textual serialization is canonical:

- no leading plus sign;
- no unnecessary leading or fractional trailing zeros;
- zero is `0` with no negative sign;
- no exponent notation.

Construction from trusted parts and explicit exact rescaling may retain a non-canonical
internal scale. Equality and ordering are numeric across scales; representation equality is
not domain equality. Serialization canonicalizes the value.

Rescaling is exact only:

- increasing scale multiplies the mantissa with checked overflow;
- decreasing scale requires every removed decimal digit to be zero;
- no implicit rounding occurs.

Operations return categorized errors rather than throwing. M1 implements parsing,
serialization, comparison, and exact rescaling only. Addition, multiplication, division,
and rounding require named checked APIs and tests when a later calculation needs them.

## Consequences

**Positive:**

- Exact, deterministic representation on all supported targets.
- No new dependency or WASM payload cost.
- Malformed, oversized, excessive-scale, overflow, and inexact operations are explicit.
- Cross-scale comparisons do not multiply operands and therefore cannot overflow.

**Negative:**

- Range is intentionally finite and narrower than arbitrary precision.
- Future arithmetic requires additional checked APIs.
- Equivalent values can have different trusted internal representations after explicit
  rescaling, so callers must use numeric equality rather than compare fields.

**Operational:**

- Venue adapters must parse external numeric strings through this API.
- Domain validators separately enforce positivity, tick alignment, and venue-specific
  bounds; the decimal type itself permits negative values.
- Floats may be produced later by presentation-only conversion, never used as normalized
  storage.

## Evidence

- `SPECIFICATION.md`, section 9: fixed-point or proven decimal requirement.
- `engine/tests/test_decimal.cpp`: boundary, malformed, overflow, cross-scale, exact-rescale,
  and deterministic generated-case coverage.
- C++20 fixed-width integers and `std::string_view` provide portable storage and bounded
  input inspection without a third-party library.
