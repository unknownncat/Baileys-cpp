import { realpathSync, statSync } from 'fs'
import { createRequire } from 'module'
import type { BinaryNodeCodingOptions } from '../WABinary/types'

export type NativeSenderMessageKey = {
	iteration: number
	seed: Uint8Array
}

export type NativeSenderKeyState = {
	senderKeyId: number
	senderChainKey: {
		iteration: number
		seed: Uint8Array
	}
	senderSigningKey: {
		public: Uint8Array
		private?: Uint8Array
	}
	senderMessageKeys: NativeSenderMessageKey[]
}

export interface NativeMessageKeyStore {
	has(iteration: number): boolean
	add(iteration: number, seed: Uint8Array, maxKeys?: number): void
	remove(iteration: number): NativeSenderMessageKey | null
	toArray(): NativeSenderMessageKey[]
	encodeSenderKeyRecord?(
		senderKeyId: number,
		chainIteration: number,
		chainSeed: Uint8Array,
		signingPublic: Uint8Array,
		signingPrivate?: Uint8Array
	): Buffer
	size(): number
}

export interface NativeFrameBuffer {
	append(data: Uint8Array): void
	popFrame?(): Buffer | null
	popFrames(): Buffer[]
	size(): number
	clear(): void
}

export interface NativeMessageWaiterRegistry {
	registerWaiter(msgId: string, token: number, deadlineMs: number): void
	resolveMessage(msgId: string): number[]
	removeWaiter(token: number): boolean
	evictExpired(nowMs: number): number[]
	rejectAll(): number[]
	size(): number
	clear(): void
}

export interface NativeHistorySyncCompressedDecoder {
	append(chunk: Uint8Array): void
	decode(reset?: boolean): unknown | null
	size(): number
	clear(): void
}

export interface NativeAlignedChunker {
	push(chunk: Uint8Array): Buffer
	takeRemaining(): Buffer
	size(): number
	clear(): void
}

export interface NativeBufferBuilder {
	append(chunk: Uint8Array): void
	toBuffer(reset?: boolean): Buffer
	size(): number
	clear(): void
}

export interface NativeRangeFilter {
	push(bytes: Uint8Array): Buffer
	reset(): void
	offset(): number
}

export interface NativeMediaEncryptFinalResult {
	finalChunk: Buffer
	mac: Buffer
	fileSha256: Buffer
	fileEncSha256: Buffer
}

export interface NativeHashSpoolResult {
	fileSha256: Buffer
	fileLength: number
}

export interface NativeHashSpoolWriter {
	update(chunk: Uint8Array): void
	final(): NativeHashSpoolResult
	abort(): void
}

export interface NativeMediaEncryptor {
	update(chunk: Uint8Array): Buffer
	final(): NativeMediaEncryptFinalResult
}

export interface NativeMediaEncryptToFileResult {
	mac: Buffer
	fileSha256: Buffer
	fileEncSha256: Buffer
	fileLength: number
}

export interface NativeMediaEncryptToFile {
	update(chunk: Uint8Array): void
	final(): NativeMediaEncryptToFileResult
	abort(): void
}

export interface NativeMediaDecryptor {
	update(chunk: Uint8Array): Buffer
	final(): Buffer
}

export type NativeMediaDecryptPipelineOptions = {
	firstBlockIsIV?: boolean
	autoPadding?: boolean
	startByte?: number
	endByte?: number
	initialOffset?: number
}

export interface NativeMediaDecryptPipeline {
	update(chunk: Uint8Array): Buffer
	final(): Buffer
}

export type NativeSyncActionDataWire = {
	index: Buffer
	value: Buffer
	padding?: Buffer
	version?: number
}

export type NativeDerivedMutationKey = {
	indexKey: Uint8Array
	valueEncryptionKey: Uint8Array
	valueMacKey: Uint8Array
}

export type NativeSyncdMutationFastInput = {
	operation?: number
	indexMac: Uint8Array
	valueBlob: Uint8Array
	keyId: Uint8Array
}

