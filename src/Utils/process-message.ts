import { proto } from '../../WAProto/index.js'
import { requireNativeExport } from '../Native/baileys-native'
import type {
	AuthenticationCreds,
	BaileysEventEmitter,
	CacheStore,
	Chat,
	GroupMetadata,
	GroupParticipant,
	LIDMapping,
	ParticipantAction,
	RequestJoinAction,
	RequestJoinMethod,
	SignalKeyStoreWithTransaction,
	SignalRepositoryWithLIDStore,
	SocketConfig,
	WAMessage,
	WAMessageKey
} from '../Types'
import { WAMessageStubType } from '../Types'
import { getContentType, normalizeMessageContent } from '../Utils/messages'
import {
	areJidsSameUser,
	isHostedLidUser,
	isHostedPnUser,
	isJidBroadcast,
	isJidStatusBroadcast,
	isLidUser,
	jidDecode,
	jidEncode,
	jidNormalizedUser
} from '../WABinary'
import { getKeyAuthor, toNumber } from './generics'
import { downloadAndProcessHistorySyncNotification } from './history'
import type { ILogger } from './logger'

const nativeDecodePollVoteMessageFast = requireNativeExport('decodePollVoteMessageFast')
const nativeDecodeEventResponseMessageFast = requireNativeExport('decodeEventResponseMessageFast')
const nativeParseJsonStringArrayFast = requireNativeExport('parseJsonStringArrayFast')
const nativeParseGroupParticipantStubsFast = requireNativeExport('parseGroupParticipantStubsFast')

const parseJsonStringArray = <T>(values?: (string | null | undefined)[]): T[] => {
	if (!values?.length) {
		return []
	}

	const compact = values.filter((value): value is string => typeof value === 'string')
	if (compact.length === 0) {
		return []
	}

	const parsed = nativeParseJsonStringArrayFast(compact)
	if (!Array.isArray(parsed) || parsed.length !== compact.length) {
		throw new Error('native parseJsonStringArrayFast returned invalid payload')
	}

	return parsed as T[]
}

const parseGroupParticipantStubs = (values?: (string | null | undefined)[]): GroupParticipant[] => {
	if (!values?.length) {
		return []
	}

	const compact = values.filter((value): value is string => typeof value === 'string')
	if (compact.length === 0) {
		return []
	}

	const parsed = nativeParseGroupParticipantStubsFast(compact)
	if (!Array.isArray(parsed) || parsed.length !== compact.length) {
		throw new Error('native parseGroupParticipantStubsFast returned invalid payload')
	}

	return parsed as GroupParticipant[]
}

type ProcessMessageContext = {
	shouldProcessHistoryMsg: boolean
	placeholderResendCache?: CacheStore
	creds: AuthenticationCreds
	keyStore: SignalKeyStoreWithTransaction
	ev: BaileysEventEmitter
	logger?: ILogger
	options: RequestInit
	signalRepository: SignalRepositoryWithLIDStore
	getMessage: SocketConfig['getMessage']
}

const REAL_MSG_STUB_TYPES = new Set([
	WAMessageStubType.CALL_MISSED_GROUP_VIDEO,
	WAMessageStubType.CALL_MISSED_GROUP_VOICE,
	WAMessageStubType.CALL_MISSED_VIDEO,
	WAMessageStubType.CALL_MISSED_VOICE
])

const REAL_MSG_REQ_ME_STUB_TYPES = new Set([WAMessageStubType.GROUP_PARTICIPANT_ADD])

