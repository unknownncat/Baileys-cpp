import { Boom } from '@hapi/boom'
import { expandAppStateKeys } from 'whatsapp-rust-bridge'
import { proto } from '../../WAProto/index.js'
import {
	type NativeDerivedMutationKey,
	type NativeSyncdMutationFastInput,
	requireNativeExport
} from '../Native/baileys-native'
import type {
	BaileysEventEmitter,
	Chat,
	ChatModification,
	ChatMutation,
	ChatUpdate,
	Contact,
	InitialAppStateSyncOptions,
	LastMessageList,
	LTHashState,
	WAPatchCreate,
	WAPatchName
} from '../Types'
import {
	type ChatLabelAssociation,
	LabelAssociationType,
	type MessageLabelAssociation
} from '../Types/LabelAssociation'
import { type BinaryNode, getBinaryNodeChild, getBinaryNodeChildren, isJidGroup, jidNormalizedUser } from '../WABinary'
import { aesEncrypt, hmacSign } from './crypto'
import { toNumber } from './generics'
import type { ILogger } from './logger'
import { LT_HASH_ANTI_TAMPERING } from './lt-hash'
import { downloadContentFromMessage, toBuffer } from './messages-media'
import { emitSyncActionResults, processContactAction } from './sync-action-utils'

type FetchAppStateSyncKey = (keyId: string) => Promise<proto.Message.IAppStateSyncKeyData | null | undefined>

export type ChatMutationMap = { [index: string]: ChatMutation }

const nativeBuildMutationMacPayload = requireNativeExport('buildMutationMacPayload')
const nativeBuildSnapshotMacPayload = requireNativeExport('buildSnapshotMacPayload')
const nativeBuildPatchMacPayload = requireNativeExport('buildPatchMacPayload')
const nativeEncodeSyncActionData = requireNativeExport('encodeSyncActionData')
const nativeEncodeSyncdPatchRaw = requireNativeExport('encodeSyncdPatchRaw')
const nativeDecodeSyncdPatchRaw = requireNativeExport('decodeSyncdPatchRaw')
const nativeDecodeSyncdSnapshotRaw = requireNativeExport('decodeSyncdSnapshotRaw')
const nativeDecodeSyncdMutationsRaw = requireNativeExport('decodeSyncdMutationsRaw')
const nativeDecodeSyncdMutationsFastWire = requireNativeExport('decodeSyncdMutationsFastWire')
const nativeParseJsonStringArrayFast = requireNativeExport('parseJsonStringArrayFast')

const mutationKeys = (keydata: Uint8Array) => {
	const keys = expandAppStateKeys(keydata)
	return {
		indexKey: keys.indexKey,
		valueEncryptionKey: keys.valueEncryptionKey,
		valueMacKey: keys.valueMacKey,
		snapshotMacKey: keys.snapshotMacKey,
		patchMacKey: keys.patchMacKey
	}
}

const generateMac = (
	operation: proto.SyncdMutation.SyncdOperation,
	data: Uint8Array,
	keyId: Uint8Array | string,
	key: Uint8Array
) => {
	const opByte = operation === proto.SyncdMutation.SyncdOperation.SET ? 0x01 : 0x02
	const keyIdBuffer = typeof keyId === 'string' ? Buffer.from(keyId, 'base64') : keyId
	const payload = nativeBuildMutationMacPayload(opByte === 0x01 ? 0 : 1, data, keyIdBuffer)
	const hmac = hmacSign(payload, key, 'sha512')
	return hmac.subarray(0, 32)
}

type Mac = { indexMac: Uint8Array; valueMac: Uint8Array; operation: proto.SyncdMutation.SyncdOperation }

const makeLtHashGenerator = ({ indexValueMap, hash }: Pick<LTHashState, 'hash' | 'indexValueMap'>) => {
	indexValueMap = { ...indexValueMap }
	const addBuffs: Uint8Array[] = []
	const subBuffs: Uint8Array[] = []

	return {
		mix: ({ indexMac, valueMac, operation }: Mac) => {
			const indexMacBase64 = Buffer.from(indexMac).toString('base64')
			const prevOp = indexValueMap[indexMacBase64]
			if (operation === proto.SyncdMutation.SyncdOperation.REMOVE) {
				if (!prevOp) {
					throw new Boom('tried remove, but no previous op', { data: { indexMac, valueMac } })
				}

				// remove from index value mac, since this mutation is erased
				delete indexValueMap[indexMacBase64]
			} else {
				addBuffs.push(valueMac)
				// add this index into the history map
				indexValueMap[indexMacBase64] = { valueMac }
			}

			if (prevOp) {
				subBuffs.push(prevOp.valueMac as Uint8Array)
			}
		},
		finish: () => {
			const result = LT_HASH_ANTI_TAMPERING.subtractThenAdd(hash, subBuffs, addBuffs)

			return {
				hash: Buffer.from(result),
				indexValueMap
			}
		}
	}
}

