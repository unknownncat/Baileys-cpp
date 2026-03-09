# Engineering Scripts

`scripts/` contains the repository's build-time and maintenance tooling. These files are part of the contributor workflow and are referenced directly by `package.json`, `binding.gyp`, and CI workflows.

## Script Inventory

| Script                          | Purpose                                                                                                                               | Typical caller                |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- |
| `generate-waproto-cpp.mjs`      | Regenerates `native/generated/WAProto_cpp.proto`, `WAProto_cpp.pb.h`, `WAProto_cpp.pb.cc`, metadata, and the source hash stamp        | `yarn build:native`           |
| `verify-native-deps.mjs`        | Verifies hashes and version metadata for generated WAProto artifacts and vendored `miniz` files                                       | `yarn verify:native-deps`, CI |
| `native-protobuf-gyp.cjs`       | Resolves protobuf include and library paths from the configured vcpkg installation                                                    | `binding.gyp` on Windows      |
| `audit-ts-native-hotspots.mjs`  | Scores TypeScript files for likely native-migration payoff based on loops, buffer work, JSON use, protobuf use, and native references | manual audit                  |
| `benchmark-native-hotpaths.mjs` | Benchmarks addon-heavy auth, protobuf, and USync paths against a built native addon                                                   | `yarn benchmark:native`       |
| `benchmark-media-hotpaths.ts`   | Benchmarks media upload, encryption, hashing, and decrypt pipeline flows                                                              | `yarn benchmark:media`        |
| `benchmark-signal-hotpaths.ts`  | Benchmarks Signal session injection, single/batch encrypt, single/batch decrypt, and group decrypt flows                              | `yarn benchmark:signal`       |
| `update-version.ts`             | Fetches the latest WhatsApp Web client revision and updates version constants in tracked source files                                 | `yarn update:version`         |

## Generation and Verification

### `generate-waproto-cpp.mjs`

This script does more than call `protoc`.

- Reads `WAProto/WAProto.proto`
- Patches enum declarations so the first value is proto3-safe (`*_UNSPECIFIED = 0`) when needed
- Rewrites `WIN32 =` enum entries to avoid C++ macro conflicts
- Resolves `protoc` from `BAILEYS_PROTOC_PATH`, the configured vcpkg install, or the system path
- Writes:
  - `native/generated/WAProto_cpp.proto`
  - `native/generated/WAProto_cpp.pb.h`
  - `native/generated/WAProto_cpp.pb.cc`
  - `native/generated/WAProto_cpp.meta.json`
  - `native/generated/WAProto_cpp.sha256`

If the patched proto plus `protoc --version` hash has not changed, the script refreshes metadata and exits without regenerating the C++ sources.

### `verify-native-deps.mjs`

This script is the enforcement point for native dependency governance.

- Reads `native/native_dependency_manifest.json`
- Verifies SHA-256 hashes for:
  - `WAProto/WAProto.proto`
  - `native/generated/WAProto_cpp.proto`
  - `native/generated/WAProto_cpp.pb.h`
  - `native/generated/WAProto_cpp.pb.cc`
  - vendored `native/third_party/miniz/*`
- Confirms `native/generated/WAProto_cpp.meta.json` matches the manifest's declared `protoc` version and source hash
- Extracts the declared `miniz` version from `native/third_party/miniz/miniz.h`

Use it after touching generated or vendored native assets:

```bash
yarn verify:native-deps
```

## Benchmarking

### `benchmark-native-hotpaths.mjs`

Benchmarks a built addon for:

- `encodeAuthValue`
- `decodeAuthValue`
- protobuf batch decode
- padded protobuf batch decode
- `parseUSyncQueryResultFast`

It loads `../build/Release/baileys_native.node` directly and initializes the native proto codec with the repository's `WAProto` bindings.

### `benchmark-media-hotpaths.ts`

Benchmarks media paths using real library helpers from `src/Utils/messages-media.ts`.

Environment controls:

- `BAILEYS_MEDIA_BENCH_ITERATIONS`
- `BAILEYS_MEDIA_BENCH_SIZE_MB`
- `BAILEYS_NATIVE_MEDIA_WRITERS=1` to include the optional native file-writer implementations when present

### `benchmark-signal-hotpaths.ts`

Creates in-memory Signal peers and measures:

- `injectE2ESessions`
- single-recipient encrypt
- batch encrypt
- single-recipient decrypt
- batch decrypt
- group decrypt

Optional profiling:

- `BAILEYS_SIGNAL_WRITE_PROFILE=1`
- CPU profiles are written to `profiles/`

## Auditing

`audit-ts-native-hotspots.mjs` performs a static heuristic scan of `src/` and prints a Markdown report. It is useful for prioritization, not as proof that a migration should happen.

It intentionally ignores:

- `src/Types/**`
- large constant-only files
- `src/__tests__/**`

## Version and Protocol Maintenance

### `update-version.ts`

Updates these files together:

- `src/Defaults/baileys-version.json`
- `src/Defaults/index.ts`
- `src/Utils/generics.ts`

The script uses `fetchLatestWaWebVersion()` and writes GitHub Actions outputs when running in CI.

### `WAProto/GenerateStatics.sh`

Although it lives outside `scripts/`, the root command `yarn gen:protobuf` depends on it. Use it after accepting schema changes in `WAProto/WAProto.proto`.

## Governance Notes

- Review generated diffs before committing them.
- Do not edit hash manifests casually; update them only after a deliberate generation or vendor upgrade.
- Do not treat benchmark output as a contractual performance guarantee.
- Do not commit downloaded upstream web assets; commit only the tracked derived artifacts required by this repository.
