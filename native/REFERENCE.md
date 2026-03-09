# Native Addon Reference

This document is the technical reference for `native/`. It summarizes the addon topology, the exported native surface consumed by TypeScript, and the directory-level file map used to maintain the C++ engine in this fork.

## Build and Loading Topology

- `binding.gyp` defines the `baileys_native` addon target.
- `native/module.cpp` initializes the addon and delegates export registration to `native/module/register_*_exports.cpp`.
- `src/Native/baileys-native.ts` is the TypeScript-side contract and strict loader.
- The loader accepts only bindings that satisfy the required export set, including the WABinary codec.
- `native/common/feature_gates.h` exposes compile-time flags for Windows builds and native WAProto availability.

## Export Surface by Domain

The list below follows the `NativeBinding` contract in `src/Native/baileys-native.ts`.

### Signal

- `encodeSenderKeyStates(states)`
- `decodeSenderKeyStates(data)`
- `isSenderKeyBinary(data)`
- `extractE2ESessionBundlesFast(users)`
- `NativeMessageKeyStore`
- `NativeMessageKeyStore.has(iteration)`
- `NativeMessageKeyStore.add(iteration, seed, maxKeys?)`
- `NativeMessageKeyStore.remove(iteration)`
- `NativeMessageKeyStore.toArray()`
- `NativeMessageKeyStore.encodeSenderKeyRecord(...)`
- `NativeMessageKeyStore.size()`

### Socket

- `decodeMessageNodeFast(attrs, meId, meLid)`
- `buildSocketCallbackEventKeys(frame, callbackPrefix)`
- `emitSocketCallbackEvents(emitter, frame, callbackPrefix?, tagPrefix?)`
- `parseUSyncQueryResultFast(result, protocolNames)`
- `extractDecryptPayloadsFast(stanza)`
- `extractDeviceJidsFast(result, myJid, myLid, excludeZeroDevices)`
- `stringifyMessageKeyFast(key)`
- `stringifyMessageKeysFast(keys)`
- `stringifyMessageKeysFromMessagesFast(messages)`
- `stringifyMessageKeysFromEntriesFast(entries)`
- `encodeGroupParticipantStubsFast(participants)`
- `parseGroupParticipantStubsFast(items)`
- `buildParticipantNodesBatch(encryptedItems, extraAttrs?)`
- `generateParticipantHashV2Fast(participants)`
- `NativeMessageWaiterRegistry`
- `NativeMessageWaiterRegistry.registerWaiter(msgId, token, deadlineMs)`
- `NativeMessageWaiterRegistry.resolveMessage(msgId)`
- `NativeMessageWaiterRegistry.removeWaiter(token)`
- `NativeMessageWaiterRegistry.evictExpired(nowMs)`
- `NativeMessageWaiterRegistry.rejectAll()`
- `NativeMessageWaiterRegistry.size()`
- `NativeMessageWaiterRegistry.clear()`

### Proto

- `padMessageWithLength(data, padLength)`
- `getUnpaddedLengthMax16(data)`
- `initProtoMessageCodec(encodeFn?, decodeFn?)`
- `encodeProtoMessageRaw(message)`
- `encodeProtoMessageWithPad(message, padLength)`
- `decodeProtoMessageRaw(data)`
- `decodeProtoMessageFromPadded(data)`
- `decodeProtoMessagesRawBatch(items)`
- `decodeProtoMessagesFromPaddedBatch(items)`
- `decodePollVoteMessageFast(...)`
- `decodeEventResponseMessageFast(...)`
- `decodeHistorySyncRaw(data)`
- `decodeCompressedHistorySyncRaw(data)`
- `NativeHistorySyncCompressedDecoder`
- `NativeHistorySyncCompressedDecoder.append(chunk)`
- `NativeHistorySyncCompressedDecoder.decode(reset?)`
- `NativeHistorySyncCompressedDecoder.size()`
- `NativeHistorySyncCompressedDecoder.clear()`

