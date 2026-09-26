# ADR-0004: Versioned Byte-Buffer Bridge Batches

Status: Accepted  
Date: 2026-09-25

## Context

The browser host and C++/WebAssembly engine need a deterministic boundary for normalized
events. Inputs are untrusted, allocation and iteration must be bounded, and the format must
behave identically on little- and big-endian hosts without adding a schema dependency.

M1 needs a measured contract and one cross-language event path, not a final encoding for
every normalized event. The first payload is therefore a normalized trade batch; later kinds
may be added under the compatibility rules below.

## Options considered

### A. Versioned fixed-layout byte buffer

- Uses `Uint8Array`/`DataView` in TypeScript and explicit byte reads in C++.
- Has a small fixed header and record overhead, no parser dependency, and validation can
  precede allocation.
- Requires hand-maintained codecs and golden fixtures.

### B. JSON text

- Easy to inspect, but repeats field names, makes 64-bit integer handling awkward in
  JavaScript, and permits parser allocation before semantic limits are known.

### C. FlatBuffers, Protobuf, or another schema framework

- Provides schema tooling and broader evolution support.
- Adds a dependency, generated-code workflow, and WASM payload cost before measurements
  demonstrate that M1 needs them.

## Decision

Use option A with a 16-byte little-endian envelope:

| Offset | Width | Field |
|---:|---:|---|
| 0 | 4 | ASCII magic `MCB1` |
| 4 | 2 | protocol version (`1`) |
| 6 | 1 | message kind (`1` = trade batch) |
| 7 | 1 | flags (`0`) |
| 8 | 4 | payload byte length |
| 12 | 2 | event count |
| 14 | 2 | reserved (`0`) |

All integers are fixed-width two's-complement values encoded little-endian. Decimals use
ADR-0003's signed 64-bit mantissa followed by an unsigned 8-bit scale. The decoder reads
bytes explicitly and does not depend on native struct layout or host endianness.

V1 limits are 65,536 bytes per complete message, 256 events per batch, 64 bytes per native
symbol, and 128 bytes per source ID. Length, count, and fixed header fields are checked
before reserving the result vector or constructing strings. A record's fixed-position fields
cannot be duplicated; duplicate-field behavior is therefore structurally impossible rather
than last-value-wins.

The JavaScript caller owns its `Uint8Array`. The synchronous bridge call lends a view only
for its duration. C++ decodes into owned domain values before returning and retains no
pointer into JavaScript/WASM linear memory. Encoders return a newly owned buffer.

Unknown versions and kinds fail closed with categorized errors. Nonzero flags/reserved
fields, inconsistent lengths, invalid enums, decimal scales over 18, invalid identifiers,
and invalid metadata also fail closed. V1 decoders accept only version 1; compatible new
message kinds may be added without changing the envelope version, while any field-layout or
semantic change requires a new protocol version. Hosts must negotiate by exact version.

## Consequences

- The boundary is compact, deterministic, bounded, and dependency-free.
- The initial format intentionally covers only normalized trades; other event payloads are
  added when a milestone exercises them.
- Codecs are duplicated across languages, so the shared golden fixture and mutation tests
  are mandatory contract checks.
- Message bytes must be copied into owned C++ values before a callback returns.

## Evidence

- Shared fixture: `fixtures/m1/trade-batch-v1.hex` (91 bytes, one trade). Update it only with
  an explained protocol change and update both codec tests in the same commit.
- Node v24.11.0, 2,000 iterations after 100 warmups, 256 trades: 19,216-byte batch; mean
  encode 0.243 ms and decode 0.124 ms on the M0 reference machine.
- `engine/tests/test_bridge_protocol.cpp` and `bridge/tests/protocol.test.mjs` cover the
  golden bytes, round-trip semantics, limits, and categorized rejection.
- `SPECIFICATION.md` sections 9, 18, 19, and 22.
