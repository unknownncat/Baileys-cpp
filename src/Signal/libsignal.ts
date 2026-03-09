// @ts-ignore
import * as libsignal from 'libsignal'
// @ts-ignore
const { PreKeyWhisperMessage } = libsignal.protobuf
import { LRUCache } from 'lru-cache'
import { requireNativeExport } from '../Native/baileys-native'
import type { LIDMapping, SignalAuthState, SignalKeyStoreWithTransaction } from '../Types'
import type { SignalRepositoryWithLIDStore } from '../Types/Signal'
import { generateSignalPubKey } from '../Utils'
import type { ILogger } from '../Utils/logger'
import {
	isHostedLidUser,
	isHostedPnUser,
	isLidUser,
	isPnUser,
	jidDecode,
	transferDevice,
	WAJIDDomains
} from '../WABinary'
import { MissingSenderKeySessionError, type SenderKeyStore } from './Group/group_cipher'
import { SenderKeyName } from './Group/sender-key-name'
import { SenderKeyRecord } from './Group/sender-key-record'
import { GroupCipher, GroupSessionBuilder, SenderKeyDistributionMessage } from './Group'
import { LIDMappingStore } from './lid-mapping'

const nativeDedupeStringListFast = requireNativeExport('dedupeStringListFast')
const nativeResolveSignalAddressFast = requireNativeExport('resolveSignalAddressFast')

/** Extract identity key from PreKeyWhisperMessage for identity change detection */
function extractIdentityFromPkmsg(ciphertext: Uint8Array): Uint8Array | undefined {
	try {
		if (!ciphertext || ciphertext.length < 2) {
			return undefined
		}

		// Version byte check (version 3)
		const version = ciphertext[0]!
		if ((version & 0xf) !== 3) {
			return undefined
		}

		// Parse protobuf (skip version byte)
		const preKeyProto = PreKeyWhisperMessage.decode(ciphertext.slice(1))
		if (preKeyProto.identityKey?.length === 33) {
			return new Uint8Array(preKeyProto.identityKey)
		}

		return undefined
	} catch {
		return undefined
	}
}

type PendingGroupCiphertextRecovery = {
	group: string
	authorJid: string
	senderNameStr: string
	ciphertext: Uint8Array
	senderKeyId: number
	senderKeyIteration: number
	firstSeenAt: number
	lastSeenAt: number
	attempts: number
	lastError?: string
}

type GroupCipherHandle = {
	resolvedAuthorJid: string
	senderName: SenderKeyName
	senderNameStr: string
	cipher: GroupCipher
}

type SessionCipherHandle = {
	resolvedJid: string
	addr: libsignal.ProtocolAddress
	addrStr: string
	cipher: libsignal.SessionCipher
}

const GROUP_PENDING_RECOVERY_TTL_MS = 30 * 60 * 1000
const GROUP_PENDING_RECOVERY_MAX_ATTEMPTS = 5
const isSessionBadMacError = (message: string) => {
	const normalized = message.toLowerCase()
	return normalized.includes('bad mac') || normalized.includes('invalidmessageexception')
}

