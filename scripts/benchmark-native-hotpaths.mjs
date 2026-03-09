import { createRequire } from 'module'
import { proto } from '../WAProto/index.js'

const require = createRequire(import.meta.url)

const native = require('../build/Release/baileys_native.node')

const toMs = ns => Number(ns) / 1e6

const benchmark = (name, iterations, fn) => {
	const started = process.hrtime.bigint()
	for (let i = 0; i < iterations; i += 1) {
		fn(i)
	}
	const elapsed = process.hrtime.bigint() - started
	return {
		name,
		iterations,
		totalMs: toMs(elapsed),
		avgUs: (toMs(elapsed) * 1000) / iterations
	}
}

native.initProtoMessageCodec(
	message => proto.Message.encode(proto.Message.fromObject(message)),
	data => proto.Message.decode(data)
)

const authPayload = {
	id: 'bench',
	counter: 42,
	flags: [true, false, true],
	data: Buffer.alloc(256, 0xab),
	nested: { a: 'x', b: 7 }
}
const authEncoded = native.encodeAuthValue(authPayload)

const protoMessages = Array.from({ length: 64 }, (_, i) => ({
	conversation: `hello-${i}`
}))
const protoEncoded = protoMessages.map(msg => Buffer.from(proto.Message.encode(proto.Message.fromObject(msg)).finish()))
const protoPadded = protoMessages.map(msg => native.encodeProtoMessageWithPad(msg, 8))

const usyncFixture = {
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
							attrs: { jid: '5511999999999@s.whatsapp.net' },
							content: [
								{ tag: 'contact', attrs: { type: 'in' }, content: [] },
								{ tag: 'lid', attrs: { val: '5511999999999@lid' }, content: [] },
								{
									tag: 'status',
									attrs: { t: '1' },
									content: 'ok'
								}
							]
						}
					]
				}
			]
		}
	]
}

const rows = [
	benchmark('encodeAuthValue', 25_000, () => {
		native.encodeAuthValue(authPayload)
	}),
	benchmark('decodeAuthValue', 25_000, () => {
		native.decodeAuthValue(authEncoded)
	}),
	benchmark('decodeProtoMessagesRawBatch[64]', 5_000, () => {
		native.decodeProtoMessagesRawBatch(protoEncoded)
	}),
	benchmark('decodeProtoMessagesFromPaddedBatch[64]', 5_000, () => {
		native.decodeProtoMessagesFromPaddedBatch(protoPadded)
	}),
	benchmark('parseUSyncQueryResultFast', 20_000, () => {
		native.parseUSyncQueryResultFast(usyncFixture, ['contact', 'lid', 'status'])
	})
]

console.log('Native Hot Path Benchmark')
console.log('name | iterations | total_ms | avg_us')
for (const row of rows) {
	console.log(`${row.name} | ${row.iterations} | ${row.totalMs.toFixed(2)} | ${row.avgUs.toFixed(2)}`)
}