export type NativeSyncdMutationFastOutput = {
	operation: number
	indexMac: Buffer
	valueMac: Buffer
	syncAction: {
		index: Buffer
		indexString?: string
		value: unknown
		padding?: Buffer
		version?: number
	}
}

export type NativeKVMutation = {
	key: string
	value?: Uint8Array | null
}

export type NativeEncryptedParticipantInput = {
	jid: string
	type: string
	ciphertext: Uint8Array
}

export type NativeParticipantBatchResult = {
	nodes: unknown[]
	shouldIncludeDeviceIdentity: boolean
}

export type NativeDecodedJid = {
	server: string
	user: string
	domainType?: number
	device?: number
}

export type NativeDecodedMessageNode = {
	key: {
		remoteJid: string
		fromMe: boolean
		id?: string
		participant?: string
		remoteJidAlt?: string
		participantAlt?: string
		addressingMode?: string
		server_id?: string
	}
	author: string
	sender: string
	category?: string
	messageTimestamp: number
	pushName?: string
	broadcast: boolean
}

export type NativeFullJid = {
	user: string
	device?: number
	domainType?: number
	server: string
}

export type NativeWAMEncodeItem = {
	key: number
	value: number | string | null
	flag: number
}

export type NativeLidPnUserPair = {
	pnUser: string
	lidUser: string
}

export type NativeSignalAddress = {
	signalUser: string
	user: string
	server: string
	device: number
	domainType: number
}

export type NativeParticipantActionResult = {
	status: string
	jid: string
	content?: unknown
}

export type NativeLinkedGroupInfo = {
	id?: string
	subject: string
	creation?: number
	owner?: string
	size?: number
}

export type NativeSenderKeySerializedParts = {
	version: number
	message: Buffer
	signature: Buffer
}

export type NativeGroupParticipantStub = {
	id: string
	phoneNumber?: string
	lid?: string
	admin?: string | null
}

export type NativeE2ESessionBundle = {
	jid: string
	session: {
		registrationId: number
		identityKey: Buffer
		signedPreKey: {
			keyId: number
			publicKey: Buffer
			signature: Buffer
		}
		preKey: {
			keyId: number
			publicKey: Buffer
		}
	}
}

export interface NativeKVStore {
	get(key: string): Buffer | null
	getMany(keys: string[]): (Buffer | null)[]
	setMany(entries: NativeKVMutation[]): void
	deleteMany(keys: string[]): void
	compact(): boolean
	clear(): void
	size(): number
}

