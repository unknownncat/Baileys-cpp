import { curve } from 'libsignal'
const { calculateSignature, verifySignature } = curve
import { proto } from '../../../WAProto/index.js'
import { requireNativeExport } from '../../Native/baileys-native'
import { CiphertextMessage } from './ciphertext-message'

const nativeSplitSenderKeySerializedFast = requireNativeExport('splitSenderKeySerializedFast')

interface SenderKeyMessageStructure {
	id: number
	iteration: number
	ciphertext: string | Buffer
}

export class SenderKeyMessage extends CiphertextMessage {
	private readonly SIGNATURE_LENGTH = 64
	private readonly messageVersion: number
	private readonly keyId: number
	private readonly iteration: number
	private readonly ciphertext: Uint8Array
	private readonly signature: Uint8Array
	private readonly serialized: Uint8Array

	constructor(
		keyId?: number | null,
		iteration?: number | null,
		ciphertext?: Uint8Array | null,
		signatureKey?: Uint8Array | null,
		serialized?: Uint8Array | null
	) {
		super()

		if (serialized) {
			const parsedSerialized = nativeSplitSenderKeySerializedFast(serialized, this.SIGNATURE_LENGTH)
			if (
				!parsedSerialized ||
				typeof parsedSerialized.version !== 'number' ||
				!(parsedSerialized.message instanceof Uint8Array) ||
				!(parsedSerialized.signature instanceof Uint8Array)
			) {
				throw new Error('native splitSenderKeySerializedFast returned invalid payload')
			}

			const senderKeyMessage = proto.SenderKeyMessage.decode(
				parsedSerialized.message
			).toJSON() as SenderKeyMessageStructure

			this.serialized = serialized
			this.messageVersion = (parsedSerialized.version & 0xff) >> 4
			this.keyId = senderKeyMessage.id
			this.iteration = senderKeyMessage.iteration
			this.ciphertext =
				typeof senderKeyMessage.ciphertext === 'string'
					? Buffer.from(senderKeyMessage.ciphertext, 'base64')
					: senderKeyMessage.ciphertext
			this.signature = parsedSerialized.signature
		} else {
			const version = (((this.CURRENT_VERSION << 4) | this.CURRENT_VERSION) & 0xff) % 256
			const ciphertextBuffer = Buffer.from(ciphertext!)
			const message = proto.SenderKeyMessage.encode(
				proto.SenderKeyMessage.create({
					id: keyId!,
					iteration: iteration!,
					ciphertext: ciphertextBuffer
				})
			).finish()

			const signature = this.getSignature(signatureKey!, Buffer.concat([Buffer.from([version]), message]))

			this.serialized = Buffer.concat([Buffer.from([version]), message, Buffer.from(signature)])
			this.messageVersion = this.CURRENT_VERSION
			this.keyId = keyId!
			this.iteration = iteration!
			this.ciphertext = ciphertextBuffer
			this.signature = signature
		}
	}

	public getKeyId(): number {
		return this.keyId
	}

	public getIteration(): number {
		return this.iteration
	}

	public getCipherText(): Uint8Array {
		return this.ciphertext
	}

	public verifySignature(signatureKey: Uint8Array): void {
		const part1 = this.serialized.slice(0, this.serialized.length - this.SIGNATURE_LENGTH)
		const part2 = this.serialized.slice(-1 * this.SIGNATURE_LENGTH)
		const res = verifySignature(signatureKey, part1, part2)
		if (!res) throw new Error('Invalid signature!')
	}

	private getSignature(signatureKey: Uint8Array, serialized: Uint8Array): Uint8Array {
		return Buffer.from(calculateSignature(signatureKey, serialized))
	}

	public serialize(): Uint8Array {
		return this.serialized
	}

	public getType(): number {
		return 4
	}
}
