import { mkdir, writeFile } from 'fs/promises'
import inspector from 'inspector'
import { join } from 'path'
import { performance } from 'perf_hooks'
import P from 'pino'
import { makeLibSignalRepository } from '../src/Signal/libsignal.ts'
import type { SignalRepositoryWithLIDStore } from '../src/Types/Signal.ts'
import type { AuthenticationCreds, SignalDataSet, SignalDataTypeMap, SignalKeyStore } from '../src/Types/Auth.ts'
import { generateSignalPubKey } from '../src/Utils/crypto.ts'
import { addTransactionCapability, initAuthCreds } from '../src/Utils/auth-utils.ts'
import { generateOrGetPreKeys } from '../src/Utils/signal.ts'

const logger = P({ level: 'silent' })

const injectIterations = Number(process.env.BAILEYS_SIGNAL_BENCH_INJECT_ITERATIONS ?? 20)
const singleIterations = Number(process.env.BAILEYS_SIGNAL_BENCH_SINGLE_ITERATIONS ?? 200)
const batchIterations = Number(process.env.BAILEYS_SIGNAL_BENCH_BATCH_ITERATIONS ?? 40)
const batchSize = Number(process.env.BAILEYS_SIGNAL_BENCH_BATCH_SIZE ?? 64)
const recipientCount = Number(process.env.BAILEYS_SIGNAL_BENCH_RECIPIENTS ?? 8)
const injectBatchSize = Number(process.env.BAILEYS_SIGNAL_BENCH_INJECT_BATCH_SIZE ?? 32)
const writeCpuProfile =
	process.env.BAILEYS_SIGNAL_WRITE_PROFILE === '1' || process.env.BAILEYS_SIGNAL_WRITE_PROFILE === 'true'

type Row = {
	name: string
	iterations: number
	workItems: number
	totalMs: number
	avgMs: number
	avgUsPerItem: number
}