export interface NativeBinding {
	encodeSenderKeyStates(states: NativeSenderKeyState[]): Buffer
	decodeSenderKeyStates(data: Uint8Array): NativeSenderKeyState[]
	isSenderKeyBinary(data: Uint8Array): boolean
	extractE2ESessionBundlesFast?(users: unknown[]): NativeE2ESessionBundle[]
	padMessageWithLength?(data: Uint8Array, padLength: number): Buffer
	getUnpaddedLengthMax16?(data: Uint8Array): number
	initProtoMessageCodec?(
		encodeFn?: (message: unknown) => { finish(): Uint8Array },
		decodeFn?: (data: Uint8Array) => unknown
	): boolean
	encodeProtoMessageRaw?(message: unknown): Buffer
	encodeProtoMessageWithPad?(message: unknown, padLength: number): Buffer
	decodeProtoMessageRaw?(data: Uint8Array): unknown
	decodeProtoMessageFromPadded?(data: Uint8Array): unknown
	decodeProtoMessagesRawBatch?(items: Uint8Array[]): unknown[]
	decodeProtoMessagesFromPaddedBatch?(items: Uint8Array[]): unknown[]
	decodePollVoteMessageFast?(
		encPayload: Uint8Array,
		encIv: Uint8Array,
		pollMsgId: string,
		pollCreatorJid: string,
		voterJid: string,
		pollEncKey: Uint8Array
	): unknown | null
	decodeEventResponseMessageFast?(
		encPayload: Uint8Array,
		encIv: Uint8Array,
		eventMsgId: string,
		eventCreatorJid: string,
		responderJid: string,
		eventEncKey: Uint8Array
	): unknown | null
	extractReportingTokenContent?(data: Uint8Array): Buffer | null
	buildReportingTokenV2?(data: Uint8Array, reportingSecret: Uint8Array): Buffer | null
	buildMutationMacPayload?(operation: number, data: Uint8Array, keyId: Uint8Array): Buffer
	buildSnapshotMacPayload?(lthash: Uint8Array, version: number, name: string): Buffer
	buildPatchMacPayload?(snapshotMac: Uint8Array, valueMacs: Uint8Array[], version: number, type: string): Buffer
	encodeSyncdPatchRaw?(patch: unknown): Buffer
	decodeSyncdPatchRaw?(data: Uint8Array): unknown
	decodeSyncdSnapshotRaw?(data: Uint8Array): unknown
	decodeSyncdMutationsRaw?(data: Uint8Array): unknown
	decodeSyncdMutationsFast?(
		mutations: NativeSyncdMutationFastInput[],
		keyMap: Record<string, NativeDerivedMutationKey>,
		validateMacs: boolean
	): NativeSyncdMutationFastOutput[]
	decodeSyncdMutationsFastWire?(
		mutations: NativeSyncdMutationFastInput[],
		keyMap: Record<string, NativeDerivedMutationKey>,
		validateMacs: boolean
	): NativeSyncdMutationFastOutput[]
	decodeHistorySyncRaw?(data: Uint8Array): unknown
	decodeCompressedHistorySyncRaw?(data: Uint8Array): unknown
	analyzeWavAudioFast?(data: Uint8Array, sampleCount?: number): { durationSec: number; waveform: Buffer } | null
	encodeSyncActionData?(index: Uint8Array, value: Uint8Array, version: number, padding?: Uint8Array): Buffer
	decodeSyncActionData?(data: Uint8Array): NativeSyncActionDataWire | null
	encodeAuthValue?(value: unknown): Buffer
	decodeAuthValue?(data: Uint8Array): unknown
	inflateZlibBuffer?(data: Uint8Array): Buffer
	initWABinaryCodec?(options: BinaryNodeCodingOptions): boolean
	encodeWABinaryNode?(node: unknown, includePrefix?: boolean): Buffer
	decodeWABinaryNode?(buffer: Uint8Array, startIndex?: number): { node: unknown; nextIndex: number }
	decodeJidFast?(jid: string): NativeDecodedJid | null
	normalizeJidUserFast?(jid: string): string
	areJidsSameUserFast?(jid1?: string, jid2?: string): boolean
	encodeWAMFast?(protocolVersion: number, sequence: number, entries: NativeWAMEncodeItem[]): Buffer
	normalizeLidPnMappingsFast?(pairs: { lid: string; pn: string }[]): NativeLidPnUserPair[]
	extractBotListV2Fast?(sections: unknown[]): { jid: string; personaId: string }[]
	pickFirstExistingKeyFast?(target: unknown, keys: string[]): string | null
	isRecoverableSignalTxErrorFast?(name: string, message: string, statusCode: number): boolean
	generateSignalPubKeyFast?(pubKey: Uint8Array, prefix?: number): Buffer
	dedupeStringListFast?(items: string[]): string[]
	resolveSignalAddressFast?(jid: string): NativeSignalAddress | null
	buildRetryMessageKeyFast?(to: string, id: string, separator?: string): string
	parseRetryErrorCodeFast?(errorAttr: string | null | undefined, maxCode: number): number | null
	isMacRetryErrorCodeFast?(code: number): boolean
	buildParticipantNodesFast?(participants: string[]): unknown[]
	extractNodeAttrsFast?(nodes: unknown[]): Record<string, unknown>[]
	mapParticipantActionResultsFast?(nodes: unknown[], includeContent?: boolean): NativeParticipantActionResult[]
	extractCommunityLinkedGroupsFast?(groupNodes: unknown[]): NativeLinkedGroupInfo[]
	buildBusinessProfileNodesFast?(args: unknown): unknown[]
	buildCatalogQueryParamsFast?(limit?: number, cursor?: string): unknown[]
	parseProductNodesFast?(productNodes: unknown[]): unknown[]
	parseCollectionNodesFast?(collectionNodes: unknown[]): unknown[]
	parseOrderProductNodesFast?(productNodes: unknown[]): unknown[]
	splitSenderKeySerializedFast?(serialized: Uint8Array, signatureLength?: number): NativeSenderKeySerializedParts | null
	parseJsonStringArrayFast?(items: string[]): unknown[] | null
	encodeGroupParticipantStubsFast?(participants: NativeGroupParticipantStub[]): string[] | null
	parseGroupParticipantStubsFast?(items: string[]): unknown[] | null
	decodeMessageNodeFast?(attrs: Record<string, unknown>, meId: string, meLid: string): NativeDecodedMessageNode | null
	stringifyMessageKeyFast?(key: unknown): string
	stringifyMessageKeysFast?(keys: unknown[]): string[]
	stringifyMessageKeysFromMessagesFast?(messages: unknown[]): string[]
	stringifyMessageKeysFromEntriesFast?(entries: unknown[]): string[]
	buildSocketCallbackEventKeys?(frame: unknown, callbackPrefix: string): string[]
	emitSocketCallbackEvents?(emitter: unknown, frame: unknown, callbackPrefix?: string, tagPrefix?: string): boolean
	parseUSyncQueryResultFast?(result: unknown, protocolNames: string[]): { list: unknown[]; sideList: unknown[] } | null
	extractDecryptPayloadsFast?(stanza: unknown): {
		payloads: { messageType: string; content: Buffer; padded: boolean }[]
		hasViewOnceUnavailable?: boolean
		retryCount?: number
	} | null
	extractDeviceJidsFast?(result: unknown[], myJid: string, myLid: string, excludeZeroDevices: boolean): NativeFullJid[]
	NativeFrameBuffer?: new () => NativeFrameBuffer
	NativeMessageWaiterRegistry?: new () => NativeMessageWaiterRegistry
	NativeHistorySyncCompressedDecoder?: new () => NativeHistorySyncCompressedDecoder
	NativeAlignedChunker?: new (blockSize?: number) => NativeAlignedChunker
	NativeHashSpoolWriter?: new (path: string) => NativeHashSpoolWriter
	NativeMediaEncryptor?: new (cipherKey: Uint8Array, iv: Uint8Array, macKey: Uint8Array) => NativeMediaEncryptor
	NativeMediaEncryptToFile?: new (
		cipherKey: Uint8Array,
		iv: Uint8Array,
		macKey: Uint8Array,
		encPath: string,
		originalPath?: string
	) => NativeMediaEncryptToFile
	NativeMediaDecryptor?: new (cipherKey: Uint8Array, iv: Uint8Array, autoPadding?: boolean) => NativeMediaDecryptor
	NativeMediaDecryptPipeline?: new (
		cipherKey: Uint8Array,
		iv: Uint8Array,
		options?: NativeMediaDecryptPipelineOptions
	) => NativeMediaDecryptPipeline
	NativeBufferBuilder?: new () => NativeBufferBuilder
	NativeRangeFilter?: new (startByte?: number, endByte?: number, initialOffset?: number) => NativeRangeFilter
	splitAlignedChunk(
		remaining: Uint8Array,
		chunk: Uint8Array,
		blockSize?: number
	): { decryptable: Buffer; remaining: Buffer }
	buildParticipantNodesBatch?(
		encryptedItems: NativeEncryptedParticipantInput[],
		extraAttrs?: Record<string, string | number | boolean>
	): NativeParticipantBatchResult
	generateParticipantHashV2Fast?(participants: string[]): string
	NativeMessageKeyStore: new (keys?: NativeSenderMessageKey[], maxKeys?: number) => NativeMessageKeyStore
	NativeKVStore?: new (
		path: string,
		options?: { compactThresholdBytes?: number; compactRatio?: number; maxQueuedBytes?: number }
	) => NativeKVStore
}

