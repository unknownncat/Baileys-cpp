# Native Addon Overview

`native/` contains the C++20 N-API addon built as `baileys_native.node` plus the generated and vendored assets required by that addon.

This directory is specific to this fork and is the main technical difference from upstream `WhiskeySockets/Baileys`.

## Native Loading Model

- The TypeScript bridge lives in `src/Native/baileys-native.ts`.
- The bridge loads `../../build/Release/baileys_native.node` or `../../build/Debug/baileys_native.node`.
- The bridge validates required exports before the runtime proceeds.
- Because of that validation, the repository should be treated as **strict native mode** rather than a soft optional-addon integration.

## Build Topology

- `binding.gyp` defines the addon target `baileys_native`.
- `native/module.cpp` is the addon entry point.
- `native/module/register_*_exports.cpp` registers domain-specific exports.
- The addon uses `node-addon-api` with exceptions and RTTI disabled.

Platform gating:

- Windows builds define `BAILEYS_HAS_NATIVE_WAPROTO=1`, compile `native/generated/WAProto_cpp.pb.cc`, and resolve protobuf libraries through `scripts/native-protobuf-gyp.cjs`.
- Non-Windows builds define `BAILEYS_HAS_NATIVE_WAPROTO=0`.
- `native/common/feature_gates.h` is the canonical source for those compile-time feature gates.

## Directory Map

| Path | Role |
| --- | --- |
| `module/` | Export registration and addon bootstrap |
| `common/` | Shared N-API validation, error handling, feature gates, and safe-copy helpers |
| `signal/` | Sender-key codecs, message-key store, and E2E session bundle extraction |
| `socket/` | USync parsing, participant helpers, callback dispatch, message key utilities, and waiter registry |
| `proto/` | Proto message codecs, history sync decode, poll/event decode, and protobuf reflection |
| `appstate/` | Reporting token helpers, MAC payload builders, sync action and patch decoding |
| `storage/` | Native auth-state codec and single-file KV store |
| `media/` | WAV analysis, encrypt/decrypt primitives, spool writers, and streaming decrypt pipeline |
| `utils/` | JID codecs, WAM encoder, zlib helpers, aligned chunking, frame buffering, and other small fast paths |
| `codec/wabinary/` | Native BinaryNode encode/decode implementation |
| `generated/` | Tracked C++ protobuf artifacts derived from `WAProto/WAProto.proto` |
| `third_party/` | Vendored `miniz` sources tracked by the native dependency manifest |

## Native Build Commands

```bash
yarn build:native
yarn build:native:debug
yarn test:native
```

`yarn build:native` currently does three things in sequence:

1. regenerates tracked WAProto C++ artifacts
2. verifies the native dependency manifest
3. runs `node-gyp rebuild -j max`

If you need the debug addon, use `yarn build:native:debug`, which places the binary under `build/Debug/`.

## Native Environment Flags

| Variable | Effect |
| --- | --- |
| `BAILEYS_VCPKG_ROOT` | Root directory used to locate protobuf headers and libraries on Windows |
| `BAILEYS_VCPKG_TRIPLET` | vcpkg triplet used by `scripts/native-protobuf-gyp.cjs` |
| `BAILEYS_PROTOC_PATH` | Explicit `protoc` binary used by `scripts/generate-waproto-cpp.mjs` |
| `BAILEYS_SKIP_NATIVE_WAPROTO=1` | Skips WAProto C++ generation in the generator script |
| `BAILEYS_NATIVE_MEDIA_WRITERS=1` | Enables the optional native file-writer path consumed by `messages-media.ts` |
| `BAILEYS_NATIVE_ERROR_LOG=1` | Enables native error logging paths covered by the native test suite |

## Tests and Benchmarks

Native-facing verification lives in `src/__tests__/Native/` and currently covers:

- addon API parity
- dependency-governance enforcement
- native KV store behavior
- media pipeline hardening
- socket codec parity
- example auth-state smoke execution

Benchmarks are split by domain:

- `yarn benchmark:native`
- `yarn benchmark:media`
- `yarn benchmark:signal`

## Maintenance Rules

1. Keep `src/Native/baileys-native.ts` aligned with the actual addon exports.
2. Do not document a TypeScript fallback that no longer exists.
3. When editing `generated/` or `third_party/`, update `native/native_dependency_manifest.json` and re-run `yarn verify:native-deps`.
4. When touching platform-gated code, check `native/common/feature_gates.h` and the Windows native workflow.
5. Keep new native behavior covered by tests before relying on it in higher-level documentation.

## Related Native Docs

- Detailed export and file reference: [`REFERENCE.md`](REFERENCE.md)
- Governance for generated and vendored assets: [`DEPENDENCY_GOVERNANCE.md`](DEPENDENCY_GOVERNANCE.md)
- Optimization guidance and measurement workflow: [`OPTIMIZATION_AUDIT.md`](OPTIMIZATION_AUDIT.md)
