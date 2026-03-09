# Baileys Native Performance Fork

Community-maintained fork of [`WhiskeySockets/Baileys`](https://github.com/WhiskeySockets/Baileys) that keeps the TypeScript-facing API surface in `src/` while moving hot paths into a native N-API addon implemented in `native/`.

## Project Positioning

- This repository is a fork intended for community experimentation, maintenance, and contribution.
- It is not an official upstream repository and should not be described as an official distribution of WhiskeySockets/Baileys.
- It is not affiliated with, endorsed by, or sponsored by WhatsApp, Meta, or any other third party mentioned in the codebase.
- The project is licensed under MIT; attribution and existing license files must be preserved.

## Current Runtime Model

This fork currently runs in **strict native mode**.

- `src/Native/baileys-native.ts` loads `build/Release/baileys_native.node` or `build/Debug/baileys_native.node` at import time.
- The loader rejects partial bindings and validates a wide export surface before the runtime starts.
- In practice, a source checkout must have the addon built before most of the library can be imported successfully.
- `useMultiFileAuthState()` is preserved as a compatibility alias, but it delegates to the native single-file KV backend implemented by `useNativeAuthState()`.

Some features still use optional native helpers internally, such as the experimental media file writers gated by `BAILEYS_NATIVE_MEDIA_WRITERS=1`, but the repository as a whole should be understood as native-first rather than TS-fallback-first.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `src/` | Public TypeScript entry points, socket orchestration, helpers, types, and tests |
| `src/Native/` | TypeScript contract for the native binding and export validation |
| `native/` | C++20 N-API addon sources, generated code, vendored dependencies, and native docs |
| `WAProto/` | Protobuf schema plus generated TypeScript runtime artifacts |
| `scripts/` | Build, benchmark, audit, generation, and maintenance tooling |
| `proto-extract/` | Standalone extractor used to rebuild `WAProto/WAProto.proto` from current WhatsApp Web assets |
| `Example/` | Interactive example client and native auth-state smoke path |
| `.github/workflows/` | CI, release, native validation, and automated update workflows |

## Requirements

- Node.js `>= 20`
- Corepack-enabled Yarn `4.x` for the repository workflow
- Python and a working C/C++ toolchain for `node-gyp`
- Windows native WAProto builds additionally expect a vcpkg-installed protobuf toolchain

Native CI uses these environment defaults on Windows:

```powershell
$env:BAILEYS_VCPKG_ROOT='C:\vcpkg'
$env:BAILEYS_VCPKG_TRIPLET='x64-windows-static'
```

## Setup

```bash
corepack enable
yarn install
yarn build
yarn build:native
```

What those commands do:

- `yarn build` compiles TypeScript into `lib/`.
- `yarn build:native` regenerates `native/generated/WAProto_cpp.*`, verifies the native dependency manifest, and runs `node-gyp rebuild`.
- `yarn build:docs` generates Markdown API docs into `docs/api/` via TypeDoc.

## Common Commands

```bash
yarn lint
yarn test
yarn test:native
yarn benchmark:native
yarn benchmark:media
yarn benchmark:signal
yarn verify:native-deps
yarn gen:protobuf
yarn update:version
yarn example
```

Command notes:

- `yarn test` runs the TypeScript/Jest suite under `src/**/*.test.ts`.
- `yarn test:native` targets the native regression suite in `src/__tests__/Native`.
- `yarn benchmark:native`, `yarn benchmark:media`, and `yarn benchmark:signal` exercise different native-heavy paths and are intended for local measurement, not contractual performance claims.
- `yarn gen:protobuf` runs `WAProto/GenerateStatics.sh` to refresh TypeScript protobuf artifacts.

## Example Flow

`Example/example.ts` demonstrates a real socket startup path and uses the native auth-state backend by default.

- Default auth directory: `baileys_auth_native`
- Override with: `AUTH_FOLDER=/path/to/state`
- Smoke validation mode: `yarn example --validate-native-auth-state`

That smoke path is covered by `src/__tests__/Native/example-native-auth-state.test.ts`.

## Distribution Note

The current package manifest only publishes:

- `lib/**/*`
- `WAProto/**/*`
- `engine-requirements.js`

That means native development, native validation, and native debugging are documented here in terms of a full source checkout, not a registry tarball alone.

## Automation in This Repository

The repository contains CI and maintenance workflows for:

- build validation on Linux
- lint and Jest execution on Linux
- native build, native tests, and native benchmarks on Windows
- scheduled `WAProto` updates
- scheduled WhatsApp Web version refreshes
- release packaging and manual release flows

See `.github/workflows/` for the authoritative workflow definitions.

## Governance and Legal Boundaries

- Keep fork attribution explicit when copying or adapting upstream-facing documentation.
- Do not claim official support, certification, policy compliance, or security guarantees unless they are directly evidenced in the repository.
- Do not commit third-party binaries or downloaded WhatsApp Web assets; only commit the derived artifacts that are part of the tracked workflow.
- Treat generated code and vendored code as governed assets: update the manifest and documentation when they change.
- Operators are responsible for evaluating whether their usage is appropriate for their own environment and obligations.

## Documentation Index

- TypeScript layer: [`src/README.md`](src/README.md)
- Native addon overview: [`native/README.md`](native/README.md)
- Native export and file reference: [`native/REFERENCE.md`](native/REFERENCE.md)
- Native dependency governance: [`native/DEPENDENCY_GOVERNANCE.md`](native/DEPENDENCY_GOVERNANCE.md)
- Native optimization audit guidance: [`native/OPTIMIZATION_AUDIT.md`](native/OPTIMIZATION_AUDIT.md)
- Engineering scripts: [`scripts/README.md`](scripts/README.md)
- Protobuf extraction tool: [`proto-extract/README.md`](proto-extract/README.md)
