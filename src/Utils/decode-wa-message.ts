import { Boom } from '@hapi/boom'
import { proto } from '../../WAProto/index.js'
import { requireNativeExport } from '../Native/baileys-native'
import { MissingSenderKeySessionError } from '../Signal/Group/group_cipher'
import type { WAMessage, WAMessageKey } from '../Types'
import type { SignalRepositoryWithLIDStore } from '../Types/Signal'
import { type BinaryNode, isHostedLidUser, isHostedPnUser, isLidUser, isPnUser, transferDevice } from '../WABinary'
import { decodeWAMessageBatch } from './generics'
import type { ILogger } from './logger'

export const getDecryptionJid = async (sender: string, repository: SignalRepositoryWithLIDStore): Promise<string> => {
	if (isLidUser(sender) || isHostedLidUser(sender)) {
		return sender
	}

	const mapped = await repository.lidMapping.getLIDForPN(sender)
	return mapped || sender
}

const storeMappingFromEnvelope = async (
	stanza: BinaryNode,
	sender: string,
	repository: SignalRepositoryWithLIDStore,
	decryptionJid: string,
	logger: ILogger
): Promise<void> => {
	const { senderAlt } = extractAddressingContext(stanza)
	const senderIsPn = isPnUser(sender) || isHostedPnUser(sender)
	const senderAltIsLid = senderAlt ? isLidUser(senderAlt) || isHostedLidUser(senderAlt) : false

	if (senderAlt && senderAltIsLid && senderIsPn && decryptionJid === sender) {
		try {
			const lidJid = transferDevice(sender, senderAlt)
			const pnJid = transferDevice(senderAlt, sender)
			await repository.lidMapping.storeLIDPNMappings([{ lid: lidJid, pn: pnJid }])
			await repository.migrateSession(pnJid, lidJid)
			logger.debug({ sender: pnJid, senderAlt: lidJid }, 'Stored LID mapping from envelope')
		} catch (error) {
			logger.warn({ sender, senderAlt, error }, 'Failed to store LID mapping')
		}
	}
}

export const NO_MESSAGE_FOUND_ERROR_TEXT = 'Message absent from node'
export const MISSING_KEYS_ERROR_TEXT = 'Key used already or never filled'

// Retry configuration for failed decryption
export const DECRYPTION_RETRY_CONFIG = {
	maxRetries: 3,
	baseDelayMs: 100,
	sessionRecordErrors: ['No session record', 'SessionError: No session record', 'No session found to decrypt message']
}

export const NACK_REASONS = {
	ParsingError: 487,
	UnrecognizedStanza: 488,
	UnrecognizedStanzaClass: 489,
	UnrecognizedStanzaType: 490,
	InvalidProtobuf: 491,
	InvalidHostedCompanionStanza: 493,
	MissingMessageSecret: 495,
	SignalErrorOldCounter: 496,
	MessageDeletedOnPeer: 499,
	UnhandledError: 500,
	UnsupportedAdminRevoke: 550,
	UnsupportedLIDGroup: 551,
	DBOperationFailed: 552
}

const nativeExtractDecryptPayloadsFast = requireNativeExport('extractDecryptPayloadsFast')
const nativeDecodeMessageNodeFast = requireNativeExport('decodeMessageNodeFast')
type DecryptRuntimeState = {
	suppressCiphertextUpsert?: boolean
}

// Runtime-only flags let the receive pipeline suppress noisy placeholder upserts without mutating protobuf state.
const decryptRuntimeStates = new WeakMap<WAMessage, DecryptRuntimeState>()

const patchDecryptRuntimeState = (message: WAMessage, patch: Partial<DecryptRuntimeState>) => {
	const current = decryptRuntimeStates.get(message) || {}
	decryptRuntimeStates.set(message, { ...current, ...patch })
}

export const getDecryptRuntimeState = (message: WAMessage): DecryptRuntimeState | undefined =>
	decryptRuntimeStates.get(message)

type SignalDecryptCandidate = {
	index: number
	type: 'pkmsg' | 'msg'
	content: Uint8Array
	padded: boolean
}