const encodeSyncActionDataWire = (
	index: Uint8Array,
	value: proto.ISyncActionValue,
	version: number,
	padding: Uint8Array = new Uint8Array(0)
) => {
	const encodedValue = proto.SyncActionValue.encode(value).finish()
	return nativeEncodeSyncActionData(index, encodedValue, version, padding)
}

export const encodeSyncdPatchWire = (patch: proto.ISyncdPatch): Uint8Array => {
	return nativeEncodeSyncdPatchRaw(patch)
}

const decodeSyncdPatchWire = (data: Uint8Array): proto.ISyncdPatch => {
	const decoded = nativeDecodeSyncdPatchRaw(data)
	if (decoded) {
		return decoded as proto.ISyncdPatch
	}

	throw new Boom('native decodeSyncdPatchRaw returned invalid payload', { statusCode: 500 })
}

const decodeSyncdSnapshotWire = (data: Uint8Array): proto.ISyncdSnapshot => {
	const decoded = nativeDecodeSyncdSnapshotRaw(data)
	if (decoded) {
		return decoded as proto.ISyncdSnapshot
	}

	throw new Boom('native decodeSyncdSnapshotRaw returned invalid payload', { statusCode: 500 })
}

const decodeSyncdMutationsWire = (data: Uint8Array): proto.ISyncdMutations => {
	const decoded = nativeDecodeSyncdMutationsRaw(data)
	if (decoded) {
		return decoded as proto.ISyncdMutations
	}

	throw new Boom('native decodeSyncdMutationsRaw returned invalid payload', { statusCode: 500 })
}

const generateSnapshotMac = (lthash: Uint8Array, version: number, name: WAPatchName, key: Uint8Array) => {
	const payload = nativeBuildSnapshotMacPayload(lthash, version, name)
	return hmacSign(payload, key, 'sha256')
}

const generatePatchMac = (
	snapshotMac: Uint8Array,
	valueMacs: Uint8Array[],
	version: number,
	type: WAPatchName,
	key: Uint8Array
) => {
	const payload = nativeBuildPatchMacPayload(snapshotMac, valueMacs, version, type)
	return hmacSign(payload, key)
}

export const newLTHashState = (): LTHashState => ({ version: 0, hash: Buffer.alloc(128), indexValueMap: {} })

export const encodeSyncdPatch = async (
	{ type, index, syncAction, apiVersion, operation }: WAPatchCreate,
	myAppStateKeyId: string,
	state: LTHashState,
	getAppStateSyncKey: FetchAppStateSyncKey
) => {
	const key = !!myAppStateKeyId ? await getAppStateSyncKey(myAppStateKeyId) : undefined
	if (!key) {
		throw new Boom(`myAppStateKey ("${myAppStateKeyId}") not present`, { statusCode: 404 })
	}

	const encKeyId = Buffer.from(myAppStateKeyId, 'base64')

	state = { ...state, indexValueMap: { ...state.indexValueMap } }

	const indexBuffer = Buffer.from(JSON.stringify(index))
	const encoded = encodeSyncActionDataWire(indexBuffer, syncAction, apiVersion)

	const keyValue = mutationKeys(key.keyData!)

	const encValue = aesEncrypt(encoded, keyValue.valueEncryptionKey)
	const valueMac = generateMac(operation, encValue, encKeyId, keyValue.valueMacKey)
	const indexMac = hmacSign(indexBuffer, keyValue.indexKey)

	// update LT hash
	const generator = makeLtHashGenerator(state)
	generator.mix({ indexMac, valueMac, operation })
	Object.assign(state, generator.finish())

	state.version += 1

	const snapshotMac = generateSnapshotMac(state.hash, state.version, type, keyValue.snapshotMacKey)

	const patch: proto.ISyncdPatch = {
		patchMac: generatePatchMac(snapshotMac, [valueMac], state.version, type, keyValue.patchMacKey),
		snapshotMac: snapshotMac,
		keyId: { id: encKeyId },
		mutations: [
			{
				operation: operation,
				record: {
					index: {
						blob: indexMac
					},
					value: {
						blob: Buffer.concat([encValue, valueMac])
					},
					keyId: { id: encKeyId }
				}
			}
		]
	}

	const base64Index = indexMac.toString('base64')
	state.indexValueMap[base64Index] = { valueMac }

	return { patch, state }
}

