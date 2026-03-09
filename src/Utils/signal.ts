import { KEY_BUNDLE_TYPE } from '../Defaults'
import { requireNativeExport } from '../Native/baileys-native'
import type { SignalRepositoryWithLIDStore } from '../Types'
import type {
	AuthenticationCreds,
	AuthenticationState,
	KeyPair,
	SignalIdentity,
	SignalKeyStore,
	SignedKeyPair
} from '../Types/Auth'
import {
	assertNodeErrorFree,
	type BinaryNode,
	type FullJid,
	getBinaryNodeChild,
	getBinaryNodeChildren,
	S_WHATSAPP_NET
} from '../WABinary'
import type { USyncQueryResultList } from '../WAUSync'
import { Curve, generateSignalPubKey } from './crypto'
import { encodeBigEndian } from './generics'

const nativeExtractE2ESessionBundlesFast = requireNativeExport('extractE2ESessionBundlesFast')
const nativeExtractDeviceJidsFast = requireNativeExport('extractDeviceJidsFast')

function chunk<T>(array: T[], size: number): T[][] {
	const chunks: T[][] = []
	for (let i = 0; i < array.length; i += size) {
		chunks.push(array.slice(i, i + size))
	}

	return chunks
}

export const createSignalIdentity = (wid: string, accountSignatureKey: Uint8Array): SignalIdentity => {
	return {
		identifier: { name: wid, deviceId: 0 },
		identifierKey: generateSignalPubKey(accountSignatureKey)
	}
}

export const getPreKeys = async ({ get }: SignalKeyStore, min: number, limit: number) => {
	const idList: string[] = []
	for (let id = min; id < limit; id++) {
		idList.push(id.toString())
	}

	return get('pre-key', idList)
}

export const generateOrGetPreKeys = (creds: AuthenticationCreds, range: number) => {
	const avaliable = creds.nextPreKeyId - creds.firstUnuploadedPreKeyId
	const remaining = range - avaliable
	const lastPreKeyId = creds.nextPreKeyId + remaining - 1
	const newPreKeys: { [id: number]: KeyPair } = {}
	if (remaining > 0) {
		for (let i = creds.nextPreKeyId; i <= lastPreKeyId; i++) {
			newPreKeys[i] = Curve.generateKeyPair()
		}
	}

	return {
		newPreKeys,
		lastPreKeyId,
		preKeysRange: [creds.firstUnuploadedPreKeyId, range] as const
	}
}

export const xmppSignedPreKey = (key: SignedKeyPair): BinaryNode => ({
	tag: 'skey',
	attrs: {},
	content: [
		{ tag: 'id', attrs: {}, content: encodeBigEndian(key.keyId, 3) },
		{ tag: 'value', attrs: {}, content: key.keyPair.public },
		{ tag: 'signature', attrs: {}, content: key.signature }
	]
})

export const xmppPreKey = (pair: KeyPair, id: number): BinaryNode => ({
	tag: 'key',
	attrs: {},
	content: [
		{ tag: 'id', attrs: {}, content: encodeBigEndian(id, 3) },
		{ tag: 'value', attrs: {}, content: pair.public }
	]
})

export const parseAndInjectE2ESessions = async (node: BinaryNode, repository: SignalRepositoryWithLIDStore) => {
	const userNodes = getBinaryNodeChildren(getBinaryNodeChild(node, 'list'), 'user')
	for (const userNode of userNodes) {
		assertNodeErrorFree(userNode)
	}

	// Extract bundles in one native pass, then inject them in chunks to avoid monopolizing the event loop.
	const sessions = userNodes.length === 0 ? [] : nativeExtractE2ESessionBundlesFast(userNodes as unknown[])
	if (!Array.isArray(sessions) || sessions.length !== userNodes.length) {
		throw new Error('native extractE2ESessionBundlesFast returned invalid payload')
	}

	// Most of the work in repository.injectE2ESession is CPU intensive, not IO
	// So Promise.all doesn't really help here,
	// but blocks even loop if we're using it inside keys.transaction, and it makes it "sync" actually
	// This way we chunk it in smaller parts and between those parts we can yield to the event loop
	// It's rare case when you need to E2E sessions for so many users, but it's possible
	const chunkSize = 100
	const chunks = chunk(sessions, chunkSize)
	if (!repository.injectE2ESessions) {
		throw new Error('strict native mode requires injectE2ESessions')
	}

	for (const sessionsChunk of chunks) {
		await repository.injectE2ESessions(sessionsChunk)
	}
}

export const extractDeviceJids = (
	result: USyncQueryResultList[],
	myJid: string,
	myLid: string,
	excludeZeroDevices: boolean
) => {
	const nativeResult = nativeExtractDeviceJidsFast(result as unknown[], myJid, myLid, excludeZeroDevices)
	if (!Array.isArray(nativeResult)) {
		throw new Error('native extractDeviceJidsFast returned invalid payload')
	}

	return nativeResult as FullJid[]
}

/**
 * get the next N keys for upload or processing
 * @param count number of pre-keys to get or generate
 */
export const getNextPreKeys = async ({ creds, keys }: AuthenticationState, count: number) => {
	const { newPreKeys, lastPreKeyId, preKeysRange } = generateOrGetPreKeys(creds, count)

	const update: Partial<AuthenticationCreds> = {
		nextPreKeyId: Math.max(lastPreKeyId + 1, creds.nextPreKeyId),
		firstUnuploadedPreKeyId: Math.max(creds.firstUnuploadedPreKeyId, lastPreKeyId + 1)
	}

	await keys.set({ 'pre-key': newPreKeys })

	const preKeys = await getPreKeys(keys, preKeysRange[0], preKeysRange[0] + preKeysRange[1])

	return { update, preKeys }
}

export const getNextPreKeysNode = async (state: AuthenticationState, count: number) => {
	const { creds } = state
	const { update, preKeys } = await getNextPreKeys(state, count)

	const node: BinaryNode = {
		tag: 'iq',
		attrs: {
			xmlns: 'encrypt',
			type: 'set',
			to: S_WHATSAPP_NET
		},
		content: [
			{ tag: 'registration', attrs: {}, content: encodeBigEndian(creds.registrationId) },
			{ tag: 'type', attrs: {}, content: KEY_BUNDLE_TYPE },
			{ tag: 'identity', attrs: {}, content: creds.signedIdentityKey.public },
			{ tag: 'list', attrs: {}, content: Object.keys(preKeys).map(k => xmppPreKey(preKeys[+k]!, +k)) },
			xmppSignedPreKey(creds.signedPreKey)
		]
	}

	return { update, node }
}