export function makeLibSignalRepository(
	auth: SignalAuthState,
	logger: ILogger,
	pnToLIDFunc?: (jids: string[]) => Promise<LIDMapping[] | undefined>
): SignalRepositoryWithLIDStore {
	const lidMapping = new LIDMappingStore(auth.keys as SignalKeyStoreWithTransaction, logger, pnToLIDFunc)
	const storage = signalStorage(auth, lidMapping)

	const parsedKeys = auth.keys as SignalKeyStoreWithTransaction
	const migratedSessionCache = new LRUCache<string, true>({
		ttl: 3 * 24 * 60 * 60 * 1000, // 7 days
		ttlAutopurge: true,
		updateAgeOnGet: true
	})
	const sessionCipherCache = new LRUCache<string, libsignal.SessionCipher>({
		max: 4096,
		ttl: 10 * 60 * 1000,
		ttlAutopurge: true,
		updateAgeOnGet: true
	})
	const sessionBuilderCache = new LRUCache<string, libsignal.SessionBuilder>({
		max: 2048,
		ttl: 10 * 60 * 1000,
		ttlAutopurge: true,
		updateAgeOnGet: true
	})
	const groupCipherCache = new LRUCache<string, GroupCipher>({
		max: 4096,
		ttl: 10 * 60 * 1000,
		ttlAutopurge: true,
		updateAgeOnGet: true
	})
	const pendingGroupCiphertextRecoveryCache = new LRUCache<string, PendingGroupCiphertextRecovery>({
		max: 4096,
		ttl: GROUP_PENDING_RECOVERY_TTL_MS,
		ttlAutopurge: true,
		updateAgeOnGet: true
	})
	const recoveredGroupPlaintextCache = new LRUCache<string, Uint8Array>({
		max: 4096,
		ttl: GROUP_PENDING_RECOVERY_TTL_MS,
		ttlAutopurge: true,
		updateAgeOnGet: true
	})
	const clearCachedSessionState = (addresses: Iterable<string>) => {
		for (const address of addresses) {
			sessionCipherCache.delete(address)
			sessionBuilderCache.delete(address)
		}
	}

	const getGroupCiphertextRecoveryKey = (senderNameStr: string, ciphertext: Uint8Array): string =>
		`${senderNameStr}:${Buffer.from(ciphertext).toString('base64')}`

	const getSessionCipher = (jid: string) => {
		const { addr, cipher } = getSessionCipherByJid(jid)
		return { addr, cipher }
	}

	const resolveCanonicalSessionJid = async (jid: string, jidAlt?: string): Promise<string> => {
		if (isLidLikeJid(jid)) {
			return jid
		}

		if (isPnLikeJid(jid)) {
			const mappedLid = await lidMapping.getLIDForPN(jid)
			if (mappedLid) {
				return mappedLid
			}
		}

		if (jidAlt && isLidLikeJid(jidAlt)) {
			return jidAlt
		}

		if (jidAlt && isPnLikeJid(jidAlt)) {
			const mappedLid = await lidMapping.getLIDForPN(jidAlt)
			if (mappedLid) {
				return mappedLid
			}
		}

		return jid
	}

	const resolveSessionJidCandidates = async (jid: string, jidAlt?: string): Promise<string[]> => {
		const out: string[] = []

		const canonical = await resolveCanonicalSessionJid(jid, jidAlt)
		out.push(canonical)
		out.push(jid)

		if (jidAlt) {
			out.push(jidAlt)
		}

		if (isPnLikeJid(jid)) {
			const mappedLid = await lidMapping.getLIDForPN(jid)
			if (mappedLid) {
				out.push(mappedLid)
			}
		}

		if (jidAlt && isPnLikeJid(jidAlt)) {
			const mappedLid = await lidMapping.getLIDForPN(jidAlt)
			if (mappedLid) {
				out.push(mappedLid)
			}
		}

		const deduped = nativeDedupeStringListFast(out.filter((item): item is string => !!item))
		if (!Array.isArray(deduped)) {
			throw new Error('native dedupeStringListFast returned invalid payload')
		}

		return deduped.filter((item): item is string => typeof item === 'string' && item.length > 0)
	}

	const getSessionCipherByJid = (jid: string): SessionCipherHandle => {
		const addr = jidToSignalProtocolAddress(jid)
		const addrStr = addr.toString()
		const cached = sessionCipherCache.get(addrStr)

		if (cached) {
			return {
				resolvedJid: jid,
				addr,
				addrStr,
				cipher: cached
			}
		}

		const cipher = new libsignal.SessionCipher(storage, addr)
		sessionCipherCache.set(addrStr, cipher)

		return {
			resolvedJid: jid,
			addr,
			addrStr,
			cipher
		}
	}

	const getSessionCipherCandidates = async (jid: string, jidAlt?: string): Promise<SessionCipherHandle[]> => {
		const candidates = await resolveSessionJidCandidates(jid, jidAlt)
		return candidates.map(candidate => getSessionCipherByJid(candidate))
	}

	const getSessionBuilder = (jid: string) => {
		const addr = jidToSignalProtocolAddress(jid)
		const cacheKey = addr.toString()
		const cached = sessionBuilderCache.get(cacheKey)
		if (cached) {
			return cached
		}

		const builder = new libsignal.SessionBuilder(storage, addr)
		sessionBuilderCache.set(cacheKey, builder)
		return builder
	}

	const isLidLikeJid = (jid?: string | null) => !!jid && (isLidUser(jid) || isHostedLidUser(jid))

	const isPnLikeJid = (jid?: string | null) => !!jid && (isPnUser(jid) || isHostedPnUser(jid))

	const resolveCanonicalGroupAuthorJid = async (authorJid: string, authorAltJid?: string): Promise<string> => {
		if (isLidLikeJid(authorJid)) {
			return authorJid
		}

		if (isPnLikeJid(authorJid)) {
			const mappedLid = await lidMapping.getLIDForPN(authorJid)
			if (mappedLid) {
				return mappedLid
			}
		}

		if (authorAltJid && isLidLikeJid(authorAltJid)) {
			return authorAltJid
		}

		if (authorAltJid && isPnLikeJid(authorAltJid)) {
			const mappedLid = await lidMapping.getLIDForPN(authorAltJid)
			if (mappedLid) {
				return mappedLid
			}
		}

		return authorJid
	}

	const resolveGroupAuthorCandidates = async (authorJid: string, authorAltJid?: string): Promise<string[]> => {
		const out: string[] = []

		const canonical = await resolveCanonicalGroupAuthorJid(authorJid, authorAltJid)
		out.push(canonical)
		out.push(authorJid)

		if (authorAltJid) {
			out.push(authorAltJid)
		}

		if (isPnLikeJid(authorJid)) {
			const mappedLid = await lidMapping.getLIDForPN(authorJid)
			if (mappedLid) {
				out.push(mappedLid)
			}
		}

		if (authorAltJid && isPnLikeJid(authorAltJid)) {
			const mappedLid = await lidMapping.getLIDForPN(authorAltJid)
			if (mappedLid) {
				out.push(mappedLid)
			}
		}

		const deduped = nativeDedupeStringListFast(out.filter((item): item is string => !!item))
		if (!Array.isArray(deduped)) {
			throw new Error('native dedupeStringListFast returned invalid payload')
		}

		return deduped.filter((item): item is string => typeof item === 'string' && item.length > 0)
	}

	const getGroupCipherByAuthor = (group: string, authorJid: string): GroupCipherHandle => {
		const senderName = jidToSignalSenderKeyName(group, authorJid)
		const senderNameStr = senderName.toString()
		const cached = groupCipherCache.get(senderNameStr)

		if (cached) {
			return {
				resolvedAuthorJid: authorJid,
				senderName,
				senderNameStr,
				cipher: cached
			}
		}

		const cipher = new GroupCipher(storage, senderName)
		groupCipherCache.set(senderNameStr, cipher)

		return {
			resolvedAuthorJid: authorJid,
			senderName,
			senderNameStr,
			cipher
		}
	}

	const getGroupCipherCandidates = async (
		group: string,
		authorJid: string,
		authorAltJid?: string
	): Promise<GroupCipherHandle[]> => {
		const candidates = await resolveGroupAuthorCandidates(authorJid, authorAltJid)
		return candidates.map(candidate => getGroupCipherByAuthor(group, candidate))
	}

	const queuePendingGroupCiphertextRecovery = (
		group: string,
		authorJid: string,
		senderNameStr: string,
		ciphertext: Uint8Array,
		error: MissingSenderKeySessionError
	) => {
		const cacheKey = getGroupCiphertextRecoveryKey(senderNameStr, ciphertext)
		const now = Date.now()
		const existing = pendingGroupCiphertextRecoveryCache.get(cacheKey)

		if (existing) {
			pendingGroupCiphertextRecoveryCache.set(cacheKey, {
				...existing,
				lastSeenAt: now,
				lastError: error.message
			})
			return
		}

		pendingGroupCiphertextRecoveryCache.set(cacheKey, {
			group,
			authorJid,
			senderNameStr,
			ciphertext,
			senderKeyId: error.senderKeyId,
			senderKeyIteration: error.senderKeyIteration,
			firstSeenAt: now,
			lastSeenAt: now,
			attempts: 0,
			lastError: error.message
		})

		logger.info(
			{ group, authorJid, senderKeyId: error.senderKeyId, senderKeyIteration: error.senderKeyIteration },
			'queued group ciphertext for sender-key recovery'
		)
	}

	const recoverPendingGroupCiphertexts = async (group: string, authorJid: string, senderNameStr: string) => {
		const pendingEntries = Array.from(pendingGroupCiphertextRecoveryCache.entries()).filter(
			([, entry]) => entry.senderNameStr === senderNameStr
		)
		if (!pendingEntries.length) {
			return
		}

		const { cipher } = getGroupCipherByAuthor(group, authorJid)
		let recoveredCount = 0
		let droppedCount = 0
		let failedCount = 0

		for (const [cacheKey, entry] of pendingEntries) {
			try {
				const plaintext = await cipher.decrypt(entry.ciphertext)
				recoveredGroupPlaintextCache.set(cacheKey, plaintext)
				pendingGroupCiphertextRecoveryCache.delete(cacheKey)
				recoveredCount += 1
			} catch (error: any) {
				const attempts = entry.attempts + 1
				if (attempts >= GROUP_PENDING_RECOVERY_MAX_ATTEMPTS) {
					pendingGroupCiphertextRecoveryCache.delete(cacheKey)
					droppedCount += 1
					continue
				}

				pendingGroupCiphertextRecoveryCache.set(cacheKey, {
					...entry,
					attempts,
					lastSeenAt: Date.now(),
					lastError: String(error?.message || error)
				})
				failedCount += 1
			}
		}

		logger.info(
			{
				group,
				authorJid,
				pending: pendingEntries.length,
				recovered: recoveredCount,
				failed: failedCount,
				dropped: droppedCount
			},
			'processed pending group ciphertext recovery queue'
		)
	}

	const repository: SignalRepositoryWithLIDStore = {
		async decryptGroupMessage({ group, authorJid, authorAltJid, msg }) {
			const handles = await getGroupCipherCandidates(group, authorJid, authorAltJid)

			for (const handle of handles) {
				const recoveryKey = getGroupCiphertextRecoveryKey(handle.senderNameStr, msg)
				const recovered = recoveredGroupPlaintextCache.get(recoveryKey)
				if (recovered) {
					return recovered
				}
			}

			return parsedKeys.transaction(async () => {
				let lastMissing: MissingSenderKeySessionError | undefined
				let lastError: unknown

				for (const handle of handles) {
					try {
						const plaintext = await handle.cipher.decrypt(msg)

						for (const aliasHandle of handles) {
							const aliasRecoveryKey = getGroupCiphertextRecoveryKey(aliasHandle.senderNameStr, msg)
							recoveredGroupPlaintextCache.set(aliasRecoveryKey, plaintext)
							pendingGroupCiphertextRecoveryCache.delete(aliasRecoveryKey)
						}

						return plaintext
					} catch (error: any) {
						lastError = error

						if (error instanceof MissingSenderKeySessionError) {
							queuePendingGroupCiphertextRecovery(group, handle.resolvedAuthorJid, handle.senderNameStr, msg, error)
							lastMissing = error
							continue
						}

						throw error
					}
				}

				throw lastMissing || lastError || new Error('Failed to decrypt group message for all author aliases')
			}, group)
		},
		async processSenderKeyDistributionMessage({ item, authorJid, authorAltJid }) {
			const builder = new GroupSessionBuilder(storage)

			if (!item.groupId) {
				throw new Error('Group ID is required for sender key distribution message')
			}

			const handles = await getGroupCipherCandidates(item.groupId, authorJid, authorAltJid)
			const primaryHandle = handles[0]
			if (!primaryHandle) {
				throw new Error('Could not resolve sender-key author aliases')
			}

			const senderMsg = new SenderKeyDistributionMessage(
				null,
				null,
				null,
				null,
				item.axolotlSenderKeyDistributionMessage
			)

			return parsedKeys.transaction(async () => {
				await builder.process(primaryHandle.senderName, senderMsg)

				const primaryRecord = await storage.loadSenderKey(primaryHandle.senderName)

				for (const handle of handles) {
					if (handle.senderNameStr === primaryHandle.senderNameStr) {
						continue
					}

					await storage.storeSenderKey(handle.senderName, primaryRecord)
				}

				for (const handle of handles) {
					await recoverPendingGroupCiphertexts(item.groupId!, handle.resolvedAuthorJid, handle.senderNameStr)
				}
			}, item.groupId)
		},

		async decryptMessage({ jid, jidAlt, type, ciphertext }) {
			const handles = await getSessionCipherCandidates(jid, jidAlt)
			const canonicalHandle = handles[0]!
			let lastError: unknown

			if (type === 'pkmsg') {
				const identityKey = extractIdentityFromPkmsg(ciphertext)
				if (identityKey) {
					const identityChanged = await storage.saveIdentity(canonicalHandle.addrStr, identityKey)
					if (identityChanged) {
						logger.info(
							{ jid: canonicalHandle.resolvedJid, addr: canonicalHandle.addrStr },
							'identity key changed or new contact, session will be re-established'
						)
					}
				}
			}

			return parsedKeys.transaction(async () => {
				for (const handle of handles) {
					try {
						return type === 'pkmsg'
							? await handle.cipher.decryptPreKeyWhisperMessage(ciphertext)
							: await handle.cipher.decryptWhisperMessage(ciphertext)
					} catch (error: any) {
						lastError = error
						const message = String(error?.message || error)
						const isInvalidPreKeyId = type === 'pkmsg' && message === 'Invalid PreKey ID'
						const isMissingSessionError =
							message === 'No session found to decrypt message' || message === 'No valid sessions'
						const isBadMacError = isSessionBadMacError(message)

						if (isBadMacError) {
							clearCachedSessionState([handle.addrStr])
							logger.warn(
								{
									jid,
									jidAlt,
									triedJid: handle.resolvedJid,
									triedAddr: handle.addrStr,
									error: message
								},
								'cleared cached session state after MAC failure'
							)
						}

						const canTryNextAlias =
							isInvalidPreKeyId || isMissingSessionError || isBadMacError

						if (canTryNextAlias) {
							logger.debug(
								{
									jid,
									jidAlt,
									triedJid: handle.resolvedJid,
									triedAddr: handle.addrStr,
									error: message
								},
								'session decrypt failed for alias, trying next candidate'
							)
							continue
						}

						throw error
					}
				}

				const finalMessage = String((lastError as Error | undefined)?.message || lastError || '')
				if (type === 'pkmsg' && finalMessage === 'Invalid PreKey ID') {
					clearCachedSessionState(handles.map(handle => handle.addrStr))
					logger.warn(
						{
							jid,
							jidAlt,
							attemptedAliases: handles.map(handle => ({
								jid: handle.resolvedJid,
								addr: handle.addrStr
							}))
						},
						'cleared cached session state after Invalid PreKey ID across all aliases'
					)
				} else if (isSessionBadMacError(finalMessage)) {
					clearCachedSessionState(handles.map(handle => handle.addrStr))
					logger.warn(
						{
							jid,
							jidAlt,
							attemptedAliases: handles.map(handle => ({
								jid: handle.resolvedJid,
								addr: handle.addrStr
							}))
						},
						'cleared cached session state after MAC failure across all aliases'
					)
				}

				throw lastError || new Error('Failed to decrypt message for all session aliases')
			}, jid)
		},

		async decryptMessagesBatch(items) {
			if (!items.length) {
				return []
			}

			const out: {
				jid: string
				type: 'pkmsg' | 'msg'
				plaintext: Uint8Array
				error?: string
			}[] = []
			out.length = items.length

			for (let i = 0; i < items.length; i += 1) {
				const item = items[i]!

				try {
					out[i] = {
						jid: item.jid,
						type: item.type,
						plaintext: await repository.decryptMessage(item)
					}
				} catch (error: any) {
					out[i] = {
						jid: item.jid,
						type: item.type,
						plaintext: new Uint8Array(0),
						error: String(error?.message || error)
					}
				}
			}

			return out
		},

		async encryptMessage({ jid, data }) {
			const { cipher } = getSessionCipher(jid)

			// Use transaction to ensure atomicity
			return parsedKeys.transaction(async () => {
				const { type: sigType, body } = await cipher.encrypt(data)
				const type = sigType === 3 ? 'pkmsg' : 'msg'
				return { type, ciphertext: Buffer.from(body) }
			}, jid)
		},

		async encryptMessagesBatch(items) {
			if (!items.length) {
				return []
			}

			const out: {
				jid: string
				type: 'pkmsg' | 'msg'
				ciphertext: Uint8Array
				error?: string
			}[] = []
			out.length = items.length

			const byJid = new Map<string, number[]>()
			for (let i = 0; i < items.length; i += 1) {
				const jid = items[i]!.jid
				const grouped = byJid.get(jid)
				if (grouped) {
					grouped.push(i)
				} else {
					byJid.set(jid, [i])
				}
			}

			for (const [jid, indexes] of byJid.entries()) {
				const { cipher } = getSessionCipher(jid)
				await parsedKeys.transaction(async () => {
					for (const i of indexes) {
						const item = items[i]!
						try {
							const { type: sigType, body } = await cipher.encrypt(item.data)
							const type: 'pkmsg' | 'msg' = sigType === 3 ? 'pkmsg' : 'msg'
							out[i] = {
								jid: item.jid,
								type,
								ciphertext: Buffer.from(body)
							}
						} catch (error: any) {
							out[i] = {
								jid: item.jid,
								type: 'msg',
								ciphertext: new Uint8Array(0),
								error: String(error?.message || error)
							}
						}
					}
				}, jid)
			}

			return out
		},

		async encryptGroupMessage({ group, meId, data }) {
			const { senderName, cipher: session } = getGroupCipherByAuthor(group, meId)
			const builder = new GroupSessionBuilder(storage)

			return parsedKeys.transaction(async () => {
				const senderKeyDistributionMessage = await builder.create(senderName)
				const ciphertext = await session.encrypt(data)

				return {
					ciphertext,
					senderKeyDistributionMessage: senderKeyDistributionMessage.serialize()
				}
			}, group)
		},

		async injectE2ESession({ jid, session }) {
			logger.trace({ jid }, 'injecting E2EE session')
			const cipher = getSessionBuilder(jid)
			return parsedKeys.transaction(async () => {
				await cipher.initOutgoing(session)
			}, jid)
		},
		async injectE2ESessions(items) {
			if (!items.length) {
				return
			}

			logger.trace({ count: items.length }, 'injecting E2EE sessions in batch')
			return parsedKeys.transaction(async () => {
				for (const { jid, session } of items) {
					const cipher = getSessionBuilder(jid)
					await cipher.initOutgoing(session)
				}
			}, `inject-e2e-${items.length}`)
		},
		jidToSignalProtocolAddress(jid) {
			return jidToSignalProtocolAddress(jid).toString()
		},

		// Optimized direct access to LID mapping store
		lidMapping,

		async validateSession(jid: string) {
			try {
				const addr = jidToSignalProtocolAddress(jid)
				const session = await storage.loadSession(addr.toString())

				if (!session) {
					return { exists: false, reason: 'no session' }
				}

				if (!session.haveOpenSession()) {
					return { exists: false, reason: 'no open session' }
				}

				return { exists: true }
			} catch (error) {
				return { exists: false, reason: 'validation error' }
			}
		},

		async deleteSession(jids: string[]) {
			if (!jids.length) return

			// Convert JIDs to signal addresses and prepare for bulk deletion
			const sessionUpdates: { [key: string]: null } = {}
			const addrStrs: string[] = []
			jids.forEach(jid => {
				const addr = jidToSignalProtocolAddress(jid)
				const addrStr = addr.toString()
				sessionUpdates[addrStr] = null
				addrStrs.push(addrStr)
			})
			clearCachedSessionState(addrStrs)

			// Single transaction for all deletions
			return parsedKeys.transaction(async () => {
				await auth.keys.set({ session: sessionUpdates })
			}, `delete-${jids.length}-sessions`)
		},

		async migrateSession(
			fromJid: string,
			toJid: string
		): Promise<{ migrated: number; skipped: number; total: number }> {
			if (!fromJid || (!isLidUser(toJid) && !isHostedLidUser(toJid))) return { migrated: 0, skipped: 0, total: 0 }

			// Only support PN to LID migration
			if (!isPnUser(fromJid) && !isHostedPnUser(fromJid)) {
				return { migrated: 0, skipped: 0, total: 1 }
			}

			const { user } = jidDecode(fromJid)!

			logger.debug({ fromJid }, 'bulk device migration - loading all user devices')

			// Get user's device list from storage
			const { [user]: userDevices } = await parsedKeys.get('device-list', [user])
			if (!userDevices) {
				return { migrated: 0, skipped: 0, total: 0 }
			}

			const { device: fromDevice } = jidDecode(fromJid)!
			const fromDeviceStr = fromDevice?.toString() || '0'
			if (!userDevices.includes(fromDeviceStr)) {
				userDevices.push(fromDeviceStr)
			}

			// Filter out cached devices before database fetch
			const uncachedDevices = userDevices.filter(device => {
				const deviceKey = `${user}.${device}`
				return !migratedSessionCache.has(deviceKey)
			})

			// Bulk check session existence only for uncached devices
			const deviceSessionKeys = uncachedDevices.map(device => `${user}.${device}`)
			const existingSessions = await parsedKeys.get('session', deviceSessionKeys)

			// Step 3: Convert existing sessions to JIDs (only migrate sessions that exist)
			const deviceJids: string[] = []
			for (const [sessionKey, sessionData] of Object.entries(existingSessions)) {
				if (sessionData) {
					// Session exists in storage
					const deviceStr = sessionKey.split('.')[1]
					if (!deviceStr) continue
					const deviceNum = parseInt(deviceStr)
					let jid = deviceNum === 0 ? `${user}@s.whatsapp.net` : `${user}:${deviceNum}@s.whatsapp.net`
					if (deviceNum === 99) {
						jid = `${user}:99@hosted`
					}

					deviceJids.push(jid)
				}
			}

			logger.debug(
				{
					fromJid,
					totalDevices: userDevices.length,
					devicesWithSessions: deviceJids.length,
					devices: deviceJids
				},
				'bulk device migration complete - all user devices processed'
			)

			// Single transaction for all migrations
			return parsedKeys.transaction(
				async (): Promise<{ migrated: number; skipped: number; total: number }> => {
					// Prepare migration operations with addressing metadata
					type MigrationOp = {
						fromJid: string
						toJid: string
						pnUser: string
						lidUser: string
						deviceId: number
						fromAddr: libsignal.ProtocolAddress
						toAddr: libsignal.ProtocolAddress
					}

					const migrationOps: MigrationOp[] = deviceJids.map(jid => {
						const lidWithDevice = transferDevice(jid, toJid)
						const fromDecoded = jidDecode(jid)!
						const toDecoded = jidDecode(lidWithDevice)!

						return {
							fromJid: jid,
							toJid: lidWithDevice,
							pnUser: fromDecoded.user,
							lidUser: toDecoded.user,
							deviceId: fromDecoded.device || 0,
							fromAddr: jidToSignalProtocolAddress(jid),
							toAddr: jidToSignalProtocolAddress(lidWithDevice)
						}
					})

					const totalOps = migrationOps.length
					let migratedCount = 0

					// Bulk fetch PN sessions - already exist (verified during device discovery)
					const pnAddrStrings = Array.from(new Set(migrationOps.map(op => op.fromAddr.toString())))
					const pnSessions = await parsedKeys.get('session', pnAddrStrings)

					// Prepare bulk session updates (PN → LID migration + deletion)
					const sessionUpdates: { [key: string]: libsignal.SerializedSessionRecord | null } = {}

					for (const op of migrationOps) {
						const pnAddrStr = op.fromAddr.toString()
						const lidAddrStr = op.toAddr.toString()

						const pnSession = pnSessions[pnAddrStr]
						if (pnSession) {
							// Session exists (guaranteed from device discovery)
							const fromSession = libsignal.SessionRecord.deserialize(pnSession)
							if (fromSession.haveOpenSession()) {
								// Queue for bulk update: copy to LID, delete from PN
								sessionUpdates[lidAddrStr] = fromSession.serialize()
								sessionUpdates[pnAddrStr] = null

								migratedCount++
							}
						}
					}

					// Single bulk session update for all migrations
					if (Object.keys(sessionUpdates).length > 0) {
						await parsedKeys.set({ session: sessionUpdates })
						logger.debug({ migratedSessions: migratedCount }, 'bulk session migration complete')

						// Cache device-level migrations
						for (const op of migrationOps) {
							if (sessionUpdates[op.toAddr.toString()]) {
								const deviceKey = `${op.pnUser}.${op.deviceId}`
								migratedSessionCache.set(deviceKey, true)
							}
						}
					}

					const skippedCount = totalOps - migratedCount
					return { migrated: migratedCount, skipped: skippedCount, total: totalOps }
				},
				`migrate-${deviceJids.length}-sessions-${jidDecode(toJid)?.user}`
			)
		}
	}

	return repository
}