export const decodeSyncdMutations = async (
	msgMutations: (proto.ISyncdMutation | proto.ISyncdRecord)[],
	initialState: LTHashState,
	getAppStateSyncKey: FetchAppStateSyncKey,
	onMutation: (mutation: ChatMutation) => void,
	validateMacs: boolean
) => {
	const ltGenerator = makeLtHashGenerator(initialState)
	const derivedKeyCache = new Map<string, ReturnType<typeof mutationKeys>>()
	const parsedIndexCache = new Map<string, unknown>()
	const hydrateParsedIndexCache = (indexes: readonly string[]) => {
		const uncached = indexes.filter(index => !parsedIndexCache.has(index))
		if (!uncached.length) {
			return
		}

		const parsedValues = nativeParseJsonStringArrayFast(uncached)
		if (!Array.isArray(parsedValues) || parsedValues.length !== uncached.length) {
			throw new Boom('native parseJsonStringArrayFast returned invalid payload', { statusCode: 500 })
		}

		for (let i = 0; i < uncached.length; i += 1) {
			parsedIndexCache.set(uncached[i]!, parsedValues[i])
		}
	}

	const parseIndex = (index: Uint8Array, indexString?: string) => {
		const indexStr = indexString ?? Buffer.from(index).toString()
		if (parsedIndexCache.has(indexStr)) {
			return { indexStr, index: parsedIndexCache.get(indexStr) }
		}

		const parsed = JSON.parse(indexStr)
		parsedIndexCache.set(indexStr, parsed)
		return { indexStr, index: parsed }
	}

	const decodeSyncActionValue = (value: unknown): proto.ISyncActionValue => {
		if (value instanceof Uint8Array || Buffer.isBuffer(value)) {
			return proto.SyncActionValue.decode(value)
		}

		return proto.SyncActionValue.fromObject(value as proto.ISyncActionValue)
	}

	if (!msgMutations.length) {
		return ltGenerator.finish()
	}

	const fastMutations: NativeSyncdMutationFastInput[] = []
	const keyMap: Record<string, NativeDerivedMutationKey> = {}

	for (const msgMutation of msgMutations) {
		const operation = 'operation' in msgMutation ? msgMutation.operation : proto.SyncdMutation.SyncdOperation.SET
		const record =
			'record' in msgMutation && !!msgMutation.record ? msgMutation.record : (msgMutation as proto.ISyncdRecord)

		const keyId = record.keyId!.id!
		const base64Key = Buffer.from(keyId).toString('base64')
		const key = await getKey(keyId)
		keyMap[base64Key] = key
		fastMutations.push({
			operation: operation!,
			indexMac: record.index!.blob!,
			valueBlob: record.value!.blob!,
			keyId
		})
	}

	const decodedMutations = nativeDecodeSyncdMutationsFastWire(fastMutations, keyMap, validateMacs)
	if (!Array.isArray(decodedMutations) || decodedMutations.length !== fastMutations.length) {
		throw new Boom('native decodeSyncdMutationsFastWire returned invalid payload', { statusCode: 500 })
	}

	const indexStrings = decodedMutations.map(mutation => {
		if (typeof mutation?.syncAction?.indexString === 'string') {
			return mutation.syncAction.indexString
		}

		return Buffer.from(mutation.syncAction.index).toString()
	})
	hydrateParsedIndexCache(indexStrings)

	for (const mutation of decodedMutations) {
		const syncAction: proto.ISyncActionData = {
			index: mutation.syncAction.index,
			value: decodeSyncActionValue(mutation.syncAction.value),
			padding: mutation.syncAction.padding ?? new Uint8Array(0),
			version: typeof mutation.syncAction.version === 'number' ? mutation.syncAction.version : undefined
		}

		const { index } = parseIndex(syncAction.index!, mutation.syncAction.indexString)
		onMutation({ syncAction, index })

		ltGenerator.mix({
			indexMac: mutation.indexMac,
			valueMac: mutation.valueMac,
			operation: mutation.operation as proto.SyncdMutation.SyncdOperation
		})
	}

	return ltGenerator.finish()

	async function getKey(keyId: Uint8Array) {
		const base64Key = Buffer.from(keyId).toString('base64')

		const cached = derivedKeyCache.get(base64Key)
		if (cached) {
			return cached
		}

		const keyEnc = await getAppStateSyncKey(base64Key)
		if (!keyEnc) {
			throw new Boom(`failed to find key "${base64Key}" to decode mutation`, {
				statusCode: 404,
				data: { msgMutations }
			})
		}

		const keys = mutationKeys(keyEnc.keyData!)
		derivedKeyCache.set(base64Key, keys)
		return keys
	}
}