type NativeSignalBatchResult = {
	plaintext?: Uint8Array | null
	error?: unknown
}

const consumeBatchDecryptResults = (
	batched: readonly NativeSignalBatchResult[],
	candidates: readonly SignalDecryptCandidate[],
	decryptedPayloadsByIndex: ({ messageType: string; msgBuffer: Uint8Array; padded: boolean } | undefined)[],
	markDecryptFailure: (err: unknown, messageType: string) => void
): boolean => {
	if (batched.length !== candidates.length) {
		return false
	}

	for (let i = 0; i < batched.length; i += 1) {
		const batchResult = batched[i]!
		const candidate = candidates[i]!
		if (batchResult.error || !(batchResult.plaintext instanceof Uint8Array) || batchResult.plaintext.length === 0) {
			markDecryptFailure(batchResult.error, candidate.type)
			continue
		}

		decryptedPayloadsByIndex[candidate.index] = {
			messageType: candidate.type,
			msgBuffer: batchResult.plaintext,
			padded: candidate.padded
		}
	}

	return true
}

export const extractAddressingContext = (stanza: BinaryNode) => {
	let senderAlt: string | undefined
	let recipientAlt: string | undefined

	const sender = stanza.attrs.participant || stanza.attrs.from
	const addressingMode = stanza.attrs.addressing_mode || (sender?.endsWith('lid') ? 'lid' : 'pn')

	if (addressingMode === 'lid') {
		// Message is LID-addressed: sender is LID, extract corresponding PN
		// without device data
		senderAlt = stanza.attrs.participant_pn || stanza.attrs.sender_pn || stanza.attrs.peer_recipient_pn
		recipientAlt = stanza.attrs.recipient_pn
		// with device data
		//if (sender && senderAlt) senderAlt = transferDevice(sender, senderAlt)
	} else {
		// Message is PN-addressed: sender is PN, extract corresponding LID
		// without device data
		senderAlt = stanza.attrs.participant_lid || stanza.attrs.sender_lid || stanza.attrs.peer_recipient_lid
		recipientAlt = stanza.attrs.recipient_lid

		//with device data
		//if (sender && senderAlt) senderAlt = transferDevice(sender, senderAlt)
	}

	return {
		addressingMode,
		senderAlt,
		recipientAlt
	}
}

/**
 * Decode the received node as a message.
 * @note this will only parse the message, not decrypt it
 */
export function decodeMessageNode(stanza: BinaryNode, meId: string, meLid: string) {
	// Envelope classification moved to native code, but JS still validates the returned shape before constructing WAMessage.
	const fast = nativeDecodeMessageNodeFast(stanza.attrs as Record<string, unknown>, meId, meLid)
	if (fast?.key?.remoteJid && fast.author && fast.sender) {
		const key: WAMessageKey = {
			remoteJid: fast.key.remoteJid,
			remoteJidAlt: fast.key.remoteJidAlt,
			fromMe: !!fast.key.fromMe,
			id: fast.key.id,
			participant: fast.key.participant,
			participantAlt: fast.key.participantAlt,
			addressingMode: fast.key.addressingMode,
			...(fast.key.server_id ? { server_id: fast.key.server_id } : {})
		}

		const fullMessage: WAMessage = {
			key,
			category: fast.category,
			messageTimestamp: fast.messageTimestamp,
			pushName: fast.pushName,
			broadcast: !!fast.broadcast
		}

		if (key.fromMe) {
			fullMessage.status = proto.WebMessageInfo.Status.SERVER_ACK
		}

		return {
			fullMessage,
			author: fast.author,
			sender: fast.sender
		}
	}

	throw new Boom('native decodeMessageNodeFast returned invalid payload', { data: stanza.attrs })
}