/** Cleans a received message to further processing */
export const cleanMessage = (message: WAMessage, meId: string, meLid: string) => {
	// ensure remoteJid and participant doesn't have device or agent in it
	if (isHostedPnUser(message.key.remoteJid!) || isHostedLidUser(message.key.remoteJid!)) {
		message.key.remoteJid = jidEncode(
			jidDecode(message.key?.remoteJid!)?.user!,
			isHostedPnUser(message.key.remoteJid!) ? 's.whatsapp.net' : 'lid'
		)
	} else {
		message.key.remoteJid = jidNormalizedUser(message.key.remoteJid!)
	}

	if (isHostedPnUser(message.key.participant!) || isHostedLidUser(message.key.participant!)) {
		message.key.participant = jidEncode(
			jidDecode(message.key.participant!)?.user!,
			isHostedPnUser(message.key.participant!) ? 's.whatsapp.net' : 'lid'
		)
	} else {
		message.key.participant = jidNormalizedUser(message.key.participant!)
	}

	const content = normalizeMessageContent(message.message)
	// if the message has a reaction, ensure fromMe & remoteJid are from our perspective
	if (content?.reactionMessage) {
		normaliseKey(content.reactionMessage.key!)
	}

	if (content?.pollUpdateMessage) {
		normaliseKey(content.pollUpdateMessage.pollCreationMessageKey!)
	}

	function normaliseKey(msgKey: WAMessageKey) {
		// if the reaction is from another user
		// we've to correctly map the key to this user's perspective
		if (!message.key.fromMe) {
			// if the sender believed the message being reacted to is not from them
			// we've to correct the key to be from them, or some other participant
			msgKey.fromMe = !msgKey.fromMe
				? areJidsSameUser(msgKey.participant || msgKey.remoteJid!, meId) ||
					areJidsSameUser(msgKey.participant || msgKey.remoteJid!, meLid)
				: // if the message being reacted to, was from them
					// fromMe automatically becomes false
					false
			// use the parent message chat so reactions/poll updates are scoped correctly
			msgKey.remoteJid = message.key.remoteJid
			// set participant of the message
			msgKey.participant = msgKey.participant || message.key.participant
		}
	}
}

export const isRealMessage = (message: WAMessage) => {
	const normalizedContent = normalizeMessageContent(message.message)
	const hasSomeContent = !!getContentType(normalizedContent)
	return (
		(!!normalizedContent ||
			REAL_MSG_STUB_TYPES.has(message.messageStubType!) ||
			REAL_MSG_REQ_ME_STUB_TYPES.has(message.messageStubType!)) &&
		hasSomeContent &&
		!normalizedContent?.protocolMessage &&
		!normalizedContent?.reactionMessage &&
		!normalizedContent?.pollUpdateMessage
	)
}

export const shouldIncrementChatUnread = (message: WAMessage) => !message.key.fromMe && !message.messageStubType

/**
 * Get the ID of the chat from the given key.
 * Typically -- that'll be the remoteJid, but for broadcasts, it'll be the participant
 */
export const getChatId = ({ remoteJid, participant, fromMe }: WAMessageKey) => {
	if (isJidBroadcast(remoteJid!) && !isJidStatusBroadcast(remoteJid!) && !fromMe) {
		return participant!
	}

	return remoteJid!
}

type PollContext = {
	/** normalised jid of the person that created the poll */
	pollCreatorJid: string
	/** ID of the poll creation message */
	pollMsgId: string
	/** poll creation message enc key */
	pollEncKey: Uint8Array
	/** jid of the person that voted */
	voterJid: string
}

type EventContext = {
	/** normalised jid of the person that created the event */
	eventCreatorJid: string
	/** ID of the event creation message */
	eventMsgId: string
	/** event creation message enc key */
	eventEncKey: Uint8Array
	/** jid of the person that responded */
	responderJid: string
}

/**
 * Decrypt a poll vote
 * @param vote encrypted vote
 * @param ctx additional info about the poll required for decryption
 * @returns list of SHA256 options
 */
export function decryptPollVote(
	{ encPayload, encIv }: proto.Message.IPollEncValue,
	{ pollCreatorJid, pollMsgId, pollEncKey, voterJid }: PollContext
) {
	if (!encPayload || !encIv) {
		throw new Error('poll vote payload/iv is required for native decryption')
	}

	const decoded = nativeDecodePollVoteMessageFast(encPayload, encIv, pollMsgId, pollCreatorJid, voterJid, pollEncKey)
	if (!decoded) {
		throw new Error('native decodePollVoteMessageFast returned invalid payload')
	}

	return decoded as proto.Message.PollVoteMessage
}