type SessionBundle = {
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

type Peer = {
	jid: string
	creds: AuthenticationCreds
	repo: SignalRepositoryWithLIDStore
	bundle: SessionBundle
}

const rows: Row[] = []

const postToInspector = <T>(session: inspector.Session, method: string, params?: object) =>
	new Promise<T>((resolve, reject) => {
		session.post(method, params ?? {}, (error, result) => {
			if (error) {
				reject(error)
				return
			}

			resolve(result as T)
		})
	})

const withOptionalCpuProfile = async <T>(work: () => Promise<T>) => {
	if (!writeCpuProfile) {
		return { result: await work(), profilePath: undefined as string | undefined }
	}

	const session = new inspector.Session()
	session.connect()
	await postToInspector(session, 'Profiler.enable')
	await postToInspector(session, 'Profiler.start')

	try {
		const result = await work()
		const { profile } = await postToInspector<{ profile: object }>(session, 'Profiler.stop')
		const profileDir = join(process.cwd(), 'profiles')
		const profilePath = join(profileDir, `signal-hotpaths-${Date.now()}.cpuprofile`)
		await mkdir(profileDir, { recursive: true })
		await writeFile(profilePath, JSON.stringify(profile))
		return { result, profilePath }
	} finally {
		session.disconnect()
	}
}

const makeMemorySignalStore = (): SignalKeyStore => {
	const buckets = new Map<keyof SignalDataTypeMap, Map<string, SignalDataTypeMap[keyof SignalDataTypeMap]>>()

	const getBucket = <T extends keyof SignalDataTypeMap>(type: T) => {
		const existing = buckets.get(type)
		if (existing) {
			return existing as Map<string, SignalDataTypeMap[T]>
		}

		const created = new Map<string, SignalDataTypeMap[T]>()
		buckets.set(type, created as Map<string, SignalDataTypeMap[keyof SignalDataTypeMap]>)
		return created
	}

	return {
		async get(type, ids) {
			const bucket = getBucket(type)
			const out: { [id: string]: SignalDataTypeMap[typeof type] } = {}
			for (const id of ids) {
				const value = bucket.get(id)
				if (typeof value !== 'undefined') {
					out[id] = value
				}
			}

			return out
		},
		async set(data: SignalDataSet) {
			for (const type of Object.keys(data) as (keyof SignalDataTypeMap)[]) {
				const entries = data[type]
				if (!entries) {
					continue
				}

				const bucket = getBucket(type)
				for (const [id, value] of Object.entries(entries)) {
					if (value === null) {
						bucket.delete(id)
					} else {
						bucket.set(id, value as SignalDataTypeMap[typeof type])
					}
				}
			}
		},
		async clear() {
			buckets.clear()
		}
	}
}

const transactionOpts = {
	maxCommitRetries: 1,
	delayBetweenTriesMs: 1
}

const createPeer = async (jid: string): Promise<Peer> => {
	const creds: AuthenticationCreds = { ...initAuthCreds() }
	const baseStore = makeMemorySignalStore()
	const { newPreKeys, lastPreKeyId } = generateOrGetPreKeys(creds, 1)
	await baseStore.set({ 'pre-key': newPreKeys })
	creds.nextPreKeyId = Math.max(lastPreKeyId + 1, creds.nextPreKeyId)
	creds.firstUnuploadedPreKeyId = Math.max(lastPreKeyId + 1, creds.firstUnuploadedPreKeyId)

	const keys = addTransactionCapability(baseStore, logger, transactionOpts)
	const repo = makeLibSignalRepository({ creds, keys }, logger)

	const [preKeyIdRaw, preKey] = Object.entries(newPreKeys)[0]!
	const preKeyId = Number(preKeyIdRaw)

	return {
		jid,
		creds,
		repo,
		bundle: {
			registrationId: creds.registrationId,
			identityKey: Buffer.from(generateSignalPubKey(creds.signedIdentityKey.public)),
			signedPreKey: {
				keyId: creds.signedPreKey.keyId,
				publicKey: Buffer.from(generateSignalPubKey(creds.signedPreKey.keyPair.public)),
				signature: Buffer.from(creds.signedPreKey.signature)
			},
			preKey: {
				keyId: preKeyId,
				publicKey: Buffer.from(generateSignalPubKey(preKey!.public))
			}
		}
	}
}

const benchmark = async (name: string, iterations: number, workItems: number, fn: (iteration: number) => Promise<void>) => {
	const started = performance.now()
	for (let i = 0; i < iterations; i += 1) {
		await fn(i)
	}

	const totalMs = performance.now() - started
	rows.push({
		name,
		iterations,
		workItems,
		totalMs,
		avgMs: totalMs / iterations,
		avgUsPerItem: (totalMs * 1000) / (iterations * workItems)
	})
}

const establishBidirectionalSession = async (left: Peer, right: Peer) => {
	await left.repo.injectE2ESession({
		jid: right.jid,
		session: right.bundle
	})
	await right.repo.injectE2ESession({
		jid: left.jid,
		session: left.bundle
	})

	const leftToRight = await left.repo.encryptMessage({
		jid: right.jid,
		data: Buffer.from('warmup-left-to-right')
	})
	await right.repo.decryptMessage({
		jid: left.jid,
		type: leftToRight.type,
		ciphertext: leftToRight.ciphertext
	})

	const rightToLeft = await right.repo.encryptMessage({
		jid: left.jid,
		data: Buffer.from('warmup-right-to-left')
	})
	await left.repo.decryptMessage({
		jid: right.jid,
		type: rightToLeft.type,
		ciphertext: rightToLeft.ciphertext
	})
}

const createRecipients = async (count: number, prefix: string) => {
	const peers: Peer[] = []
	for (let i = 0; i < count; i += 1) {
		peers.push(await createPeer(`${prefix}-${i}@s.whatsapp.net`))
	}

	return peers
}

const buildPayloads = (count: number, prefix: string) =>
	Array.from({ length: count }, (_, index) => Buffer.from(`${prefix}-${index}`))

const runBenchmarks = async () => {
	const injectSender = await createPeer('inject-sender@s.whatsapp.net')
	const injectBatches: { jid: string; session: SessionBundle }[][] = []
	for (let i = 0; i < injectIterations; i += 1) {
		const recipients = await createRecipients(injectBatchSize, `inject-recipient-${i}`)
		injectBatches.push(recipients.map(recipient => ({ jid: recipient.jid, session: recipient.bundle })))
	}

	await benchmark('injectE2ESessions[32]', injectIterations, injectBatchSize, async iteration => {
		await injectSender.repo.injectE2ESessions!(injectBatches[iteration]!)
	})

	const encryptSender = await createPeer('encrypt-sender@s.whatsapp.net')
	const encryptRecipients = await createRecipients(recipientCount, 'encrypt-recipient')
	for (const recipient of encryptRecipients) {
		await establishBidirectionalSession(encryptSender, recipient)
	}

	const singleRecipient = encryptRecipients[0]!
	const singlePayloads = buildPayloads(singleIterations, 'encrypt-single')
	await benchmark('encryptMessage[msg]', singleIterations, 1, async iteration => {
		await encryptSender.repo.encryptMessage({
			jid: singleRecipient.jid,
			data: singlePayloads[iteration]!
		})
	})

	const mixedBatchTemplate = buildPayloads(batchSize, 'encrypt-batch').map((data, index) => ({
		jid: encryptRecipients[index % encryptRecipients.length]!.jid,
		data
	}))
	await benchmark(`encryptMessagesBatch[${batchSize}][${recipientCount} recipients]`, batchIterations, batchSize, async () => {
		await encryptSender.repo.encryptMessagesBatch!(mixedBatchTemplate)
	})

	const decryptSender = await createPeer('decrypt-sender@s.whatsapp.net')
	const decryptRecipient = await createPeer('decrypt-recipient@s.whatsapp.net')
	await establishBidirectionalSession(decryptSender, decryptRecipient)

	const decryptSingleInputs = []
	for (let i = 0; i < singleIterations; i += 1) {
		const encrypted = await decryptSender.repo.encryptMessage({
			jid: decryptRecipient.jid,
			data: Buffer.from(`decrypt-single-${i}`)
		})
		decryptSingleInputs.push(encrypted)
	}

	await benchmark('decryptMessage[msg]', singleIterations, 1, async iteration => {
		const encrypted = decryptSingleInputs[iteration]!
		await decryptRecipient.repo.decryptMessage({
			jid: decryptSender.jid,
			type: encrypted.type,
			ciphertext: encrypted.ciphertext
		})
	})

	const decryptBatchInputs = []
	for (let iteration = 0; iteration < batchIterations; iteration += 1) {
		const items = buildPayloads(batchSize, `decrypt-batch-${iteration}`).map(data => ({
			jid: decryptRecipient.jid,
			data
		}))
		const encryptedBatch = await decryptSender.repo.encryptMessagesBatch!(items)
		decryptBatchInputs.push(
			encryptedBatch.map(item => ({
				jid: decryptSender.jid,
				type: item.type,
				ciphertext: item.ciphertext
			}))
		)
	}

	await benchmark(`decryptMessagesBatch[${batchSize}]`, batchIterations, batchSize, async iteration => {
		await decryptRecipient.repo.decryptMessagesBatch!(decryptBatchInputs[iteration]!)
	})

	const groupSender = await createPeer('group-sender@s.whatsapp.net')
	const groupRecipient = await createPeer('group-recipient@s.whatsapp.net')
	const groupJid = 'benchmark-group@g.us'
	const initialGroupMessage = await groupSender.repo.encryptGroupMessage({
		group: groupJid,
		meId: groupSender.jid,
		data: Buffer.from('group-initial')
	})

	await groupRecipient.repo.processSenderKeyDistributionMessage({
		item: {
			groupId: groupJid,
			axolotlSenderKeyDistributionMessage: initialGroupMessage.senderKeyDistributionMessage
		},
		authorJid: groupSender.jid
	})
	await groupRecipient.repo.decryptGroupMessage({
		group: groupJid,
		authorJid: groupSender.jid,
		msg: initialGroupMessage.ciphertext
	})

	const groupDecryptInputs = []
	for (let iteration = 0; iteration < batchIterations; iteration += 1) {
		const batch = []
		for (let index = 0; index < batchSize; index += 1) {
			const encrypted = await groupSender.repo.encryptGroupMessage({
				group: groupJid,
				meId: groupSender.jid,
				data: Buffer.from(`group-${iteration}-${index}`)
			})
			batch.push(encrypted.ciphertext)
		}
		groupDecryptInputs.push(batch)
	}

	await benchmark(`decryptGroupMessage[${batchSize}]`, batchIterations, batchSize, async iteration => {
		for (const ciphertext of groupDecryptInputs[iteration]!) {
			await groupRecipient.repo.decryptGroupMessage({
				group: groupJid,
				authorJid: groupSender.jid,
				msg: ciphertext
			})
		}
	})
}

const { profilePath } = await withOptionalCpuProfile(runBenchmarks)

console.log('Signal Hot Path Benchmark')
console.log(`inject_batch_size | ${injectBatchSize}`)
console.log(`recipients | ${recipientCount}`)
console.log(`batch_size | ${batchSize}`)
console.log('name | iterations | work_items | total_ms | avg_ms | avg_us_per_item')
for (const row of rows) {
	console.log(
		`${row.name} | ${row.iterations} | ${row.workItems} | ${row.totalMs.toFixed(2)} | ${row.avgMs.toFixed(2)} | ${row.avgUsPerItem.toFixed(2)}`
	)
}

if (profilePath) {
	console.log(`cpu_profile | ${profilePath}`)
}
