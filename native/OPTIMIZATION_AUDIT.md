# Native Optimization Audit

This document is a repository-state audit guide for the native addon. It describes where the current codebase concentrates native work, what measurement tooling exists, and how optimization candidates should be reviewed.

It intentionally does **not** record unverified benchmark deltas or marketing-style performance claims.

## Evidence Sources

The observations in this document are derived from:

- `binding.gyp`
- `native/**`
- `src/**` integration points
- benchmark scripts in `scripts/`
- hotspot scoring in `scripts/audit-ts-native-hotspots.mjs`
- native regression coverage in `src/__tests__/Native/`

## Audit Principles

1. Protect wire compatibility before chasing speed.
2. Measure with repository scripts before and after changes.
3. Prefer lower-allocation and lower-marshaling designs over deeper JS object graphs.
4. Keep Windows-only and WAProto-gated code paths explicit.
5. Record governance updates when generated or vendored assets change as part of an optimization.

## Current Hot Domains

| Domain            | Why it is hot                                                                                           | Current evidence                                                                             | Audit focus                                                        |
| ----------------- | ------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| `codec/wabinary/` | BinaryNode encode/decode sits on the critical messaging path                                            | native codec registration plus strict initialization in `src/WABinary/native-codec.ts`       | buffer copies, dictionary lookups, marshal shape                   |
| `socket/`         | Message node decode, callback routing, participant batching, and USync parsing are repeatedly exercised | direct `requireNativeExport(...)` use across socket modules and dedicated socket codec tests | string churn, per-item allocations, callback fan-out               |
| `proto/`          | Message encode/decode, history sync, and poll/event parsing are central to chat processing              | strict native proto initialization in `src/Utils/generics.ts` plus benchmark coverage        | protobuf reflection cost, compressed decode, batch decode behavior |
| `appstate/`       | Patch and mutation decode combines crypto, protobuf, and sync semantics                                 | native exports are required by chat/app-state utilities                                      | MAC validation cost, decode pipeline shape, platform gates         |
| `media/`          | Encryption, decryption, hashing, and spool operations handle large byte streams                         | dedicated media benchmarks and native media tests                                            | chunk alignment, file I/O overlap, copy avoidance                  |
| `signal/`         | Session injection, sender-key handling, and batch operations are security-sensitive and frequent        | signal benchmark script plus sender-key tests                                                | batch marshalling, key-store retention, guard-clause cost          |
| `storage/`        | Auth-state persistence is always native-backed in this fork                                             | `useNativeAuthState()` and native KV tests                                                   | write amplification, compaction thresholds, read batching          |
| `utils/`          | Many low-level helpers are individually small but called often                                          | direct exports for JID, WAM, zlib, retries, and builders                                     | string normalization, repeated parsing, buffer growth strategy     |

## Measurement Tooling Already in the Repository

### Static prioritization

```bash
node scripts/audit-ts-native-hotspots.mjs
```

Use this when deciding whether a TypeScript path is a good migration candidate. It is heuristic only.

### Native microbenchmarks

```bash
yarn benchmark:native
yarn benchmark:media
yarn benchmark:signal
```

These scripts cover different domains and should be preferred over ad-hoc timing snippets.

### Regression safety nets

```bash
yarn test:native
yarn test
```

Optimization work should be considered incomplete if it changes a hot path but does not preserve these suites.

## Known Risk Areas

### WABinary and socket parsing

- Protocol decoding is correctness-critical.
- Small changes in parse shape can cause downstream semantic regressions.
- Favor micro-optimizations that reduce copies or intermediate arrays without changing output contracts.

### Protobuf and app-state

- Windows-only native WAProto support means platform behavior must stay explicit.
- If a path depends on generated C++ protobuf types, verify what happens when `BAILEYS_HAS_NATIVE_WAPROTO=0`.
- Do not assume faster code is acceptable if it changes error messages, padding behavior, or batch ordering.

### Media

- AES and stream alignment errors can be subtle and security-relevant.
- The optional native file-writer path is env-gated and should be measured separately from the always-on decrypt pipeline path.

### Storage

- `NativeKVStore` is part of the auth-state contract used by examples and tests.
- Changes to compaction, mutation batching, or file format require compatibility review.

## Suggested Optimization Workflow

1. Reproduce the current behavior with the relevant benchmark script.
2. Read the corresponding native tests and TS integration points.
3. Make the smallest change that removes a proven bottleneck.
4. Re-run the benchmark and both test layers.
5. If generated or vendored native files changed, update governance artifacts before merging.

## Current Improvement Backlog

These are repository-informed priorities, not promises:

1. Expand benchmark fixtures so large-batch and malformed-input cases are both measured.
2. Reduce repeated string materialization in socket and JID-heavy paths.
3. Review buffer-growth strategies in helpers that accumulate or split stream data.
4. Add domain-specific benchmark baselines to PR review when touching codec-heavy code.
5. Keep platform-gated behavior visible in tests when Windows-only capabilities are involved.