export type NativeBindingLoadInfo = {
	loaded: boolean
	candidate: string
	resolvedPath: string
	realPath: string
	sizeBytes: number | null
	mtimeIso: string | null
	exportCount: number
	exportSample: string[]
	moduleLoadListMatches: string[]
	reportSharedObjectMatches: string[]
}

const require = createRequire(import.meta.url)
let loadedBindingCandidate = ''
let loadedBindingPath = ''

const REQUIRED_CORE_EXPORTS: (keyof NativeBinding)[] = [
	'encodeSenderKeyStates',
	'decodeSenderKeyStates',
	'isSenderKeyBinary',
	'splitAlignedChunk',
	'NativeMessageKeyStore'
]

const REQUIRED_STRICT_NATIVE_EXPORTS: (keyof NativeBinding)[] = [
	'encodeAuthValue',
	'decodeAuthValue',
	'NativeKVStore',
	'buildMutationMacPayload',
	'buildSnapshotMacPayload',
	'buildPatchMacPayload',
	'encodeSyncActionData',
	'decodeSyncActionData',
	'encodeSyncdPatchRaw',
	'decodeSyncdPatchRaw',
	'decodeSyncdSnapshotRaw',
	'decodeSyncdMutationsRaw',
	'decodeSyncdMutationsFast',
	'decodeSyncdMutationsFastWire',
	'buildReportingTokenV2',
	'extractReportingTokenContent',
	'padMessageWithLength',
	'getUnpaddedLengthMax16',
	'initProtoMessageCodec',
	'encodeProtoMessageRaw',
	'encodeProtoMessageWithPad',
	'decodeProtoMessageRaw',
	'decodeProtoMessageFromPadded',
	'decodeProtoMessagesRawBatch',
	'decodeProtoMessagesFromPaddedBatch',
	'decodePollVoteMessageFast',
	'decodeEventResponseMessageFast',
	'decodeHistorySyncRaw',
	'decodeCompressedHistorySyncRaw',
	'NativeHistorySyncCompressedDecoder',
	'inflateZlibBuffer',
	'decodeJidFast',
	'normalizeJidUserFast',
	'areJidsSameUserFast',
	'encodeWAMFast',
	'normalizeLidPnMappingsFast',
	'extractBotListV2Fast',
	'pickFirstExistingKeyFast',
	'isRecoverableSignalTxErrorFast',
	'generateSignalPubKeyFast',
	'dedupeStringListFast',
	'resolveSignalAddressFast',
	'buildRetryMessageKeyFast',
	'parseRetryErrorCodeFast',
	'isMacRetryErrorCodeFast',
	'buildParticipantNodesFast',
	'extractNodeAttrsFast',
	'mapParticipantActionResultsFast',
	'extractCommunityLinkedGroupsFast',
	'buildBusinessProfileNodesFast',
	'buildCatalogQueryParamsFast',
	'parseProductNodesFast',
	'parseCollectionNodesFast',
	'parseOrderProductNodesFast',
	'splitSenderKeySerializedFast',
	'parseJsonStringArrayFast',
	'parseUSyncQueryResultFast',
	'extractE2ESessionBundlesFast',
	'extractDeviceJidsFast',
	'encodeGroupParticipantStubsFast',
	'parseGroupParticipantStubsFast',
	'buildParticipantNodesBatch',
	'generateParticipantHashV2Fast',
	'buildSocketCallbackEventKeys',
	'emitSocketCallbackEvents',
	'NativeMessageWaiterRegistry'
]