const jidToSignalProtocolAddress = (jid: string): libsignal.ProtocolAddress => {
	const resolved = nativeResolveSignalAddressFast(jid)
	if (
		!resolved?.signalUser ||
		!resolved.server ||
		typeof resolved.device !== 'number' ||
		!Number.isFinite(resolved.device)
	) {
		throw new Error(`Invalid native signal address payload for jid: "${jid}"`)
	}

	const finalDevice = resolved.device
	if (finalDevice === 99 && resolved.server !== 'hosted' && resolved.server !== 'hosted.lid') {
		throw new Error('Unexpected non-hosted device JID with device 99. This ID seems invalid. ID:' + jid)
	}

	return new libsignal.ProtocolAddress(resolved.signalUser, finalDevice)
}

const jidToSignalSenderKeyName = (group: string, user: string): SenderKeyName => {
	return new SenderKeyName(group, jidToSignalProtocolAddress(user))
}

function signalStorage(
	{ creds, keys }: SignalAuthState,
	lidMapping: LIDMappingStore
): SenderKeyStore &
	libsignal.SignalStorage & {
		loadIdentityKey(id: string): Promise<Uint8Array | undefined>
		saveIdentity(id: string, identityKey: Uint8Array): Promise<boolean>
	} {
	const senderKeyRecordCache = new LRUCache<string, SenderKeyRecord>({
		max: 4096,
		ttl: 10 * 60 * 1000,
		ttlAutopurge: true,
		updateAgeOnGet: true
	})

	// Shared function to resolve PN signal address to LID if mapping exists
	const resolveLIDSignalAddress = async (id: string): Promise<string> => {
		if (id.includes('.')) {
			const [deviceId, device] = id.split('.')
			const [user, domainType_] = deviceId!.split('_')
			const domainType = parseInt(domainType_ || '0')

			if (domainType === WAJIDDomains.LID || domainType === WAJIDDomains.HOSTED_LID) return id

			const pnJid = `${user!}${device !== '0' ? `:${device}` : ''}@${domainType === WAJIDDomains.HOSTED ? 'hosted' : 's.whatsapp.net'}`

			const lidForPN = await lidMapping.getLIDForPN(pnJid)
			if (lidForPN) {
				const lidAddr = jidToSignalProtocolAddress(lidForPN)
				return lidAddr.toString()
			}
		}

		return id
	}

	return {
		loadSession: async (id: string) => {
			try {
				const wireJid = await resolveLIDSignalAddress(id)
				const { [wireJid]: sess } = await keys.get('session', [wireJid])

				if (sess) {
					return libsignal.SessionRecord.deserialize(sess)
				}
			} catch (e) {
				return null
			}

			return null
		},
		storeSession: async (id: string, session: libsignal.SessionRecord) => {
			const wireJid = await resolveLIDSignalAddress(id)
			await keys.set({ session: { [wireJid]: session.serialize() } })
		},
		isTrustedIdentity: () => {
			return true // TOFU - Trust on First Use (same as WhatsApp Web)
		},
		loadIdentityKey: async (id: string) => {
			const wireJid = await resolveLIDSignalAddress(id)
			const { [wireJid]: key } = await keys.get('identity-key', [wireJid])
			return key || undefined
		},
		saveIdentity: async (id: string, identityKey: Uint8Array): Promise<boolean> => {
			const wireJid = await resolveLIDSignalAddress(id)
			const { [wireJid]: existingKey } = await keys.get('identity-key', [wireJid])

			const keysMatch =
				existingKey &&
				existingKey.length === identityKey.length &&
				existingKey.every((byte, i) => byte === identityKey[i])

			if (existingKey && !keysMatch) {
				// Identity changed - clear session and update key
				await keys.set({
					session: { [wireJid]: null },
					'identity-key': { [wireJid]: identityKey }
				})
				return true
			}

			if (!existingKey) {
				// New contact - Trust on First Use (TOFU)
				await keys.set({ 'identity-key': { [wireJid]: identityKey } })
				return true
			}

			return false
		},
		loadPreKey: async (id: number | string) => {
			const keyId = id.toString()
			const { [keyId]: key } = await keys.get('pre-key', [keyId])
			if (key) {
				return {
					privKey: Buffer.from(key.private),
					pubKey: Buffer.from(key.public)
				}
			}
		},
		removePreKey: (id: number) => keys.set({ 'pre-key': { [id]: null } }),
		loadSignedPreKey: () => {
			const key = creds.signedPreKey
			return {
				privKey: Buffer.from(key.keyPair.private),
				pubKey: Buffer.from(key.keyPair.public)
			}
		},
		loadSenderKey: async (senderKeyName: SenderKeyName) => {
			const keyId = senderKeyName.toString()
			const cached = senderKeyRecordCache.get(keyId)
			if (cached) {
				return cached
			}

			const { [keyId]: key } = await keys.get('sender-key', [keyId])
			if (key) {
				const record = SenderKeyRecord.deserialize(key)
				senderKeyRecordCache.set(keyId, record)
				return record
			}

			return new SenderKeyRecord()
		},
		storeSenderKey: async (senderKeyName: SenderKeyName, key: SenderKeyRecord) => {
			const keyId = senderKeyName.toString()
			const serialized = key.serializeForStorage()
			await keys.set({ 'sender-key': { [keyId]: serialized } })
			senderKeyRecordCache.set(keyId, key)
		},
		getOurRegistrationId: () => creds.registrationId,
		getOurIdentity: () => {
			const { signedIdentityKey } = creds
			return {
				privKey: Buffer.from(signedIdentityKey.private),
				pubKey: Buffer.from(generateSignalPubKey(signedIdentityKey.public))
			}
		}
	}
}
