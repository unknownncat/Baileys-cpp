import { Boom } from '@hapi/boom'
import NodeCache from '@cacheable/node-cache'
import readline from 'readline'
import makeWASocket, {
	CacheStore,
	DEFAULT_CONNECTION_CONFIG,
	DisconnectReason,
	fetchLatestBaileysVersion,
	generateMessageIDV2,
	getAggregateVotesInPollMessage,
	isJidNewsletter,
	makeCacheableSignalKeyStore,
	proto,
	useNativeAuthState,
	WAMessageContent,
	WAMessageKey,
	type WAMessage,
	type WASocket
} from '../src'
import { getNativeBindingLoadInfo } from '../src/Native/baileys-native'
import P from 'pino'
import qrc from "qrcode-terminal"

const logger = P({
	level: "trace",
	transport: {
		targets: [
			{
				target: "pino-pretty", // pretty-print for console
				options: { colorize: true },
				level: "trace",
			},
			{
				target: "pino/file", // raw file output
				options: { destination: './wa-logs.txt' },
				level: "trace",
			},
		],
	},
})
logger.level = 'trace'

const doReplies = process.argv.includes('--do-reply')
const usePairingCode = process.argv.includes('--use-pairing-code')
const validateNativeAuthState = process.argv.includes('--validate-native-auth-state')
const EXAMPLE_REPLY_JID = process.env.EXAMPLE_REPLY_JID?.trim() || undefined
const REQUEST_PLACEHOLDER_COMMAND = 'requestPlaceholder'
const ON_DEMAND_HISTORY_SYNC_COMMAND = 'onDemandHistSync'

// external map to store retry counts of messages when decryption/encryption fails
// keep this out of the socket itself, so as to prevent a message decryption/encryption loop across socket restarts
const msgRetryCounterCache = new NodeCache() as CacheStore

const RECONNECT_DELAY_BASE_MS = 1_000
const RECONNECT_DELAY_MAX_MS = 30_000

let reconnectTimer: NodeJS.Timeout | undefined
let reconnectAttempt = 0
let startSockInFlight: Promise<WASocket> | undefined
let activeSocketInstance: ReturnType<typeof makeWASocket> | undefined
const AUTH_FOLDER = process.env.AUTH_FOLDER ?? 'baileys_auth_native'

const clearReconnectTimer = () => {
	if (reconnectTimer) {
		clearTimeout(reconnectTimer)
		reconnectTimer = undefined
	}
}

// The example now exercises the native-backed auth store explicitly so reconnects reuse one persisted backend.
const loadAuthState = async () => {
	const nativeState = await useNativeAuthState(AUTH_FOLDER)
	return {
		source: 'native' as const,
		...nativeState
	}
}

// Collapse overlapping reconnect attempts into a single exponential-backoff loop.
const scheduleReconnect = (reason: string) => {
	if (reconnectTimer) {
		return
	}

	const delayMs = Math.min(RECONNECT_DELAY_BASE_MS * (2 ** reconnectAttempt), RECONNECT_DELAY_MAX_MS)
	reconnectAttempt += 1
	logger.warn({ reason, reconnectAttempt, delayMs }, 'scheduling reconnect')
	reconnectTimer = setTimeout(() => {
		reconnectTimer = undefined
		void startSock()
	}, delayMs)
}

let rl: readline.Interface | undefined
const getReadline = () => {
	if (!rl) {
		rl = readline.createInterface({ input: process.stdin, output: process.stdout })
	}

	return rl
}
const closeReadline = () => {
	if (rl) {
		rl.close()
		rl = undefined
	}
}
const question = (text: string) => new Promise<string>((resolve) => getReadline().question(text, resolve))

// Smoke-test that the native auth backend persists credentials before the socket flow starts.
const runNativeAuthStateSmoke = async () => {
	const first = await loadAuthState()
	await first.saveCreds()

	const second = await loadAuthState()
	const firstNoiseKey = Buffer.from(first.state.creds.noiseKey.public)
	const secondNoiseKey = Buffer.from(second.state.creds.noiseKey.public)
	if (!firstNoiseKey.equals(secondNoiseKey)) {
		throw new Error('native auth state smoke failed: persisted creds mismatch')
	}

	console.log(`native auth state smoke passed (${first.source})`)
	logger.info({ source: first.source }, 'native auth state smoke passed')
}