export const decodeSyncdPatch = async (
	msg: proto.ISyncdPatch,
	name: WAPatchName,
	initialState: LTHashState,
	getAppStateSyncKey: FetchAppStateSyncKey,
	onMutation: (mutation: ChatMutation) => void,
	validateMacs: boolean
) => {
	if (validateMacs) {
		const base64Key = Buffer.from(msg.keyId!.id!).toString('base64')
		const mainKeyObj = await getAppStateSyncKey(base64Key)
		if (!mainKeyObj) {
			throw new Boom(`failed to find key "${base64Key}" to decode patch`, { statusCode: 404, data: { msg } })
		}

		const mainKey = mutationKeys(mainKeyObj.keyData!)
		const mutationmacs = msg.mutations!.map(mutation => mutation.record!.value!.blob!.slice(-32))

		const patchMac = generatePatchMac(
			msg.snapshotMac!,
			mutationmacs,
			toNumber(msg.version!.version),
			name,
			mainKey.patchMacKey
		)
		if (Buffer.compare(patchMac, msg.patchMac!) !== 0) {
			throw new Boom('Invalid patch mac')
		}
	}

	const result = await decodeSyncdMutations(msg.mutations!, initialState, getAppStateSyncKey, onMutation, validateMacs)
	return result
}

export const extractSyncdPatches = async (result: BinaryNode, options: RequestInit) => {
	const syncNode = getBinaryNodeChild(result, 'sync')
	const collectionNodes = getBinaryNodeChildren(syncNode, 'collection')

	const final = {} as {
		[T in WAPatchName]: { patches: proto.ISyncdPatch[]; hasMorePatches: boolean; snapshot?: proto.ISyncdSnapshot }
	}
	await Promise.all(
		collectionNodes.map(async collectionNode => {
			const patchesNode = getBinaryNodeChild(collectionNode, 'patches')

			const patches = getBinaryNodeChildren(patchesNode || collectionNode, 'patch')
			const snapshotNode = getBinaryNodeChild(collectionNode, 'snapshot')

			const syncds: proto.ISyncdPatch[] = []
			const name = collectionNode.attrs.name as WAPatchName

			const hasMorePatches = collectionNode.attrs.has_more_patches === 'true'

			let snapshot: proto.ISyncdSnapshot | undefined = undefined
			if (snapshotNode && !!snapshotNode.content) {
				if (!Buffer.isBuffer(snapshotNode.content)) {
					snapshotNode.content = Buffer.from(Object.values(snapshotNode.content))
				}

				const blobRef = proto.ExternalBlobReference.decode(snapshotNode.content as Buffer)
				const data = await downloadExternalBlob(blobRef, options)
				snapshot = decodeSyncdSnapshotWire(data)
			}

			for (let { content } of patches) {
				if (content) {
					if (!Buffer.isBuffer(content)) {
						content = Buffer.from(Object.values(content))
					}

					const syncd = decodeSyncdPatchWire(content as Uint8Array)
					if (!syncd.version) {
						syncd.version = { version: +collectionNode.attrs.version! + 1 }
					}

					syncds.push(syncd)
				}
			}

			final[name] = { patches: syncds, hasMorePatches, snapshot }
		})
	)

	return final
}

export const downloadExternalBlob = async (blob: proto.IExternalBlobReference, options: RequestInit) => {
	const stream = await downloadContentFromMessage(blob, 'md-app-state', { options })
	return toBuffer(stream)
}

export const downloadExternalPatch = async (blob: proto.IExternalBlobReference, options: RequestInit) => {
	const buffer = await downloadExternalBlob(blob, options)
	const syncData = decodeSyncdMutationsWire(buffer)
	return syncData
}

export const decodeSyncdSnapshot = async (
	name: WAPatchName,
	snapshot: proto.ISyncdSnapshot,
	getAppStateSyncKey: FetchAppStateSyncKey,
	minimumVersionNumber: number | undefined,
	validateMacs = true
) => {
	const newState = newLTHashState()
	newState.version = toNumber(snapshot.version!.version)

	const mutationMap: ChatMutationMap = {}
	const areMutationsRequired = typeof minimumVersionNumber === 'undefined' || newState.version > minimumVersionNumber

	const { hash, indexValueMap } = await decodeSyncdMutations(
		snapshot.records!,
		newState,
		getAppStateSyncKey,
		areMutationsRequired
			? mutation => {
					const index = mutation.syncAction.index?.toString()
					mutationMap[index!] = mutation
				}
			: () => {},
		validateMacs
	)
	newState.hash = hash
	newState.indexValueMap = indexValueMap

	if (validateMacs) {
		const base64Key = Buffer.from(snapshot.keyId!.id!).toString('base64')
		const keyEnc = await getAppStateSyncKey(base64Key)
		if (!keyEnc) {
			throw new Boom(`failed to find key "${base64Key}" to decode mutation`)
		}

		const result = mutationKeys(keyEnc.keyData!)
		const computedSnapshotMac = generateSnapshotMac(newState.hash, newState.version, name, result.snapshotMacKey)
		if (Buffer.compare(snapshot.mac!, computedSnapshotMac) !== 0) {
			throw new Boom(`failed to verify LTHash at ${newState.version} of ${name} from snapshot`)
		}
	}

	return {
		state: newState,
		mutationMap
	}
}

