# TypeScript Runtime Layer

`src/` contains the public TypeScript entry points and the high-level runtime behavior of the fork: socket orchestration, event semantics, auth helpers, message handling, protocol utilities, and the native binding contract consumed by the rest of the codebase.

## Scope

The public package entry point is `src/index.ts`, which re-exports:

- `WAProto`
- `Utils`
- `Types`
- `Defaults`
- `WABinary`
- `WAM`
- `WAUSync`
- `makeWASocket`

## Directory Map

| Path         | Role                                                                                    |
| ------------ | --------------------------------------------------------------------------------------- |
| `Defaults/`  | Version constants, connection defaults, media mappings, and shared runtime constants    |
| `Native/`    | TypeScript definitions and strict loading rules for `baileys_native.node`               |
| `Signal/`    | Signal repository integration, sender-key handling, and LID/PN mapping helpers          |
| `Socket/`    | Socket composition layers, chat/group/community/newsletter flows, send/receive handling |
| `Types/`     | Public and internal TypeScript contracts                                                |
| `Utils/`     | Auth, crypto, history, media, retry, validation, and general helper modules             |
| `WABinary/`  | BinaryNode codec, JID helpers, and constants                                            |
| `WAM/`       | WAM record encoding utilities                                                           |
| `WAUSync/`   | USync protocol query builders and protocol-specific parsing                             |
| `__tests__/` | Jest coverage for binary, e2e, native, signal, socket, and utility behavior             |

## Native Integration Rules

This repository no longer documents the TypeScript layer as "native optional" in the general case.

- `src/Native/baileys-native.ts` resolves the addon eagerly and throws if required exports are missing.
- Most hot-path modules call `requireNativeExport(...)` at module scope, which means import-time addon availability is part of the runtime contract.
- The WABinary codec is also initialized in strict mode and throws if the codec cannot be initialized.
- A smaller number of helpers use `getOptionalNativeExport(...)` for opt-in behavior. The clearest example is the media file-writer path gated by `BAILEYS_NATIVE_MEDIA_WRITERS=1`.

## Auth State Behavior

Auth-state handling in this fork is native-backed:

- `useNativeAuthState(folder)` stores credentials and signal state in `auth-state.kv`.
- The native store is implemented by `NativeKVStore`.
- `useMultiFileAuthState(folder)` is kept only as a compatibility alias and delegates to `useNativeAuthState(folder)`.

If documentation, examples, or integration notes mention multi-file JSON persistence, they are out of date for this fork.

## Important Runtime Contracts

- `fetchLatestBaileysVersion()` reads the default version from upstream `WhiskeySockets/Baileys`.
- `fetchLatestWaWebVersion()` reads the current client revision from `https://web.whatsapp.com/sw.js`.
- `encodeWAMessage()` / `decodeWAMessage()` rely on the native proto codec after `initProtoMessageCodec(...)` succeeds.
- Media encryption/decryption flows use native primitives and native stream helpers from `messages-media.ts`.
- Socket flows compose upward from `Socket/index.ts`, which applies `DEFAULT_CONNECTION_CONFIG` and delegates to `makeCommunitiesSocket(...)`.

## Test Layout

`src/__tests__/` is divided by behavior rather than by package layer:

- `Native/` checks addon API parity, error contracts, storage, media, and governance behavior.
- `Signal/`, `Socket/`, and `Utils/` cover runtime logic in TypeScript.
- `binary/` and `e2e/` cover protocol and end-to-end behavior.

## Change Guidelines

1. Preserve the export surface of `src/index.ts` unless the change is intentional and documented.
2. Treat `src/Native/baileys-native.ts` as the authoritative bridge contract for native capabilities.
3. Do not document a fallback path unless the code still implements that fallback.
4. When updating protocol or version constants, keep `src/Defaults/index.ts`, `src/Defaults/baileys-version.json`, and `src/Utils/generics.ts` aligned.
5. When changing auth-state behavior, check both `useNativeAuthState.ts` and the compatibility alias in `use-multi-file-auth-state.ts`.
