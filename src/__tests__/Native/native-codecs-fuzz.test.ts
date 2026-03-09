import * as constants from '../../WABinary/constants'
import { proto } from '../../../WAProto/index.js'
import { BAILEYS_NATIVE } from '../../Native/baileys-native'

const native = BAILEYS_NATIVE

const randInt = (max: number) => Math.floor(Math.random() * max)

const randomAscii = (min: number, max: number) => {
	const len = min + randInt(Math.max(1, max - min + 1))
	let out = ''
	for (let i = 0; i < len; i += 1) {
		out += String.fromCharCode(97 + randInt(26))
	}
	return out
}

const randomBytes = (maxLen: number) => {
	const len = randInt(maxLen + 1)
	const out = Buffer.allocUnsafe(len)
	for (let i = 0; i < len; i += 1) {
		out[i] = randInt(256)
	}
	return out
}

const randomAuthValue = (depth = 0): unknown => {
	if (depth > 2) {
		const leafRoll = randInt(5)
		if (leafRoll === 0) return null
		if (leafRoll === 1) return randomAscii(0, 16)
		if (leafRoll === 2) return randInt(10_000)
		if (leafRoll === 3) return randInt(2) === 0
		return randomBytes(24)
	}

	const roll = randInt(8)
	if (roll === 0) return null
	if (roll === 1) return randomAscii(0, 16)
	if (roll === 2) return randInt(10_000)
	if (roll === 3) return randInt(2) === 0
	if (roll === 4) return randomBytes(32)
	if (roll === 5) {
		const len = randInt(6)
		return Array.from({ length: len }, () => randomAuthValue(depth + 1))
	}
	const obj: Record<string, unknown> = {}
	const fields = 1 + randInt(5)
	for (let i = 0; i < fields; i += 1) {
		obj[randomAscii(1, 8)] = randomAuthValue(depth + 1)
	}
	return obj
}

const normalizeAuthValue = (value: unknown): unknown => {
	if (value === null || typeof value !== 'object') {
		return value
	}
	if (Buffer.isBuffer(value)) {
		return Buffer.from(value)
	}
	if (value instanceof Uint8Array) {
		return Buffer.from(value)
	}
	if (Array.isArray(value)) {
		return value.map(item => normalizeAuthValue(item))
	}
	const out: Record<string, unknown> = {}
	for (const [k, v] of Object.entries(value)) {
		out[k] = normalizeAuthValue(v)
	}
	return out
}

type FuzzNode = {
	tag: string
	attrs: Record<string, string>
	content?: FuzzNode[] | string | Uint8Array
}

const randomBinaryNode = (): FuzzNode => {
	const msgId = `${Date.now()}-${randInt(10_000)}`
	const body = randomAscii(0, 40)
	return {
		tag: 'message',
		attrs: {
			to: `${55_000_000_000 + randInt(99_999_999)}@s.whatsapp.net`,
			id: msgId,
			type: 'text'
		},
		content: [
			{
				tag: 'body',
				attrs: {},
				content: body
			}
		]
	}
}

describe('native codec fuzz-like regression', () => {
	it('keeps encodeAuthValue/decodeAuthValue stable over randomized inputs', () => {
		for (let i = 0; i < 250; i += 1) {
			const value = randomAuthValue()
			const encoded = native.encodeAuthValue!(value)
			const decoded = native.decodeAuthValue!(encoded)
			expect(normalizeAuthValue(decoded)).toEqual(normalizeAuthValue(value))
		}
	})

	it('keeps protobuf raw/padded batch decoders stable over randomized messages', () => {
		const initOk = native.initProtoMessageCodec!(
			message => proto.Message.encode(proto.Message.fromObject(message as object)),
			data => proto.Message.decode(data)
		)
		expect(initOk).toBe(true)

		for (let round = 0; round < 60; round += 1) {
			const size = 1 + randInt(8)
			const messages = Array.from({ length: size }, () => ({ conversation: randomAscii(0, 64) }))
			const encoded = messages.map(msg => Buffer.from(proto.Message.encode(proto.Message.fromObject(msg)).finish()))
			const padded = messages.map(msg => native.encodeProtoMessageWithPad!(msg, 8))

			const rawDecoded = native.decodeProtoMessagesRawBatch!(encoded) as unknown[]
			const paddedDecoded = native.decodeProtoMessagesFromPaddedBatch!(padded) as unknown[]
			expect(rawDecoded).toHaveLength(size)
			expect(paddedDecoded).toHaveLength(size)

			for (let i = 0; i < size; i += 1) {
				const rawReEncoded = Buffer.from(proto.Message.encode(proto.Message.fromObject(rawDecoded[i] as object)).finish())
				const paddedReEncoded = Buffer.from(proto.Message.encode(proto.Message.fromObject(paddedDecoded[i] as object)).finish())
				expect(rawReEncoded).toEqual(encoded[i])
				expect(paddedReEncoded).toEqual(encoded[i])
			}
		}
	})

	it('keeps wa binary codec roundtrip stable over randomized node trees', () => {
		const initOk = native.initWABinaryCodec!(constants)
		expect(initOk).toBe(true)

		let successfulRoundtrips = 0
		for (let i = 0; i < 120; i += 1) {
			const node = randomBinaryNode()
			const encoded = native.encodeWABinaryNode!(node, false)
			const decoded = native.decodeWABinaryNode!(encoded, 0)
			expect(decoded.nextIndex).toBe(encoded.length)

			const reEncoded = native.encodeWABinaryNode!(decoded.node, false)
			expect(reEncoded).toEqual(encoded)
			successfulRoundtrips += 1
		}
		expect(successfulRoundtrips).toBeGreaterThan(20)
	})
})
