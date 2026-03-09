{
	"targets": [
		{
			"target_name": "baileys_native",
			"sources": [
				"native/module.cpp",
				"native/module/register_signal_exports.cpp",
				"native/module/register_socket_exports.cpp",
				"native/module/register_proto_exports.cpp",
				"native/module/register_appstate_exports.cpp",
				"native/module/register_storage_exports.cpp",
				"native/module/register_media_exports.cpp",
				"native/module/register_utility_exports.cpp",
				"native/module/register_wabinary_exports.cpp",
				"native/common/common.cpp",
				"native/signal/sender_key_codec_common.cpp",
				"native/signal/sender_key_codec_encode.cpp",
				"native/signal/sender_key_codec_decode.cpp",
				"native/signal/message_key_store.cpp",
				"native/signal/e2e_session_extractor.cpp",
				"native/utils/chunk_split.cpp",
				"native/utils/aligned_chunker.cpp",
				"native/utils/buffer_builder.cpp",
				"native/media/internal/cng_crypto.cpp",
				"native/media/media_file_writers.cpp",
				"native/media/media_encryptor.cpp",
				"native/media/media_decryptor.cpp",
				"native/media/media_decrypt_pipeline.cpp",
				"native/media/internal/media_decrypt_pipeline_core.cpp",
				"native/media/internal/media_decrypt_pipeline_api.cpp",
				"native/media/audio_wav_analyzer.cpp",
				"native/utils/range_filter.cpp",
				"native/utils/jid_codec.cpp",
				"native/utils/json_batch_parser.cpp",
				"native/utils/runtime_fast_paths.cpp",
				"native/utils/wam_encode.cpp",
				"native/proto/protobuf_fast_path.cpp",
				"native/proto/poll_event_decoder.cpp",
				"native/proto/reflection/protobuf_reflection_from_js.cpp",
				"native/proto/reflection/internal/protobuf_reflection_from_js_readers.cpp",
				"native/proto/reflection/internal/protobuf_reflection_from_js_fields.cpp",
				"native/proto/reflection/protobuf_reflection_to_js.cpp",
				"native/proto/proto_message_codec.cpp",
				"native/proto/internal/proto_message_codec_internal.cpp",
				"native/proto/history_sync_codec.cpp",
				"native/proto/history_sync_stream_decoder.cpp",
				"native/appstate/reporting_token.cpp",
				"native/appstate/app_state_payload.cpp",
				"native/appstate/mutation_engine.cpp",
				"native/appstate/internal/mutation_engine_parse.cpp",
				"native/appstate/internal/mutation_engine_decode.cpp",
				"native/appstate/syncd_patch_codec.cpp",
				"native/appstate/sync_action_codec.cpp",
				"native/storage/value_codec.cpp",
				"native/storage/internal/native_kv_store_core.cpp",
				"native/storage/internal/native_kv_store_api.cpp",
				"native/socket/participant_batch.cpp",
				"native/socket/participant_hash.cpp",
				"native/socket/internal/participant_hash_core.cpp",
				"native/socket/message_key_codec.cpp",
				"native/socket/message_node_decoder.cpp",
				"native/socket/message_waiter_registry.cpp",
				"native/socket/group_stub_codec.cpp",
				"native/socket/internal/group_stub_flat_json.cpp",
				"native/socket/callback_dispatch.cpp",
				"native/socket/decrypt_payload_extractor.cpp",
				"native/socket/usync_parser.cpp",
				"native/socket/internal/usync_parser_helpers.cpp",
				"native/socket/device_jid_extractor.cpp",
				"native/utils/zlib_helper.cpp",
				"native/utils/zlib_inflate.cpp",
				"native/utils/secure_memory.cpp",
				"native/utils/sha256.cpp",
				"native/third_party/miniz/miniz.c",
				"native/third_party/miniz/miniz_tdef.c",
				"native/third_party/miniz/miniz_tinfl.c",
				"native/third_party/miniz/miniz_zip.c",
				"native/storage/native_kv_store.cpp",
				"native/codec/wabinary/wa_binary_codec.cpp",
				"native/codec/wabinary/wa_binary_encode.cpp",
				"native/codec/wabinary/wa_binary_decode.cpp",
				"native/utils/frame_buffer.cpp"
			],
			"include_dirs": [
				"<!@(node -p \"require('node-addon-api').include\")",
				"native",
				"native/generated"
			],
			"dependencies": [
				"<!(node -p \"require('node-addon-api').gyp\")"
			],
			"defines": [
				"NAPI_DISABLE_CPP_EXCEPTIONS",
				"NODE_ADDON_API_DISABLE_DEPRECATED"
			],
			"cflags": [
				"-O3",
				"-flto",
				"-fstrict-aliasing",
				"-fdata-sections",
				"-ffunction-sections",
				"-fvisibility=hidden"
			],
			"cflags_cc": [
				"-std=c++20",
				"-O3",
				"-flto",
				"-fstrict-aliasing",
				"-fdata-sections",
				"-ffunction-sections",
				"-fvisibility=hidden",
				"-fno-rtti",
				"-fno-exceptions"
			],
			"ldflags": [
				"-flto",
				"-Wl,--gc-sections"
			],
			"xcode_settings": {
				"GCC_OPTIMIZATION_LEVEL": "3",
				"CLANG_CXX_LANGUAGE_STANDARD": "c++20",
				"CLANG_CXX_LIBRARY": "libc++",
				"MACOSX_DEPLOYMENT_TARGET": "11.0",
				"OTHER_CFLAGS": [
					"-O3",
					"-flto",
					"-fstrict-aliasing",
					"-fvisibility=hidden"
				],
				"OTHER_CPLUSPLUSFLAGS": [
					"-O3",
					"-flto",
					"-fstrict-aliasing",
					"-fvisibility=hidden",
					"-fno-rtti",
					"-fno-exceptions"
				],
				"OTHER_LDFLAGS": [
					"-flto",
					"-Wl,-dead_strip"
				]
			},
			"msvs_settings": {
				"VCCLCompilerTool": {
					"Optimization": 3,
					"FavorSizeOrSpeed": 1,
					"InlineFunctionExpansion": 2,
					"WholeProgramOptimization": "true",
					"OmitFramePointers": "true",
					"StringPooling": "true",
					"EnableFiberSafeOptimizations": "true",
					"ExceptionHandling": 0,
					"RuntimeTypeInfo": "false",
					"MultiProcessorCompilation": "true",
					"AdditionalOptions": [
						"/std:c++20",
						"/fp:fast",
						"/GL",
						"/Gy",
						"/Gw",
						"/Oi",
						"/Ot",
						"/Qpar"
					]
				},
				"VCLinkerTool": {
					"LinkTimeCodeGeneration": 1,
					"OptimizeReferences": 2,
					"EnableCOMDATFolding": 2,
					"AdditionalOptions": [
						"/LTCG",
						"/OPT:REF",
						"/OPT:ICF"
					]
				}
			},
			"conditions": [
				[
					"OS=='win'",
					{
						"defines": [
							"BAILEYS_HAS_NATIVE_WAPROTO=1"
						],
						"sources": [
							"native/generated/WAProto_cpp.pb.cc"
						],
						"include_dirs": [
							"<!(node scripts/native-protobuf-gyp.cjs include)"
						],
						"libraries": [
							"<!@(node scripts/native-protobuf-gyp.cjs libs)",
							"bcrypt.lib"
						]
					}
				],
				[
					"OS!='win'",
					{
						"defines": [
							"BAILEYS_HAS_NATIVE_WAPROTO=0"
						]
					}
				],
				[
					"OS=='linux'",
					{
						"cflags_cc": [
							"-fno-plt"
						],
						"ldflags": [
							"-Wl,-O3"
						]
					}
				]
			]
		}
	]
}