const getMissingExports = (binding: NativeBinding, keys: readonly (keyof NativeBinding)[]) =>
	keys.filter(key => typeof binding[key] === 'undefined' || binding[key] === null)

const resolveNativeBinding = (): NativeBinding => {
	const candidates = ['../../build/Release/baileys_native.node', '../../build/Debug/baileys_native.node']
	const loadErrors: string[] = []

	for (const candidate of candidates) {
		try {
			const resolvedCandidate = require.resolve(candidate)
			const loaded = require(resolvedCandidate) as NativeBinding
			if (!loaded) {
				loadErrors.push(`${candidate}: empty binding`)
				continue
			}

			const missingCore = getMissingExports(loaded, REQUIRED_CORE_EXPORTS)
			if (missingCore.length) {
				loadErrors.push(`${candidate}: missing core exports [${missingCore.join(', ')}]`)
				continue
			}

			const missingStrict = getMissingExports(loaded, REQUIRED_STRICT_NATIVE_EXPORTS)
			if (missingStrict.length) {
				loadErrors.push(`${candidate}: missing strict exports [${missingStrict.join(', ')}]`)
				continue
			}

			if (
				typeof loaded.initWABinaryCodec !== 'function' ||
				typeof loaded.encodeWABinaryNode !== 'function' ||
				typeof loaded.decodeWABinaryNode !== 'function'
			) {
				loadErrors.push(`${candidate}: missing WABinary codec exports`)
				continue
			}

			loadedBindingCandidate = candidate
			loadedBindingPath = resolvedCandidate
			return loaded
		} catch (error) {
			loadErrors.push(`${candidate}: ${(error as Error)?.message || String(error)}`)
		}
	}

	throw new Error(
		`Strict native mode requires baileys_native.node with full export coverage. Load attempts failed: ${loadErrors.join(
			' | '
		)}`
	)
}