### AppState

- `extractReportingTokenContent(data)`
- `buildReportingTokenV2(data, reportingSecret)`
- `buildMutationMacPayload(operation, data, keyId)`
- `buildSnapshotMacPayload(lthash, version, name)`
- `buildPatchMacPayload(snapshotMac, valueMacs, version, type)`
- `encodeSyncActionData(index, value, version, padding?)`
- `decodeSyncActionData(data)`
- `encodeSyncdPatchRaw(patch)`
- `decodeSyncdPatchRaw(data)`
- `decodeSyncdSnapshotRaw(data)`
- `decodeSyncdMutationsRaw(data)`
- `decodeSyncdMutationsFast(mutations, keyMap, validateMacs)`
- `decodeSyncdMutationsFastWire(mutations, keyMap, validateMacs)`

### Storage

- `encodeAuthValue(value)`
- `decodeAuthValue(data)`
- `NativeKVStore`
- `NativeKVStore.get(key)`
- `NativeKVStore.getMany(keys)`
- `NativeKVStore.setMany(entries)`
- `NativeKVStore.deleteMany(keys)`
- `NativeKVStore.compact()`
- `NativeKVStore.clear()`
- `NativeKVStore.size()`

### Media

- `analyzeWavAudioFast(data, sampleCount?)`
- `NativeHashSpoolWriter(path)`
- `NativeHashSpoolWriter.update(chunk)`
- `NativeHashSpoolWriter.final()`
- `NativeHashSpoolWriter.abort()`
- `NativeMediaEncryptor(cipherKey, iv, macKey)`
- `NativeMediaEncryptor.update(chunk)`
- `NativeMediaEncryptor.final()`
- `NativeMediaEncryptToFile(cipherKey, iv, macKey, encPath, originalPath?)`
- `NativeMediaEncryptToFile.update(chunk)`
- `NativeMediaEncryptToFile.final()`
- `NativeMediaEncryptToFile.abort()`
- `NativeMediaDecryptor(cipherKey, iv, autoPadding?)`
- `NativeMediaDecryptor.update(chunk)`
- `NativeMediaDecryptor.final()`
- `NativeMediaDecryptPipeline(cipherKey, iv, options?)`
- `NativeMediaDecryptPipeline.update(chunk)`
- `NativeMediaDecryptPipeline.final()`

### Utility and Runtime Helpers

- `splitAlignedChunk(remaining, chunk, blockSize?)`
- `inflateZlibBuffer(data)`
- `decodeJidFast(jid)`
- `normalizeJidUserFast(jid)`
- `areJidsSameUserFast(jid1?, jid2?)`
- `parseJsonStringArrayFast(items)`
- `encodeWAMFast(protocolVersion, sequence, entries)`
- `normalizeLidPnMappingsFast(pairs)`
- `extractBotListV2Fast(sections)`
- `pickFirstExistingKeyFast(target, keys)`
- `isRecoverableSignalTxErrorFast(name, message, statusCode)`
- `generateSignalPubKeyFast(pubKey, prefix?)`
- `dedupeStringListFast(items)`
- `resolveSignalAddressFast(jid)`
- `buildRetryMessageKeyFast(to, id, separator?)`
- `parseRetryErrorCodeFast(errorAttr, maxCode)`
- `isMacRetryErrorCodeFast(code)`
- `buildParticipantNodesFast(participants)`
- `extractNodeAttrsFast(nodes)`
- `mapParticipantActionResultsFast(nodes, includeContent?)`
- `extractCommunityLinkedGroupsFast(groupNodes)`
- `buildBusinessProfileNodesFast(args)`
- `buildCatalogQueryParamsFast(limit?, cursor?)`
- `parseProductNodesFast(productNodes)`
- `parseCollectionNodesFast(collectionNodes)`
- `parseOrderProductNodesFast(productNodes)`
- `splitSenderKeySerializedFast(serialized, signatureLength?)`
- `NativeAlignedChunker`
- `NativeAlignedChunker.push(chunk)`
- `NativeAlignedChunker.takeRemaining()`
- `NativeAlignedChunker.size()`
- `NativeAlignedChunker.clear()`
- `NativeBufferBuilder`
- `NativeBufferBuilder.append(chunk)`
- `NativeBufferBuilder.toBuffer(reset?)`
- `NativeBufferBuilder.size()`
- `NativeBufferBuilder.clear()`
- `NativeRangeFilter`
- `NativeRangeFilter.push(bytes)`
- `NativeRangeFilter.reset()`
- `NativeRangeFilter.offset()`
- `NativeFrameBuffer`
- `NativeFrameBuffer.append(data)`
- `NativeFrameBuffer.popFrame()`
- `NativeFrameBuffer.popFrames()`
- `NativeFrameBuffer.size()`
- `NativeFrameBuffer.clear()`