/**
 * Decrypt an event response
 * @param response encrypted event response
 * @param ctx additional info about the event required for decryption
 * @returns event response message
 */
export function decryptEventResponse(
	{ encPayload, encIv }: proto.Message.IPollEncValue,
	{ eventCreatorJid, eventMsgId, eventEncKey, responderJid }: EventContext
) {
	if (!encPayload || !encIv) {
		throw new Error('event response payload/iv is required for native decryption')
	}

	const decoded = nativeDecodeEventResponseMessageFast(
		encPayload,
		encIv,
		eventMsgId,
		eventCreatorJid,
		responderJid,
		eventEncKey
	)
	if (!decoded) {
		throw new Error('native decodeEventResponseMessageFast returned invalid payload')
	}

	return decoded as proto.Message.EventResponseMessage
}

const processMessage = async (
	message: WAMessage,
	{
		shouldProcessHistoryMsg,
		placeholderResendCache,
		ev,
		creds,
		signalRepository,
		keyStore,
		logger,
		options,
		getMessage
	}: ProcessMessageContext
) => {
	const meId = creds.me!.id
	const { accountSettings } = creds

	const chat: Partial<Chat> = { id: jidNormalizedUser(getChatId(message.key)) }
	const isRealMsg = isRealMessage(message)

	if (isRealMsg) {
		chat.messages = [{ message }]
		chat.conversationTimestamp = toNumber(message.messageTimestamp)
		// only increment unread count if not CIPHERTEXT and from another person
		if (shouldIncrementChatUnread(message)) {
			chat.unreadCount = (chat.unreadCount || 0) + 1
		}
	}

	const content = normalizeMessageContent(message.message)

	// unarchive chat if it's a real message, or someone reacted to our message
	// and we've the unarchive chats setting on
	if ((isRealMsg || content?.reactionMessage?.key?.fromMe) && accountSettings?.unarchiveChats) {
		chat.archived = false
		chat.readOnly = false
	}

	const protocolMsg = content?.protocolMessage
	if (protocolMsg) {
		switch (protocolMsg.type) {
			case proto.Message.ProtocolMessage.Type.HISTORY_SYNC_NOTIFICATION:
				const histNotification = protocolMsg.historySyncNotification!
				const process = shouldProcessHistoryMsg
				const isLatest = !creds.processedHistoryMessages?.length

				logger?.info(
					{
						histNotification,
						process,
						id: message.key.id,
						isLatest
					},
					'got history notification'
				)

				if (process) {
					if (histNotification.syncType !== proto.HistorySync.HistorySyncType.ON_DEMAND) {
						const alreadyProcessed = (creds.processedHistoryMessages || []).some(
							entry =>
								entry?.key?.id === message.key.id &&
								areJidsSameUser(entry?.key?.remoteJid || undefined, message.key.remoteJid || undefined) &&
								!!entry?.messageTimestamp
						)
						ev.emit('creds.update', {
							processedHistoryMessages: alreadyProcessed
								? creds.processedHistoryMessages || []
								: [
										...(creds.processedHistoryMessages || []),
										{ key: message.key, messageTimestamp: message.messageTimestamp }
									]
						})
					}

					const data = await downloadAndProcessHistorySyncNotification(histNotification, options, logger)

					if (data.lidPnMappings?.length) {
						logger?.debug({ count: data.lidPnMappings.length }, 'processing LID-PN mappings from history sync')
						await signalRepository.lidMapping
							.storeLIDPNMappings(data.lidPnMappings)
							.catch(err => logger?.warn({ err }, 'failed to store LID-PN mappings from history sync'))
					}

					ev.emit('messaging-history.set', {
						...data,
						isLatest: histNotification.syncType !== proto.HistorySync.HistorySyncType.ON_DEMAND ? isLatest : undefined,
						peerDataRequestSessionId: histNotification.peerDataRequestSessionId
					})
				}

				break
			case proto.Message.ProtocolMessage.Type.APP_STATE_SYNC_KEY_SHARE:
				const keys = protocolMsg.appStateSyncKeyShare!.keys
				if (keys?.length) {
					let newAppStateSyncKeyId = ''
					await keyStore.transaction(async () => {
						const newKeys: string[] = []
						for (const { keyData, keyId } of keys) {
							const strKeyId = Buffer.from(keyId!.keyId!).toString('base64')
							newKeys.push(strKeyId)

							await keyStore.set({ 'app-state-sync-key': { [strKeyId]: keyData! } })

							newAppStateSyncKeyId = strKeyId
						}

						logger?.info({ newAppStateSyncKeyId, newKeys }, 'injecting new app state sync keys')
					}, meId)

					ev.emit('creds.update', { myAppStateKeyId: newAppStateSyncKeyId })
				} else {
					logger?.info({ protocolMsg }, 'recv app state sync with 0 keys')
				}

				break
			case proto.Message.ProtocolMessage.Type.REVOKE:
				ev.emit('messages.update', [
					{
						key: {
							...message.key,
							id: protocolMsg.key!.id
						},
						update: { message: null, messageStubType: WAMessageStubType.REVOKE, key: message.key }
					}
				])
				break
			case proto.Message.ProtocolMessage.Type.EPHEMERAL_SETTING:
				Object.assign(chat, {
					ephemeralSettingTimestamp: toNumber(message.messageTimestamp),
					ephemeralExpiration: protocolMsg.ephemeralExpiration || null
				})
				break
			case proto.Message.ProtocolMessage.Type.PEER_DATA_OPERATION_REQUEST_RESPONSE_MESSAGE:
				const response = protocolMsg.peerDataOperationRequestResponseMessage!
				if (response) {
					const peerDataOperationResult = response.peerDataOperationResult || []
					ev.emit('peer-data-operation.response', {
						requestId: response.stanzaId || undefined,
						requestType: response.peerDataOperationRequestType || undefined,
						results: peerDataOperationResult
					})

					for (const result of peerDataOperationResult) {
						// eslint-disable-next-line max-depth
						if (result?.fullHistorySyncOnDemandRequestResponse) {
							const status = result.fullHistorySyncOnDemandRequestResponse.responseCode
							// eslint-disable-next-line max-depth
							if (
								typeof status === 'number' &&
								status !==
									proto.Message.PeerDataOperationRequestResponseMessage.PeerDataOperationResult
										.FullHistorySyncOnDemandResponseCode.REQUEST_SUCCESS
							) {
								logger?.warn({ status, stanzaId: response.stanzaId }, 'history sync on demand request was not accepted')
							}
						}

						// eslint-disable-next-line max-depth
						if (result?.historySyncChunkRetryResponse && result.historySyncChunkRetryResponse.canRecover === false) {
							logger?.warn(
								{ response: result.historySyncChunkRetryResponse, stanzaId: response.stanzaId },
								'history sync chunk retry reported non-recoverable state'
							)
						}

						const retryResponse = result?.placeholderMessageResendResponse
						//eslint-disable-next-line max-depth
						if (!retryResponse?.webMessageInfoBytes) {
							continue
						}

						//eslint-disable-next-line max-depth
						try {
							const webMessageInfo = proto.WebMessageInfo.decode(retryResponse.webMessageInfoBytes)
							const msgId = webMessageInfo.key?.id
							// Retrieve cached original message data (preserves LID details,
							// timestamps, etc. that the phone may omit in its PDO response)
							const cachedData = msgId ? await placeholderResendCache?.get<Partial<WAMessage> | true>(msgId) : undefined
							//eslint-disable-next-line max-depth
							if (msgId) {
								await placeholderResendCache?.del(msgId)
							}

							let finalMsg: WAMessage
							//eslint-disable-next-line max-depth
							if (cachedData && typeof cachedData === 'object') {
								// Apply decoded message content onto cached metadata (preserves LID etc.)
								cachedData.message = webMessageInfo.message
								//eslint-disable-next-line max-depth
								if (webMessageInfo.messageTimestamp) {
									cachedData.messageTimestamp = webMessageInfo.messageTimestamp
								}

								finalMsg = cachedData as WAMessage
							} else {
								finalMsg = webMessageInfo as WAMessage
							}

							logger?.debug({ msgId, requestId: response.stanzaId }, 'received placeholder resend')

							ev.emit('messages.upsert', {
								messages: [finalMsg],
								type: 'notify',
								requestId: response.stanzaId!
							})
						} catch (err) {
							logger?.warn({ err, stanzaId: response.stanzaId }, 'failed to decode placeholder resend response')
						}
					}
				}

				break
			case proto.Message.ProtocolMessage.Type.MESSAGE_EDIT:
				ev.emit('messages.update', [
					{
						// flip the sender / fromMe properties because they're in the perspective of the sender
						key: { ...message.key, id: protocolMsg.key?.id },
						update: {
							message: {
								editedMessage: {
									message: protocolMsg.editedMessage
								}
							},
							messageTimestamp: protocolMsg.timestampMs
								? Math.floor(toNumber(protocolMsg.timestampMs) / 1000)
								: message.messageTimestamp
						}
					}
				])
				break
			case proto.Message.ProtocolMessage.Type.GROUP_MEMBER_LABEL_CHANGE:
				const labelAssociationMsg = protocolMsg.memberLabel
				if (labelAssociationMsg?.label) {
					ev.emit('group.member-tag.update', {
						groupId: chat.id!,
						label: labelAssociationMsg.label,
						participant: message.key.participant!,
						participantAlt: message.key.participantAlt!,
						messageTimestamp: Number(message.messageTimestamp)
					})
				}

				break
			case proto.Message.ProtocolMessage.Type.LID_MIGRATION_MAPPING_SYNC:
				const encodedPayload = protocolMsg.lidMigrationMappingSyncMessage?.encodedMappingPayload!
				const { pnToLidMappings, chatDbMigrationTimestamp } =
					proto.LIDMigrationMappingSyncPayload.decode(encodedPayload)
				logger?.debug({ pnToLidMappings, chatDbMigrationTimestamp }, 'got lid mappings and chat db migration timestamp')
				const pairs = []
				for (const { pn, latestLid, assignedLid } of pnToLidMappings) {
					const lid = latestLid || assignedLid
					pairs.push({ lid: `${lid}@lid`, pn: `${pn}@s.whatsapp.net` })
				}

				await signalRepository.lidMapping.storeLIDPNMappings(pairs)
				if (pairs.length) {
					for (const { pn, lid } of pairs) {
						await signalRepository.migrateSession(pn, lid)
					}
				}
		}
	} else if (content?.reactionMessage) {
		const reaction: proto.IReaction = {
			...content.reactionMessage,
			key: message.key
		}
		ev.emit('messages.reaction', [
			{
				reaction,
				key: content.reactionMessage?.key!
			}
		])
	} else if (content?.encEventResponseMessage) {
		const encEventResponse = content.encEventResponseMessage
		const creationMsgKey = encEventResponse.eventCreationMessageKey!

		// we need to fetch the event creation message to get the event enc key
		const eventMsg = await getMessage(creationMsgKey)
		if (eventMsg) {
			try {
				const meIdNormalised = jidNormalizedUser(meId)

				// all jids need to be PN
				const eventCreatorKey = creationMsgKey.participant || creationMsgKey.remoteJid!
				const eventCreatorPn = isLidUser(eventCreatorKey)
					? await signalRepository.lidMapping.getPNForLID(eventCreatorKey)
					: eventCreatorKey
				const eventCreatorJid = getKeyAuthor(
					{ remoteJid: jidNormalizedUser(eventCreatorPn!), fromMe: meIdNormalised === eventCreatorPn },
					meIdNormalised
				)

				const responderJid = getKeyAuthor(message.key, meIdNormalised)
				const eventEncKey = eventMsg?.messageContextInfo?.messageSecret

				if (!eventEncKey) {
					logger?.warn({ creationMsgKey }, 'event response: missing messageSecret for decryption')
				} else {
					const responseMsg = decryptEventResponse(encEventResponse, {
						eventEncKey,
						eventCreatorJid,
						eventMsgId: creationMsgKey.id!,
						responderJid
					})

					const eventResponse = {
						eventResponseMessageKey: message.key,
						senderTimestampMs: responseMsg.timestampMs!,
						response: responseMsg
					}

					ev.emit('messages.update', [
						{
							key: creationMsgKey,
							update: {
								eventResponses: [eventResponse]
							}
						}
					])
				}
			} catch (err) {
				logger?.warn({ err, creationMsgKey }, 'failed to decrypt event response')
			}
		} else {
			logger?.warn({ creationMsgKey }, 'event creation message not found, cannot decrypt response')
		}
	} else if (message.messageStubType) {
		const jid = message.key?.remoteJid!
		//let actor = whatsappID (message.participant)
		let participants: GroupParticipant[]
		const emitParticipantsUpdate = (action: ParticipantAction) =>
			ev.emit('group-participants.update', {
				id: jid,
				author: message.key.participant!,
				authorPn: message.key.participantAlt!,
				participants,
				action
			})
		const emitGroupUpdate = (update: Partial<GroupMetadata>) => {
			ev.emit('groups.update', [
				{ id: jid, ...update, author: message.key.participant ?? undefined, authorPn: message.key.participantAlt }
			])
		}

		const emitGroupRequestJoin = (participant: LIDMapping, action: RequestJoinAction, method: RequestJoinMethod) => {
			ev.emit('group.join-request', {
				id: jid,
				author: message.key.participant!,
				authorPn: message.key.participantAlt!,
				participant: participant.lid,
				participantPn: participant.pn,
				action,
				method: method!
			})
		}

		const participantsIncludesMe = () => participants.find(jid => areJidsSameUser(meId, jid.phoneNumber)) // ADD SUPPORT FOR LID

		switch (message.messageStubType) {
			case WAMessageStubType.GROUP_PARTICIPANT_CHANGE_NUMBER:
				participants = parseGroupParticipantStubs(message.messageStubParameters as string[])
				emitParticipantsUpdate('modify')
				break
			case WAMessageStubType.GROUP_PARTICIPANT_LEAVE:
			case WAMessageStubType.GROUP_PARTICIPANT_REMOVE:
				participants = parseGroupParticipantStubs(message.messageStubParameters as string[])
				emitParticipantsUpdate('remove')
				// mark the chat read only if you left the group
				if (participantsIncludesMe()) {
					chat.readOnly = true
				}

				break
			case WAMessageStubType.GROUP_PARTICIPANT_ADD:
			case WAMessageStubType.GROUP_PARTICIPANT_INVITE:
			case WAMessageStubType.GROUP_PARTICIPANT_ADD_REQUEST_JOIN:
				participants = parseGroupParticipantStubs(message.messageStubParameters as string[])
				if (participantsIncludesMe()) {
					chat.readOnly = false
				}

				emitParticipantsUpdate('add')
				break
			case WAMessageStubType.GROUP_PARTICIPANT_DEMOTE:
				participants = parseGroupParticipantStubs(message.messageStubParameters as string[])
				emitParticipantsUpdate('demote')
				break
			case WAMessageStubType.GROUP_PARTICIPANT_PROMOTE:
				participants = parseGroupParticipantStubs(message.messageStubParameters as string[])
				emitParticipantsUpdate('promote')
				break
			case WAMessageStubType.GROUP_CHANGE_ANNOUNCE:
				const announceValue = message.messageStubParameters?.[0]
				emitGroupUpdate({ announce: announceValue === 'true' || announceValue === 'on' })
				break
			case WAMessageStubType.GROUP_CHANGE_RESTRICT:
				const restrictValue = message.messageStubParameters?.[0]
				emitGroupUpdate({ restrict: restrictValue === 'true' || restrictValue === 'on' })
				break
			case WAMessageStubType.GROUP_CHANGE_SUBJECT:
				const name = message.messageStubParameters?.[0]
				chat.name = name
				emitGroupUpdate({ subject: name })
				break
			case WAMessageStubType.GROUP_CHANGE_DESCRIPTION:
				const description = message.messageStubParameters?.[0]
				chat.description = description
				emitGroupUpdate({ desc: description })
				break
			case WAMessageStubType.GROUP_CHANGE_INVITE_LINK:
				const code = message.messageStubParameters?.[0]
				emitGroupUpdate({ inviteCode: code })
				break
			case WAMessageStubType.GROUP_MEMBER_ADD_MODE:
				const memberAddValue = message.messageStubParameters?.[0]
				emitGroupUpdate({ memberAddMode: memberAddValue === 'all_member_add' })
				break
			case WAMessageStubType.GROUP_MEMBERSHIP_JOIN_APPROVAL_MODE:
				const approvalMode = message.messageStubParameters?.[0]
				emitGroupUpdate({ joinApprovalMode: approvalMode === 'on' })
				break
			case WAMessageStubType.GROUP_MEMBERSHIP_JOIN_APPROVAL_REQUEST_NON_ADMIN_ADD:
			case WAMessageStubType.GROUP_MEMBERSHIP_JOIN_APPROVAL_REQUEST:
				const participant = parseJsonStringArray<LIDMapping>([message.messageStubParameters?.[0]])[0] as LIDMapping
				if (!participant) {
					throw new Error('invalid membership join participant payload')
				}

				const action = message.messageStubParameters?.[1] as RequestJoinAction
				const method = message.messageStubParameters?.[2] as RequestJoinMethod
				emitGroupRequestJoin(participant, action, method)
				break
		}
	} /*  else if(content?.pollUpdateMessage) {
			const creationMsgKey = content.pollUpdateMessage.pollCreationMessageKey!
			// we need to fetch the poll creation message to get the poll enc key
			const pollMsg = await getMessage(creationMsgKey)
		if(pollMsg) {
			const meIdNormalised = jidNormalizedUser(meId)
			const pollCreatorJid = getKeyAuthor(creationMsgKey, meIdNormalised)
			const voterJid = getKeyAuthor(message.key, meIdNormalised)
			const pollEncKey = pollMsg.messageContextInfo?.messageSecret!

			try {
				const voteMsg = decryptPollVote(
					content.pollUpdateMessage.vote!,
					{
						pollEncKey,
						pollCreatorJid,
						pollMsgId: creationMsgKey.id!,
						voterJid,
					}
				)
				ev.emit('messages.update', [
					{
						key: creationMsgKey,
						update: {
							pollUpdates: [
								{
									pollUpdateMessageKey: message.key,
									vote: voteMsg,
									senderTimestampMs: (content.pollUpdateMessage.senderTimestampMs! as Long).toNumber(),
								}
							]
						}
					}
				])
			} catch(err) {
				logger?.warn(
					{ err, creationMsgKey },
					'failed to decrypt poll vote'
				)
			}
		} else {
			logger?.warn(
				{ creationMsgKey },
				'poll creation message not found, cannot decrypt update'
			)
		}
		} */

	if (Object.keys(chat).length > 1) {
		ev.emit('chats.update', [chat])
	}
}

export default processMessage