export const decryptMessageNode = (
	stanza: BinaryNode,
	meId: string,
	meLid: string,
	repository: SignalRepositoryWithLIDStore,
	logger: ILogger
) => {
	const { fullMessage, author, sender } = decodeMessageNode(stanza, meId, meLid)
	return {
		fullMessage,
		category: stanza.attrs.category,
		author,
		async decrypt() {
			let decryptables = 0
			const decryptedPayloadsByIndex: ({ messageType: string; msgBuffer: Uint8Array; padded: boolean } | undefined)[] =
				[]
			const decryptCandidates: { messageType: string; content: Uint8Array; padded: boolean }[] = []
			let decryptionJid: string | undefined
			let mappingStored = false
			const getOrLoadDecryptionJid = async () => {
				if (!decryptionJid) {
					decryptionJid = await getDecryptionJid(author, repository)
				}

				return decryptionJid
			}

			const markDecryptFailure = (err: unknown, messageType: string) => {
				const errMessage = err instanceof Error ? err.message : String(err)
				const errorContext = {
					key: fullMessage.key,
					err,
					messageType,
					sender,
					author,
					isSessionRecordError: isSessionRecordError(err)
				}

				if (isRecoverableDecryptError(err)) {
					logger.warn(errorContext, 'failed to decrypt message')
				} else {
					logger.error(errorContext, 'failed to decrypt message')
				}

				fullMessage.messageStubType = proto.WebMessageInfo.StubType.CIPHERTEXT
				fullMessage.messageStubParameters = [errMessage]
				if (messageType === 'skmsg' && err instanceof MissingSenderKeySessionError) {
					patchDecryptRuntimeState(fullMessage, { suppressCiphertextUpsert: true })
				}
			}

			const stanzaContent = Array.isArray(stanza.content) ? stanza.content : []

			for (const { tag, content } of stanzaContent) {
				if (tag === 'verified_name' && content instanceof Uint8Array) {
					const cert = proto.VerifiedNameCertificate.decode(content)
					const details = proto.VerifiedNameCertificate.Details.decode(cert.details!)
					fullMessage.verifiedBizName = details.verifiedName
				}
			}

			if (stanzaContent.length) {
				const fast = nativeExtractDecryptPayloadsFast(stanza as unknown)
				if (!fast || !Array.isArray(fast.payloads)) {
					throw new Error('native extractDecryptPayloadsFast returned invalid payload')
				}

				if (fast.hasViewOnceUnavailable) {
					fullMessage.isViewOnce = true
				}

				if (typeof fast.retryCount === 'number' && Number.isFinite(fast.retryCount)) {
					fullMessage.retryCount = fast.retryCount
				}

				for (const payload of fast.payloads) {
					if (!(payload?.content instanceof Uint8Array) || !payload?.messageType) {
						continue
					}

					decryptCandidates.push({
						messageType: payload.messageType,
						content: payload.content,
						padded: payload.padded !== false
					})
				}
			}

			decryptables = decryptCandidates.length

			decryptedPayloadsByIndex.length = decryptCandidates.length

			const groupedCandidates: {
				index: number
				messageType: string
				content: Uint8Array
				padded: boolean
			}[] = []
			const signalCandidates: SignalDecryptCandidate[] = []

			for (let i = 0; i < decryptCandidates.length; i += 1) {
				const candidate = decryptCandidates[i]!
				const e2eType = candidate.messageType
				if (e2eType === 'plaintext') {
					decryptedPayloadsByIndex[i] = {
						messageType: e2eType,
						msgBuffer: candidate.content,
						padded: candidate.padded
					}
					continue
				}

				if (e2eType === 'pkmsg' || e2eType === 'msg') {
					signalCandidates.push({
						index: i,
						type: e2eType,
						content: candidate.content,
						padded: candidate.padded
					})
					continue
				}

				groupedCandidates.push({
					index: i,
					messageType: e2eType,
					content: candidate.content,
					padded: candidate.padded
				})
			}

			if ((signalCandidates.length > 0 || groupedCandidates.length > 0) && !mappingStored) {
				const jid = await getOrLoadDecryptionJid()
				await storeMappingFromEnvelope(stanza, author, repository, jid, logger)
				mappingStored = true
			}

			const signalAltJid = fullMessage.key.participantAlt || fullMessage.key.remoteJidAlt

			if (signalCandidates.length > 0) {
				// Direct signal payloads are decrypted in batch; sender-key payloads still need the group-session path below.
				if (!repository.decryptMessagesBatch) {
					throw new Boom('strict native mode requires decryptMessagesBatch', { statusCode: 500 })
				}

				const jid = await getOrLoadDecryptionJid()
				const batched = await repository.decryptMessagesBatch(
					signalCandidates.map(candidate => ({
						jid,
						jidAlt: signalAltJid,
						type: candidate.type,
						ciphertext: candidate.content
					}))
				)

				if (!Array.isArray(batched) || batched.length !== signalCandidates.length) {
					throw new Boom('native batch decrypt returned invalid payload size', {
						statusCode: 500,
						data: {
							inputs: signalCandidates.length,
							outputs: Array.isArray(batched) ? batched.length : 'invalid'
						}
					})
				}

				const consumed = consumeBatchDecryptResults(
					batched as NativeSignalBatchResult[],
					signalCandidates,
					decryptedPayloadsByIndex,
					markDecryptFailure
				)
				if (!consumed) {
					throw new Boom('native batch decrypt payload shape mismatch', { statusCode: 500 })
				}
			}

			for (const candidate of groupedCandidates) {
				try {
					if (candidate.messageType === 'skmsg') {
						const msgBuffer = await repository.decryptGroupMessage({
							group: sender,
							authorJid: author,
							authorAltJid: fullMessage.key.participantAlt,
							msg: candidate.content
						})
						decryptedPayloadsByIndex[candidate.index] = {
							messageType: candidate.messageType,
							msgBuffer,
							padded: candidate.padded
						}
						continue
					}

					throw new Error(`Unknown e2e type: ${candidate.messageType}`)
				} catch (err: any) {
					markDecryptFailure(err, candidate.messageType)
				}
			}

			const decryptedPayloads = decryptedPayloadsByIndex.filter(payload => !!payload) as {
				messageType: string
				msgBuffer: Uint8Array
				padded: boolean
			}[]

			if (decryptedPayloads.length > 0) {
				const decodedMessages = decodeWAMessageBatch(
					decryptedPayloads.map(({ msgBuffer, padded }) => ({
						data: msgBuffer,
						padded
					}))
				)
				if (decodedMessages.length !== decryptedPayloads.length) {
					throw new Error('native decodeWAMessageBatch returned invalid payload length')
				}

				for (let i = 0; i < decryptedPayloads.length; i += 1) {
					let msg = decodedMessages[i]!

					msg = msg.deviceSentMessage?.message || msg
					if (msg.senderKeyDistributionMessage) {
						try {
							await repository.processSenderKeyDistributionMessage({
								authorJid: author,
								authorAltJid: fullMessage.key.participantAlt,
								item: msg.senderKeyDistributionMessage
							})
						} catch (err) {
							logger.error({ key: fullMessage.key, err }, 'failed to process sender key distribution message')
						}
					}

					if (fullMessage.message) {
						Object.assign(fullMessage.message, msg)
					} else {
						fullMessage.message = msg
					}
				}
			}

			// if nothing was found to decrypt
			if (!decryptables && !fullMessage.isViewOnce) {
				fullMessage.messageStubType = proto.WebMessageInfo.StubType.CIPHERTEXT
				fullMessage.messageStubParameters = [NO_MESSAGE_FOUND_ERROR_TEXT]
			}
		}
	}
}

/**
 * Utility function to check if an error is related to missing session record
 */
function isSessionRecordError(error: any): boolean {
	const errorMessage = error?.message || error?.toString() || ''
	return DECRYPTION_RETRY_CONFIG.sessionRecordErrors.some(errorPattern => errorMessage.includes(errorPattern))
}

function isRecoverableDecryptError(error: any): boolean {
	const errorMessage = (error?.message || error?.toString() || '').toLowerCase()
	if (!errorMessage) {
		return false
	}

	return (
		errorMessage.includes('invalid prekey') ||
		errorMessage.includes('no session found to decrypt message') ||
		errorMessage.includes('no session record') ||
		errorMessage.includes('no valid sessions') ||
		errorMessage.includes('invalidmessageexception') ||
		errorMessage.includes('bad mac') ||
		errorMessage.includes('missing sender key')
	)
}
