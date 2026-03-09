import { createHmac } from 'crypto'
import { Curve, deriveSignalSecrets, generateSignalKeyPairRaw } from '../../Utils/crypto'

describe('signal crypto compatibility fallbacks', () => {
	it('generates libsignal-compatible key pairs for sign/verify', () => {
		const pair = Curve.generateKeyPair()
		const message = Buffer.from('baileys-signal-compat')
		const signature = Curve.sign(pair.private, message)

		expect(pair.private).toHaveLength(32)
		expect(pair.public).toHaveLength(32)
		expect(signature).toHaveLength(64)
		expect(Curve.verify(pair.public, message, signature)).toBe(true)
	})

	it('keeps raw sender signing key format compatible', () => {
		const pair = generateSignalKeyPairRaw()

		expect(pair.privKey).toHaveLength(32)
		expect(pair.pubKey).toHaveLength(33)
	})

	it('derives sender secrets with HKDF-SHA256 compatible output', () => {
		const input = Buffer.alloc(32, 3)
		const salt = Buffer.alloc(32)
		const info = Buffer.from('WhisperGroup')
		const actual = deriveSignalSecrets(input, salt, info, 2)

		const prk = createHmac('sha256', salt).update(input).digest()
		const expected: Buffer[] = []
		let previous = Buffer.alloc(0)

		for (let i = 1; i <= 2; i += 1) {
			previous = createHmac('sha256', prk)
				.update(previous)
				.update(info)
				.update(Buffer.from([i]))
				.digest()
			expected.push(previous)
		}

		expect(actual).toHaveLength(2)
		expect(Buffer.from(actual[0]!)).toEqual(expected[0])
		expect(Buffer.from(actual[1]!)).toEqual(expected[1])
	})
})