### WABinary

- `initWABinaryCodec(options)`
- `encodeWABinaryNode(node, includePrefix?)`
- `decodeWABinaryNode(buffer, startIndex?)`

## Important Contract Notes

- The loader in `src/Native/baileys-native.ts` is strict; missing required exports are load failures.
- `initProtoMessageCodec(...)` is used to supply JS protobuf encode/decode callbacks to the native layer when needed.
- Windows-only or WAProto-gated code paths remain behind compile-time feature flags; do not assume the same implementation body runs on every platform.
- The media writer constructors exist in the binding, but the higher-level TypeScript path enables some of them only when `BAILEYS_NATIVE_MEDIA_WRITERS=1`.
- `useNativeAuthState()` and the compatibility alias `useMultiFileAuthState()` both depend on the native storage exports.

## Directory and File Map

### Root files

- `module.cpp`: addon initialization and registrar dispatch
- `README.md`: native overview
- `REFERENCE.md`: this document
- `OPTIMIZATION_AUDIT.md`: optimization workflow and priorities
- `DEPENDENCY_GOVERNANCE.md`: governance process for generated and vendored assets
- `native_dependency_manifest.json`: authoritative dependency hash manifest

### `module/`

- `register_exports.h`
- `register_signal_exports.cpp`
- `register_socket_exports.cpp`
- `register_proto_exports.cpp`
- `register_appstate_exports.cpp`
- `register_storage_exports.cpp`
- `register_media_exports.cpp`
- `register_utility_exports.cpp`
- `register_wabinary_exports.cpp`

### `common/`

- `common.{h,cpp}`: shared N-API readers, validators, and conversions
- `feature_gates.h`: compile-time feature switches
- `napi_guard.h`: consistent error-return helpers
- `native_error_log.h`: native-side error logging hooks
- `safe_copy.h`: bounded copy helpers

### `signal/`

- `sender_key_codec_{common,encode,decode}.cpp`
- `message_key_store.{h,cpp}`
- `e2e_session_extractor.{h,cpp}`
- `internal/sender_key_codec_shared.h`

### `socket/`

- `callback_dispatch.{h,cpp}`
- `decrypt_payload_extractor.{h,cpp}`
- `device_jid_extractor.{h,cpp}`
- `group_stub_codec.{h,cpp}`
- `message_key_codec.{h,cpp}`
- `message_node_decoder.{h,cpp}`
- `message_waiter_registry.{h,cpp}`
- `participant_batch.{h,cpp}`
- `participant_hash.{h,cpp}`
- `usync_parser.{h,cpp}`
- `internal/group_stub_flat_json.{h,cpp}`
- `internal/participant_hash_core.{h,cpp}`
- `internal/usync_parser_helpers.{h,cpp}`

### `proto/`