export const BAILEYS_NATIVE = resolveNativeBinding()

export const getNativeBindingLoadInfo = (): NativeBindingLoadInfo => {
	let realPath = loadedBindingPath
	let sizeBytes: number | null = null
	let mtimeIso: string | null = null

	try {
		realPath = realpathSync(loadedBindingPath)
	} catch { }

	try {
		const stats = statSync(loadedBindingPath)
		sizeBytes = stats.size
		mtimeIso = stats.mtime.toISOString()
	} catch { }

	const bindingName = 'baileys_native.node'
	const lowerBindingName = bindingName.toLowerCase()
	const moduleLoadList = (process as NodeJS.Process & { moduleLoadList?: string[] }).moduleLoadList || []
	const moduleLoadListMatches = moduleLoadList.filter((entry: string) => entry.toLowerCase().includes(lowerBindingName))
	const report = typeof process.report?.getReport === 'function' ? (process.report.getReport() as { sharedObjects?: unknown[] }) : undefined
	const sharedObjects = Array.isArray(report?.sharedObjects) ? report.sharedObjects : []
	const reportSharedObjectMatches = sharedObjects.filter(
		(entry: unknown): entry is string => typeof entry === 'string' && entry.toLowerCase().includes(lowerBindingName)
	)
	const loaded = !!loadedBindingCandidate && !!loadedBindingPath && Object.keys(BAILEYS_NATIVE).length > 0

	return {
		loaded,
		candidate: loadedBindingCandidate,
		resolvedPath: loadedBindingPath,
		realPath,
		sizeBytes,
		mtimeIso,
		exportCount: Object.keys(BAILEYS_NATIVE).length,
		exportSample: Object.keys(BAILEYS_NATIVE).sort().slice(0, 12),
		moduleLoadListMatches,
		reportSharedObjectMatches
	}
}

export const requireNativeExport = <TKey extends keyof NativeBinding>(name: TKey): NonNullable<NativeBinding[TKey]> => {
	const value = BAILEYS_NATIVE[name]
	if (typeof value === 'undefined' || value === null) {
		throw new Error(`Strict native mode missing BAILEYS_NATIVE.${String(name)}`)
	}

	return value as NonNullable<NativeBinding[TKey]>
}

export const getOptionalNativeExport = <TKey extends keyof NativeBinding>(name: TKey) => {
	const value = BAILEYS_NATIVE[name]
	if (typeof value === 'undefined' || value === null) {
		return undefined
	}

	return value as NonNullable<NativeBinding[TKey]>
}