export const decodePatches = async (
	name: WAPatchName,
	syncds: proto.ISyncdPatch[],
	initial: LTHashState,
	getAppStateSyncKey: FetchAppStateSyncKey,
	options: RequestInit,
	minimumVersionNumber?: number,
	logger?: ILogger,
	validateMacs = true
) => {
	const newState: LTHashState = {
		...initial,
		indexValueMap: { ...initial.indexValueMap }
	}

	const mutationMap: ChatMutationMap = {}

	for (const syncd of syncds) {
		const { version, keyId, snapshotMac } = syncd
		if (syncd.externalMutations) {
			logger?.trace({ name, version }, 'downloading external patch')
			const ref = await downloadExternalPatch(syncd.externalMutations, options)
			const externalMutations = ref.mutations || []
			logger?.debug({ name, version, mutations: externalMutations.length }, 'downloaded external patch')
			if (!syncd.mutations) {
				syncd.mutations = []
			}

			syncd.mutations.push(...externalMutations)
		}

		const patchVersion = toNumber(version!.version)

		newState.version = patchVersion
		const shouldMutate = typeof minimumVersionNumber === 'undefined' || patchVersion > minimumVersionNumber

		const decodeResult = await decodeSyncdPatch(
			syncd,
			name,
			newState,
			getAppStateSyncKey,
			shouldMutate
				? mutation => {
						const index = mutation.syncAction.index?.toString()
						mutationMap[index!] = mutation
					}
				: () => {},
			true
		)

		newState.hash = decodeResult.hash
		newState.indexValueMap = decodeResult.indexValueMap

		if (validateMacs) {
			const base64Key = Buffer.from(keyId!.id!).toString('base64')
			const keyEnc = await getAppStateSyncKey(base64Key)
			if (!keyEnc) {
				throw new Boom(`failed to find key "${base64Key}" to decode mutation`)
			}

			const result = mutationKeys(keyEnc.keyData!)
			const computedSnapshotMac = generateSnapshotMac(newState.hash, newState.version, name, result.snapshotMacKey)
			if (Buffer.compare(snapshotMac!, computedSnapshotMac) !== 0) {
				throw new Boom(`failed to verify LTHash at ${newState.version} of ${name}`)
			}
		}

		// clear memory used up by the mutations
		syncd.mutations = []
	}

	return { state: newState, mutationMap }
}