// Keep the WhatsApp reply compact while still exposing enough binding diagnostics to confirm the addon in use.
const formatNativeBindingLoadInfoLines = () => {
	const info = getNativeBindingLoadInfo()
	const exportSampleLines = info.exportSample.length ? info.exportSample.map(sample => `> ${sample}`) : ['> none']

	return [
		`> native binding loaded: ${info.loaded}`,
		// `> candidate: ${info.candidate || 'unknown'}`,
		// `> resolved path: ${info.resolvedPath || 'unknown'}`,
		// `> real path: ${info.realPath || 'unknown'}`,
		`> size bytes: ${info.sizeBytes ?? 'unknown'}`,
		`> mtime: ${info.mtimeIso || 'unknown'}`,
		`> export count: ${info.exportCount}`,
		'> export sample:',
		'',
		...exportSampleLines,
		// `> process.moduleLoadList: ${info.moduleLoadListMatches.length ? info.moduleLoadListMatches.join(' | ') : 'no direct entry'}`,
		// `> process.report sharedObjects: ${info.reportSharedObjectMatches.length ? info.reportSharedObjectMatches.join(' | ') : 'not found'}`
	]
}

const getMessageText = (msg: WAMessage) => msg.message?.conversation || msg.message?.extendedTextMessage?.text

const sendExampleText = async (sock: WASocket, remoteJid: string, text: string) => {
	const messageId = generateMessageIDV2(sock.user?.id)
	await sock.sendMessage(remoteJid, { text }, { messageId })
	return messageId
}

const shouldSendExampleReplies = (msg: WAMessage) =>
	doReplies &&
	!msg.key.fromMe &&
	!!msg.key.remoteJid &&
	!isJidNewsletter(msg.key.remoteJid) &&
	(!EXAMPLE_REPLY_JID || msg.key.remoteJid === EXAMPLE_REPLY_JID)

const sendExampleReplyBundle = async (sock: WASocket, msg: WAMessage) => {
	const remoteJid = msg.key.remoteJid
	if (!remoteJid) {
		return
	}

	const startTask = performance.now()
	const pongId = await sendExampleText(sock, remoteJid, `pong ${msg.key.id || 'unknown'}`)
	logger.debug({ id: pongId, orig_id: msg.key.id }, 'replying to message')

	const ms = Math.round(performance.now() - startTask)
	await sendExampleText(sock, remoteJid, `> tarefa concluida em ${ms}ms`)
	await sendExampleText(sock, remoteJid, formatNativeBindingLoadInfoLines().join('\n'))
	await sendExampleText(sock, remoteJid, JSON.stringify(msg, null, 2))
}

const handleExampleTextCommand = async (
	sock: WASocket,
	msg: WAMessage,
	text: string,
	requestId?: string
) => {
	if (text === REQUEST_PLACEHOLDER_COMMAND && !requestId) {
		const messageId = await sock.requestPlaceholderResend(msg.key)
		logger.debug({ id: messageId, key: msg.key }, 'requested placeholder resync')
	}

	if (text === ON_DEMAND_HISTORY_SYNC_COMMAND && msg.messageTimestamp) {
		const messageId = await sock.fetchMessageHistory(50, msg.key, msg.messageTimestamp)
		logger.debug({ id: messageId, key: msg.key }, 'requested on-demand history resync')
	}

	if (shouldSendExampleReplies(msg)) {
		await sendExampleReplyBundle(sock, msg)
	}
}

