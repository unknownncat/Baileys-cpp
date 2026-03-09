import { proto } from '../../../WAProto/index.js'
import { BAILEYS_NATIVE } from '../../Native/baileys-native'

const native = BAILEYS_NATIVE

describe('native API parity golden', () => {
	it('keeps auth value roundtrip and sync contract', () => {
		const input = {
			counter: 7,
			label: 'state',
			ok: true,
			payload: Buffer.from([1, 2, 3]),
			items: [null, 'x', 9, Buffer.from([9, 8, 7])]
		}

		const encoded = native.encodeAuthValue!(input)
		expect(encoded).toBeInstanceOf(Buffer)
		expect(encoded).not.toBeInstanceOf(Promise)

		const decoded = native.decodeAuthValue!(encoded) as typeof input
		expect(decoded.counter).toBe(7)
		expect(decoded.label).toBe('state')
		expect(decoded.ok).toBe(true)
		expect(decoded.payload).toEqual(Buffer.from([1, 2, 3]))
		expect(decoded.items[3]).toEqual(Buffer.from([9, 8, 7]))
	})

	it('keeps error messages for key guard clauses', () => {
		expect(() => (native.encodeAuthValue as unknown as () => Buffer)()).toThrow(
			'encodeAuthValue(value) requires value'
		)
		expect(() => (native.parseGroupParticipantStubsFast as unknown as (v: unknown) => unknown)(1)).toThrow(
			'parseGroupParticipantStubsFast(items) expects items array'
		)
		expect(() => (native.initProtoMessageCodec as unknown as (a: unknown) => unknown)({})).toThrow(
			'initProtoMessageCodec(encodeFn, decodeFn) expects 2 functions'
		)
		expect(() => (native.decodePollVoteMessageFast as unknown as () => unknown)()).toThrow(
			'decodePollLikeMessage(encPayload, encIv, msgId, creatorJid, responderJid, encKey) requires 6 arguments'
		)
		expect(() => (native.decodeEventResponseMessageFast as unknown as () => unknown)()).toThrow(
			'decodePollLikeMessage(encPayload, encIv, msgId, creatorJid, responderJid, encKey) requires 6 arguments'
		)

		const encoded = native.encodeAuthValue!({ a: 1 })
		const trailing = Buffer.concat([encoded, Buffer.from([0xff])])
		expect(() => native.decodeAuthValue!(trailing)).toThrow('decodeAuthValue trailing bytes')
	})

	it('keeps proto batch decode parity with reference protobufjs codec', () => {
		const initOk = native.initProtoMessageCodec!(
			message => proto.Message.encode(proto.Message.fromObject(message as object)),
			data => proto.Message.decode(data)
		)
		expect(initOk).toBe(true)

		const messages: proto.IMessage[] = [
			{ conversation: 'alpha' },
			{ conversation: 'beta' },
			{ imageMessage: { caption: 'caption' } }
		]

		const encoded = messages.map(msg => Buffer.from(proto.Message.encode(proto.Message.fromObject(msg)).finish()))
		const decodedRaw = native.decodeProtoMessagesRawBatch!(encoded) as unknown[]
		expect(decodedRaw).toHaveLength(encoded.length)

		for (let i = 0; i < decodedRaw.length; i += 1) {
			const reEncoded = Buffer.from(proto.Message.encode(proto.Message.fromObject(decodedRaw[i] as object)).finish())
			expect(reEncoded).toEqual(encoded[i])
		}

		const padded = messages.map(msg => native.encodeProtoMessageWithPad!(msg, 8))
		const decodedPadded = native.decodeProtoMessagesFromPaddedBatch!(padded) as unknown[]
		expect(decodedPadded).toHaveLength(messages.length)
		for (let i = 0; i < decodedPadded.length; i += 1) {
			const reEncoded = Buffer.from(proto.Message.encode(proto.Message.fromObject(decodedPadded[i] as object)).finish())
			expect(reEncoded).toEqual(encoded[i])
		}
	})

	it('keeps usync parser output shape', () => {
		const resultNode = {
			attrs: { type: 'result' },
			content: [
				{
					tag: 'usync',
					attrs: {},
					content: [
						{
							tag: 'list',
							attrs: {},
							content: [
								{
									tag: 'user',
									attrs: { jid: '123456@s.whatsapp.net' },
									content: [
										{ tag: 'contact', attrs: { type: 'in' }, content: [] },
										{ tag: 'lid', attrs: { val: '123456@lid' }, content: [] }
									]
								}
							]
						}
					]
				}
			]
		}

		const out = native.parseUSyncQueryResultFast!(resultNode, ['contact', 'lid'])
		expect(out).toEqual({
			list: [{ id: '123456@s.whatsapp.net', contact: true, lid: '123456@lid' }],
			sideList: []
		})
	})
})