export const chatModificationToAppPatch = (mod: ChatModification, jid: string) => {
	const OP = proto.SyncdMutation.SyncdOperation
	const getMessageRange = (lastMessages: LastMessageList) => {
		let messageRange: proto.SyncActionValue.ISyncActionMessageRange
		if (Array.isArray(lastMessages)) {
			const lastMsg = lastMessages[lastMessages.length - 1]
			messageRange = {
				lastMessageTimestamp: lastMsg?.messageTimestamp,
				messages: lastMessages?.length
					? lastMessages.map(m => {
							if (!m.key?.id || !m.key?.remoteJid) {
								throw new Boom('Incomplete key', { statusCode: 400, data: m })
							}

							if (isJidGroup(m.key.remoteJid) && !m.key.fromMe && !m.key.participant) {
								throw new Boom('Expected not from me message to have participant', { statusCode: 400, data: m })
							}

							if (!m.messageTimestamp || !toNumber(m.messageTimestamp)) {
								throw new Boom('Missing timestamp in last message list', { statusCode: 400, data: m })
							}

							if (m.key.participant) {
								m.key.participant = jidNormalizedUser(m.key.participant)
							}

							return m
						})
					: undefined
			}
		} else {
			messageRange = lastMessages
		}

		return messageRange
	}

	let patch: WAPatchCreate
	if ('mute' in mod) {
		patch = {
			syncAction: {
				muteAction: {
					muted: !!mod.mute,
					muteEndTimestamp: mod.mute || undefined
				}
			},
			index: ['mute', jid],
			type: 'regular_high',
			apiVersion: 2,
			operation: OP.SET
		}
	} else if ('archive' in mod) {
		patch = {
			syncAction: {
				archiveChatAction: {
					archived: !!mod.archive,
					messageRange: getMessageRange(mod.lastMessages)
				}
			},
			index: ['archive', jid],
			type: 'regular_low',
			apiVersion: 3,
			operation: OP.SET
		}
	} else if ('markRead' in mod) {
		patch = {
			syncAction: {
				markChatAsReadAction: {
					read: mod.markRead,
					messageRange: getMessageRange(mod.lastMessages)
				}
			},
			index: ['markChatAsRead', jid],
			type: 'regular_low',
			apiVersion: 3,
			operation: OP.SET
		}
	} else if ('deleteForMe' in mod) {
		const { timestamp, key, deleteMedia } = mod.deleteForMe
		patch = {
			syncAction: {
				deleteMessageForMeAction: {
					deleteMedia,
					messageTimestamp: timestamp
				}
			},
			index: ['deleteMessageForMe', jid, key.id!, key.fromMe ? '1' : '0', '0'],
			type: 'regular_high',
			apiVersion: 3,
			operation: OP.SET
		}
	} else if ('clear' in mod) {
		patch = {
			syncAction: {
				clearChatAction: {
					messageRange: getMessageRange(mod.lastMessages)
				}
			},
			index: ['clearChat', jid, '1' /*the option here is 0 when keep starred messages is enabled*/, '0'],
			type: 'regular_high',
			apiVersion: 6,
			operation: OP.SET
		}
	} else if ('pin' in mod) {
		patch = {
			syncAction: {
				pinAction: {
					pinned: !!mod.pin
				}
			},
			index: ['pin_v1', jid],
			type: 'regular_low',
			apiVersion: 5,
			operation: OP.SET
		}
	} else if ('contact' in mod) {
		patch = {
			syncAction: {
				contactAction: mod.contact || {}
			},
			index: ['contact', jid],
			type: 'critical_unblock_low',
			apiVersion: 2,
			operation: mod.contact ? OP.SET : OP.REMOVE
		}
	} else if ('disableLinkPreviews' in mod) {
		patch = {
			syncAction: {
				privacySettingDisableLinkPreviewsAction: mod.disableLinkPreviews || {}
			},
			index: ['setting_disableLinkPreviews'],
			type: 'regular',
			apiVersion: 8,
			operation: OP.SET
		}
	} else if ('star' in mod) {
		const key = mod.star.messages[0]!
		patch = {
			syncAction: {
				starAction: {
					starred: !!mod.star.star
				}
			},
			index: ['star', jid, key.id, key.fromMe ? '1' : '0', '0'],
			type: 'regular_low',
			apiVersion: 2,
			operation: OP.SET
		}
	} else if ('delete' in mod) {
		patch = {
			syncAction: {
				deleteChatAction: {
					messageRange: getMessageRange(mod.lastMessages)
				}
			},
			index: ['deleteChat', jid, '1'],
			type: 'regular_high',
			apiVersion: 6,
			operation: OP.SET
		}
	} else if ('pushNameSetting' in mod) {
		patch = {
			syncAction: {
				pushNameSetting: {
					name: mod.pushNameSetting
				}
			},
			index: ['setting_pushName'],
			type: 'critical_block',
			apiVersion: 1,
			operation: OP.SET
		}
	} else if ('quickReply' in mod) {
		patch = {
			syncAction: {
				quickReplyAction: {
					count: 0,
					deleted: mod.quickReply.deleted || false,
					keywords: [],
					message: mod.quickReply.message || '',
					shortcut: mod.quickReply.shortcut || ''
				}
			},
			index: ['quick_reply', mod.quickReply.timestamp || String(Math.floor(Date.now() / 1000))],
			type: 'regular',
			apiVersion: 2,
			operation: OP.SET
		}
	} else if ('addLabel' in mod) {
		patch = {
			syncAction: {
				labelEditAction: {
					name: mod.addLabel.name,
					color: mod.addLabel.color,
					predefinedId: mod.addLabel.predefinedId,
					deleted: mod.addLabel.deleted
				}
			},
			index: ['label_edit', mod.addLabel.id],
			type: 'regular',
			apiVersion: 3,
			operation: OP.SET
		}
	} else if ('addChatLabel' in mod) {
		patch = {
			syncAction: {
				labelAssociationAction: {
					labeled: true
				}
			},
			index: [LabelAssociationType.Chat, mod.addChatLabel.labelId, jid],
			type: 'regular',
			apiVersion: 3,
			operation: OP.SET
		}
	} else if ('removeChatLabel' in mod) {
		patch = {
			syncAction: {
				labelAssociationAction: {
					labeled: false
				}
			},
			index: [LabelAssociationType.Chat, mod.removeChatLabel.labelId, jid],
			type: 'regular',
			apiVersion: 3,
			operation: OP.SET
		}
	} else if ('addMessageLabel' in mod) {
		patch = {
			syncAction: {
				labelAssociationAction: {
					labeled: true
				}
			},
			index: [LabelAssociationType.Message, mod.addMessageLabel.labelId, jid, mod.addMessageLabel.messageId, '0', '0'],
			type: 'regular',
			apiVersion: 3,
			operation: OP.SET
		}
	} else if ('removeMessageLabel' in mod) {
		patch = {
			syncAction: {
				labelAssociationAction: {
					labeled: false
				}
			},
			index: [
				LabelAssociationType.Message,
				mod.removeMessageLabel.labelId,
				jid,
				mod.removeMessageLabel.messageId,
				'0',
				'0'
			],
			type: 'regular',
			apiVersion: 3,
			operation: OP.SET
		}
	} else {
		throw new Boom('not supported')
	}

	patch.syncAction.timestamp = Date.now()

	return patch
}

