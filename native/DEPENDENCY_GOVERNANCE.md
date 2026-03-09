# Native Dependency Governance

The native addon depends on two classes of governed assets:

1. generated WAProto C++ files under `native/generated/`
2. vendored third-party sources under `native/third_party/`

These assets are intentionally locked by hash so that build drift is detected early and reviewed explicitly.

## Authoritative Sources

- Manifest: `native/native_dependency_manifest.json`
- Generator: `scripts/generate-waproto-cpp.mjs`
- Verifier: `scripts/verify-native-deps.mjs`

The manifest is the authoritative source for file hashes. This document describes the process, not a second copy of every checksum.

## Current Governed Inputs

### Generated WAProto artifacts

- source schema: `WAProto/WAProto.proto`
- generated schema copy: `native/generated/WAProto_cpp.proto`
- generated header: `native/generated/WAProto_cpp.pb.h`
- generated source: `native/generated/WAProto_cpp.pb.cc`
- metadata: `native/generated/WAProto_cpp.meta.json`
- hash stamp: `native/generated/WAProto_cpp.sha256`
- declared generator version: `libprotoc 33.4`

### Vendored third-party code

- dependency: `miniz`
- declared version: `3.1.0`
- governed path: `native/third_party/miniz/*`

## What `yarn verify:native-deps` Enforces

The verifier checks that:

- every governed file exists
- file hashes match the manifest
- WAProto generation metadata matches the declared `protoc` version and source hash
- the vendored `miniz` version extracted from `miniz.h` matches the manifest

This verification is also covered by the native test suite and referenced by CI.

## Update Workflow

### When changing `WAProto`

1. Update `WAProto/WAProto.proto` through reviewed maintenance work.
2. Run:

```bash
node scripts/generate-waproto-cpp.mjs
yarn verify:native-deps
```

3. If the generated artifacts changed intentionally, update `native/native_dependency_manifest.json` to match the new hashes and metadata.
4. Review the generated diff before merge.

### When changing vendored native sources

1. Replace or edit the vendored files deliberately.
2. Confirm provenance and intended version.
3. Update the manifest hashes and declared version.
4. Re-run:

```bash
yarn verify:native-deps
yarn test:native
```

## Review Rules

- Do not hand-edit generated protobuf C++ outputs as a substitute for rerunning the generator.
- Do not change manifest hashes just to "make CI green"; hash updates must correspond to reviewed file changes.
- Do not commit downloaded third-party binaries or build outputs.
- Keep this repository explicit about being a community fork; governed dependency updates must not imply upstream or vendor endorsement.

## CI and Test Coverage

- `.github/workflows/build.yml` runs native dependency verification on Linux CI.
- `src/__tests__/Native/native-dependency-governance.test.ts` exercises both the passing and hash-drift failure paths.

## Minimum Acceptance for a Governance-Sensitive Change

Before merging a change that touches `native/generated/` or `native/third_party/`, verify:

1. the source or provenance of the change is clear
2. the manifest matches the new files
3. the verifier passes
4. related documentation remains accurate