// start a connection
const startSock = async (): Promise<WASocket> => {
	if (startSockInFlight) {
		return startSockInFlight
	}

	startSockInFlight = (async () => {
		const { state, saveCreds, source } = await loadAuthState()
		logger.info({ source }, 'loaded auth state backend')
		// NOTE: For unit testing purposes only
		if (process.env.ADV_SECRET_KEY) {
			state.creds.advSecretKey = process.env.ADV_SECRET_KEY
		}
		// fetch latest version of WA Web
		const { version, isLatest } = await fetchLatestBaileysVersion()
		logger.debug({ version: version.join('.'), isLatest }, `using latest WA version`)

		const sock = makeWASocket({
			version,
			emitOwnEvents: false,
			logger,
			markOnlineOnConnect: false,
			waWebSocketUrl: process.env.SOCKET_URL ?? DEFAULT_CONNECTION_CONFIG.waWebSocketUrl,
			auth: {
				creds: state.creds,
				/** caching makes the store faster to send/recv messages */
				keys: makeCacheableSignalKeyStore(state.keys, logger),
			},
			msgRetryCounterCache,
			generateHighQualityLinkPreview: true,
			// ignore all broadcast messages -- to receive the same
			// comment the line below out
			// shouldIgnoreJid: jid => isJidBroadcast(jid),
			// implement to handle retries & poll updates
			getMessage
		})
		activeSocketInstance = sock

		// the process function lets you process all events that just occurred
		// efficiently in a batch
		sock.ev.process(
			// events is a map for event name => event data
			async (events) => {
				// something about the connection changed
				// maybe it closed, or we received all offline message or connection opened
				if (events['connection.update']) {
					const update = events['connection.update']
					const { connection, lastDisconnect, qr } = update
					if (connection === 'close') {
						if (sock !== activeSocketInstance) {
							logger.debug('ignoring close from stale socket instance')
							return
						}

						const statusCode = (lastDisconnect?.error as Boom | undefined)?.output?.statusCode
						const isLoggedOut = statusCode === DisconnectReason.loggedOut
						const isReplaced = statusCode === DisconnectReason.connectionReplaced

						// reconnect if not logged out
						if (!isLoggedOut) {
							const reason = isReplaced ? 'connection_replaced' : `disconnect_${statusCode ?? 'unknown'}`
							scheduleReconnect(reason)
						} else {
							logger.fatal('Connection closed. You are logged out.')
							clearReconnectTimer()
						}
					}

					if (connection === 'open') {
						clearReconnectTimer()
						reconnectAttempt = 0
					}

					if (qr) {
						// Pairing code for Web clients
						qrc.generate(qr, { small: true })
						if (usePairingCode && !sock.authState.creds.registered) {
							const phoneNumber = (await question('Please enter your phone number:\n')).trim()
							const code = await sock.requestPairingCode(phoneNumber)
							console.log(`Pairing code: ${code}`)
						}
					}

					logger.debug(update, 'connection update')
				}

				// credentials updated -- save them
				if (events['creds.update']) {
					await saveCreds()
					logger.debug({}, 'creds save triggered')
				}

				if (events['labels.association']) {
					logger.debug(events['labels.association'], 'labels.association event fired')
				}

				if (events['labels.edit']) {
					logger.debug(events['labels.edit'], 'labels.edit event fired')
				}

				if (events['call']) {
					logger.debug(events['call'], 'call event fired')
				}

				// history received
				if (events['messaging-history.set']) {
					const { chats, contacts, messages, isLatest, progress, syncType } = events['messaging-history.set']
					if (syncType === proto.HistorySync.HistorySyncType.ON_DEMAND) {
						logger.debug(messages, 'received on-demand history sync')
					}
					logger.debug(
						{
							contacts: contacts.length,
							chats: chats.length,
							messages: messages.length,
							isLatest,
							progress,
							syncType: syncType?.toString()
						},
						'messaging-history.set event fired'
					)
				}

				// received a new message
				if (events['messages.upsert']) {
					const upsert = events['messages.upsert']
					logger.debug(upsert, 'messages.upsert fired')

					if (upsert.requestId) {
						logger.debug(upsert, 'placeholder request message received')
					}

					if (upsert.type === 'notify') {
						for (const msg of upsert.messages) {
							const text = getMessageText(msg)
							if (!text) {
								continue
							}

							await handleExampleTextCommand(sock, msg, text, upsert.requestId)
						}
					}
				}

				// messages updated like status delivered, message deleted etc.
				if (events['messages.update']) {
					logger.debug(events['messages.update'], 'messages.update fired')

					for (const { key, update } of events['messages.update']) {
						if (update.pollUpdates) {
							const pollCreation: proto.IMessage = {} // get the poll creation message somehow
							if (pollCreation) {
								console.log(
									'got poll update, aggregation: ',
									getAggregateVotesInPollMessage({
										message: pollCreation,
										pollUpdates: update.pollUpdates,
									})
								)
							}
						}
					}
				}

				if (events['message-receipt.update']) {
					logger.debug(events['message-receipt.update'])
				}

				if (events['contacts.upsert']) {
					logger.debug(events['contacts.upsert'])
				}

				if (events['messages.reaction']) {
					logger.debug(events['messages.reaction'])
				}

				if (events['presence.update']) {
					logger.debug(events['presence.update'])
				}

				if (events['chats.update']) {
					logger.debug(events['chats.update'])
				}

				if (events['contacts.update']) {
					for (const contact of events['contacts.update']) {
						if (typeof contact.imgUrl !== 'undefined') {
							const newUrl = contact.imgUrl === null
								? null
								: await sock.profilePictureUrl(contact.id!).catch(() => null)
							logger.debug({ id: contact.id, newUrl }, `contact has a new profile pic`)
						}
					}
				}

				if (events['chats.delete']) {
					logger.debug('chats deleted ', events['chats.delete'])
				}

				if (events['group.member-tag.update']) {
					logger.debug('group member tag update', JSON.stringify(events['group.member-tag.update'], undefined, 2))
				}
			}
		)

		return sock

		async function getMessage(key: WAMessageKey): Promise<WAMessageContent | undefined> {
			// Implement a way to retreive messages that were upserted from messages.upsert
			// up to you
			void key

			// only if store is present
			return proto.Message.create({ conversation: 'test' })
		}
	})().finally(() => {
		startSockInFlight = undefined
	})

	return startSockInFlight
}

const main = async () => {
	if (validateNativeAuthState) {
		await runNativeAuthStateSmoke()
		return
	}

	await startSock()
}

main()
	.catch(error => {
		console.error(error)
		process.exitCode = 1
	})
	.finally(() => {
		closeReadline()
	})