- `protobuf_fast_path.{h,cpp}`
- `poll_event_decoder.{h,cpp}`
- `proto_message_codec.{h,cpp}`
- `history_sync_codec.{h,cpp}`
- `history_sync_stream_decoder.{h,cpp}`
- `internal/proto_message_codec_internal.{h,cpp}`
- `reflection/protobuf_reflection_codec.h`
- `reflection/protobuf_reflection_from_js.cpp`
- `reflection/protobuf_reflection_to_js.cpp`
- `reflection/internal/protobuf_reflection_from_js_internal.h`
- `reflection/internal/protobuf_reflection_from_js_readers.cpp`
- `reflection/internal/protobuf_reflection_from_js_fields.cpp`

### `appstate/`

- `reporting_token.{h,cpp}`
- `app_state_payload.{h,cpp}`
- `mutation_engine.{h,cpp}`
- `sync_action_codec.{h,cpp}`
- `syncd_patch_codec.{h,cpp}`
- `internal/mutation_engine_internal.h`
- `internal/mutation_engine_parse.cpp`
- `internal/mutation_engine_decode.cpp`

### `storage/`

- `value_codec.{h,cpp}`
- `native_kv_store.{h,cpp}`
- `internal/native_kv_store_object.h`
- `internal/native_kv_store_api.cpp`
- `internal/native_kv_store_core.cpp`

### `media/`

- `audio_wav_analyzer.{h,cpp}`
- `media_encryptor.cpp`
- `media_decryptor.cpp`
- `media_decrypt_pipeline.cpp`
- `media_file_writers.cpp`
- `media_crypto.h`
- `internal/media_decrypt_pipeline_api.cpp`
- `internal/media_decrypt_pipeline_core.cpp`
- `internal/media_decrypt_pipeline_object.h`
- `internal/cng_crypto.{h,cpp}`

### `utils/`

- `chunk_split.{h,cpp}`
- `aligned_chunker.{h,cpp}`
- `buffer_builder.{h,cpp}`
- `frame_buffer.{h,cpp}`
- `range_filter.{h,cpp}`
- `jid_codec.{h,cpp}`
- `json_batch_parser.{h,cpp}`
- `runtime_fast_paths.{h,cpp}`
- `runtime_fast_paths/business.inc`
- `runtime_fast_paths/jid_retry.inc`
- `runtime_fast_paths/participant_attrs.inc`
- `runtime_fast_paths/sender_key.inc`
- `wam_encode.{h,cpp}`
- `zlib_helper.{h,cpp}`
- `zlib_inflate.{h,cpp}`
- `secure_memory.{h,cpp}`
- `sha256.{h,cpp}`

### `codec/wabinary/`

- `wa_binary_codec.{h,cpp}`
- `wa_binary_encode.cpp`
- `wa_binary_decode.cpp`
- `wa_binary_internal.h`

### `generated/`

- `WAProto_cpp.proto`
- `WAProto_cpp.pb.h`
- `WAProto_cpp.pb.cc`
- `WAProto_cpp.meta.json`
- `WAProto_cpp.sha256`
- `wa_proto_adapter.h`

### `third_party/`

- `miniz_adapter.h`
- `miniz/miniz.c`
- `miniz/miniz.h`
- `miniz/miniz_common.h`
- `miniz/miniz_export.h`
- `miniz/miniz_tdef.c`
- `miniz/miniz_tdef.h`
- `miniz/miniz_tinfl.c`
- `miniz/miniz_tinfl.h`
- `miniz/miniz_zip.c`
- `miniz/miniz_zip.h`

## Navigation Tips

- Start at `src/Native/baileys-native.ts` if the question is about contract or load failures.
- Start at `module.cpp` plus `module/register_*_exports.cpp` if the question is about where an export is registered.
- Start at `common/feature_gates.h` if the question is platform-specific.
- Start at `native_dependency_manifest.json` and `DEPENDENCY_GOVERNANCE.md` if the change touches generated or vendored files.