export const processSyncAction = (
	syncAction: ChatMutation,
	ev: BaileysEventEmitter,
	me: Contact,
	initialSyncOpts?: InitialAppStateSyncOptions,
	logger?: ILogger
) => {
	const isInitialSync = !!initialSyncOpts
	const accountSettings = initialSyncOpts?.accountSettings

	logger?.trace({ syncAction, initialSync: !!initialSyncOpts }, 'processing sync action')

	const {
		syncAction: { value: action },
		index: [type, id, msgId, fromMe]
	} = syncAction

	if (action?.muteAction) {
		ev.emit('chats.update', [
			{
				id,
				muteEndTime: action.muteAction?.muted ? toNumber(action.muteAction.muteEndTimestamp) : null,
				conditional: getChatUpdateConditional(id!, undefined)
			}
		])
	} else if (action?.archiveChatAction || type === 'archive' || type === 'unarchive') {
		// okay so we've to do some annoying computation here
		// when we're initially syncing the app state
		// there are a few cases we need to handle
		// 1. if the account unarchiveChats setting is true
		//   a. if the chat is archived, and no further messages have been received -- simple, keep archived
		//   b. if the chat was archived, and the user received messages from the other person afterwards
		//		then the chat should be marked unarchved --
		//		we compare the timestamp of latest message from the other person to determine this
		// 2. if the account unarchiveChats setting is false -- then it doesn't matter,
		//	it'll always take an app state action to mark in unarchived -- which we'll get anyway
		const archiveAction = action?.archiveChatAction
		const isArchived = archiveAction ? archiveAction.archived : type === 'archive'
		// // basically we don't need to fire an "archive" update if the chat is being marked unarchvied
		// // this only applies for the initial sync
		// if(isInitialSync && !isArchived) {
		// 	isArchived = false
		// }

		const msgRange = !accountSettings?.unarchiveChats ? undefined : archiveAction?.messageRange
		// logger?.debug({ chat: id, syncAction }, 'message range archive')

		ev.emit('chats.update', [
			{
				id,
				archived: isArchived,
				conditional: getChatUpdateConditional(id!, msgRange)
			}
		])
	} else if (action?.markChatAsReadAction) {
		const markReadAction = action.markChatAsReadAction
		// basically we don't need to fire an "read" update if the chat is being marked as read
		// because the chat is read by default
		// this only applies for the initial sync
		const isNullUpdate = isInitialSync && markReadAction.read

		ev.emit('chats.update', [
			{
				id,
				unreadCount: isNullUpdate ? null : !!markReadAction?.read ? 0 : -1,
				conditional: getChatUpdateConditional(id!, markReadAction?.messageRange)
			}
		])
	} else if (action?.deleteMessageForMeAction || type === 'deleteMessageForMe') {
		ev.emit('messages.delete', {
			keys: [
				{
					remoteJid: id,
					id: msgId,
					fromMe: fromMe === '1'
				}
			]
		})
	} else if (action?.contactAction) {
		const results = processContactAction(action.contactAction, id, logger)
		emitSyncActionResults(ev, results)
	} else if (action?.pushNameSetting) {
		const name = action?.pushNameSetting?.name
		if (name && me?.name !== name) {
			ev.emit('creds.update', { me: { ...me, name } })
		}
	} else if (action?.pinAction) {
		ev.emit('chats.update', [
			{
				id,
				pinned: action.pinAction?.pinned ? toNumber(action.timestamp) : null,
				conditional: getChatUpdateConditional(id!, undefined)
			}
		])
	} else if (action?.unarchiveChatsSetting) {
		const unarchiveChats = !!action.unarchiveChatsSetting.unarchiveChats
		ev.emit('creds.update', { accountSettings: { unarchiveChats } })

		logger?.info(`archive setting updated => '${action.unarchiveChatsSetting.unarchiveChats}'`)
		if (accountSettings) {
			accountSettings.unarchiveChats = unarchiveChats
		}
	} else if (action?.starAction || type === 'star') {
		let starred = action?.starAction?.starred
		if (typeof starred !== 'boolean') {
			starred = syncAction.index[syncAction.index.length - 1] === '1'
		}

		ev.emit('messages.update', [
			{
				key: { remoteJid: id, id: msgId, fromMe: fromMe === '1' },
				update: { starred }
			}
		])
	} else if (action?.deleteChatAction || type === 'deleteChat') {
		if (!isInitialSync) {
			ev.emit('chats.delete', [id!])
		}
	} else if (action?.labelEditAction) {
		const { name, color, deleted, predefinedId } = action.labelEditAction

		ev.emit('labels.edit', {
			id: id!,
			name: name!,
			color: color!,
			deleted: deleted!,
			predefinedId: predefinedId ? String(predefinedId) : undefined
		})
	} else if (action?.labelAssociationAction) {
		ev.emit('labels.association', {
			type: action.labelAssociationAction.labeled ? 'add' : 'remove',
			association:
				type === LabelAssociationType.Chat
					? ({
							type: LabelAssociationType.Chat,
							chatId: syncAction.index[2],
							labelId: syncAction.index[1]
						} as ChatLabelAssociation)
					: ({
							type: LabelAssociationType.Message,
							chatId: syncAction.index[2],
							messageId: syncAction.index[3],
							labelId: syncAction.index[1]
						} as MessageLabelAssociation)
		})
	} else if (action?.localeSetting?.locale) {
		ev.emit('settings.update', { setting: 'locale', value: action.localeSetting.locale })
	} else if (action?.timeFormatAction) {
		ev.emit('settings.update', { setting: 'timeFormat', value: action.timeFormatAction })
	} else if (action?.pnForLidChatAction) {
		if (action.pnForLidChatAction.pnJid) {
			ev.emit('lid-mapping.update', { lid: id!, pn: action.pnForLidChatAction.pnJid })
		}
	} else if (action?.privacySettingRelayAllCalls) {
		ev.emit('settings.update', {
			setting: 'privacySettingRelayAllCalls',
			value: action.privacySettingRelayAllCalls
		})
	} else if (action?.statusPrivacy) {
		ev.emit('settings.update', { setting: 'statusPrivacy', value: action.statusPrivacy })
	} else if (action?.lockChatAction) {
		ev.emit('chats.lock', { id: id!, locked: !!action.lockChatAction.locked })
	} else if (action?.privacySettingDisableLinkPreviewsAction) {
		ev.emit('settings.update', {
			setting: 'disableLinkPreviews',
			value: action.privacySettingDisableLinkPreviewsAction
		})
	} else if (action?.notificationActivitySettingAction?.notificationActivitySetting) {
		ev.emit('settings.update', {
			setting: 'notificationActivitySetting',
			value: action.notificationActivitySettingAction.notificationActivitySetting
		})
	} else if (action?.lidContactAction) {
		ev.emit('contacts.upsert', [
			{
				id: id!,
				name:
					action.lidContactAction.fullName ||
					action.lidContactAction.firstName ||
					action.lidContactAction.username ||
					undefined,
				lid: id!,
				phoneNumber: undefined
			}
		])
	} else if (action?.privacySettingChannelsPersonalisedRecommendationAction) {
		ev.emit('settings.update', {
			setting: 'channelsPersonalisedRecommendation',
			value: action.privacySettingChannelsPersonalisedRecommendationAction
		})
	} else {
		logger?.debug({ syncAction, id }, 'unprocessable update')
	}

	function getChatUpdateConditional(
		id: string,
		msgRange: proto.SyncActionValue.ISyncActionMessageRange | null | undefined
	): ChatUpdate['conditional'] {
		return isInitialSync
			? data => {
					const chat = data.historySets.chats[id] || data.chatUpserts[id]
					if (chat) {
						return msgRange ? isValidPatchBasedOnMessageRange(chat, msgRange) : true
					}
				}
			: undefined
	}

	function isValidPatchBasedOnMessageRange(
		chat: Chat,
		msgRange: proto.SyncActionValue.ISyncActionMessageRange | null | undefined
	) {
		const lastMsgTimestamp = Number(msgRange?.lastMessageTimestamp || msgRange?.lastSystemMessageTimestamp || 0)
		const chatLastMsgTimestamp = Number(chat?.lastMessageRecvTimestamp || 0)
		return lastMsgTimestamp >= chatLastMsgTimestamp
	}
}
