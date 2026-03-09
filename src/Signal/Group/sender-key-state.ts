import { type NativeMessageKeyStore, requireNativeExport } from '../../Native/baileys-native'
import { SenderChainKey } from './sender-chain-key'
import { SenderMessageKey } from './sender-message-key'

interface SenderChainKeyStructure {
	iteration: number
	seed: Uint8Array
}

interface SenderSigningKeyStructure {
	public: Uint8Array
	private?: Uint8Array
}

interface SenderMessageKeyStructure {
	iteration: number
	seed: Uint8Array
}

interface SenderKeyStateStructure {
	senderKeyId: number
	senderChainKey: SenderChainKeyStructure
	senderSigningKey: SenderSigningKeyStructure
	senderMessageKeys: SenderMessageKeyStructure[]
}

export class SenderKeyState {
	private readonly MAX_MESSAGE_KEYS = 2000
	private readonly senderKeyStateStructure: SenderKeyStateStructure
	private readonly nativeMessageKeyStore: NativeMessageKeyStore
	private readonly onMutate?: () => void
	private messageKeysDirty = false

	constructor(
		id?: number | null,
		iteration?: number | null,
		chainKey?: Uint8Array | null | string,
		signatureKeyPair?: { public: Uint8Array | string; private: Uint8Array | string } | null,
		signatureKeyPublic?: Uint8Array | string | null,
		signatureKeyPrivate?: Uint8Array | string | null,
		senderKeyStateStructure?: SenderKeyStateStructure | null,
		onMutate?: () => void
	) {
		this.onMutate = onMutate

		if (senderKeyStateStructure) {
			this.senderKeyStateStructure = {
				...senderKeyStateStructure,
				senderMessageKeys: Array.isArray(senderKeyStateStructure.senderMessageKeys)
					? senderKeyStateStructure.senderMessageKeys
					: []
			}
		} else {
			if (signatureKeyPair) {
				signatureKeyPublic = signatureKeyPair.public
				signatureKeyPrivate = signatureKeyPair.private
			}

			this.senderKeyStateStructure = {
				senderKeyId: id || 0,
				senderChainKey: {
					iteration: iteration || 0,
					seed: Buffer.from(chainKey || [])
				},
				senderSigningKey: {
					public: Buffer.from(signatureKeyPublic || []),
					private: Buffer.from(signatureKeyPrivate || [])
				},
				senderMessageKeys: []
			}
		}

		const NativeMessageKeyStoreCtor = requireNativeExport('NativeMessageKeyStore')
		this.nativeMessageKeyStore = new NativeMessageKeyStoreCtor(
			this.senderKeyStateStructure.senderMessageKeys,
			this.MAX_MESSAGE_KEYS
		)
	}

	private markMutated(messageKeysChanged = false): void {
		if (messageKeysChanged) {
			this.messageKeysDirty = true
		}

		this.onMutate?.()
	}

	public getKeyId(): number {
		return this.senderKeyStateStructure.senderKeyId
	}

	public getSenderChainKey(): SenderChainKey {
		return new SenderChainKey(
			this.senderKeyStateStructure.senderChainKey.iteration,
			this.senderKeyStateStructure.senderChainKey.seed
		)
	}

	public setSenderChainKey(chainKey: SenderChainKey): void {
		this.senderKeyStateStructure.senderChainKey = {
			iteration: chainKey.getIteration(),
			seed: chainKey.getSeed()
		}
		this.markMutated()
	}

	public getSigningKeyPublic(): Buffer {
		const publicKey = Buffer.from(this.senderKeyStateStructure.senderSigningKey.public)

		if (publicKey.length === 32) {
			const fixed = Buffer.alloc(33)
			fixed[0] = 0x05
			publicKey.copy(fixed, 1)
			return fixed
		}

		return publicKey
	}

	public getSigningKeyPrivate(): Buffer | undefined {
		const privateKey = this.senderKeyStateStructure.senderSigningKey.private

		return Buffer.from(privateKey || [])
	}

	public hasSenderMessageKey(iteration: number): boolean {
		return this.nativeMessageKeyStore.has(iteration)
	}

	public addSenderMessageKey(senderMessageKey: SenderMessageKey): void {
		this.nativeMessageKeyStore.add(senderMessageKey.getIteration(), senderMessageKey.getSeed(), this.MAX_MESSAGE_KEYS)
		this.markMutated(true)
	}

	public removeSenderMessageKey(iteration: number): SenderMessageKey | null {
		const nativeMessageKey = this.nativeMessageKeyStore.remove(iteration)
		if (nativeMessageKey) {
			this.markMutated(true)
			return new SenderMessageKey(nativeMessageKey.iteration, nativeMessageKey.seed)
		}

		return null
	}

	public serializeRecordForStorage(): Buffer | undefined {
		if (!this.nativeMessageKeyStore.encodeSenderKeyRecord) {
			return undefined
		}

		const { senderKeyId, senderChainKey, senderSigningKey } = this.senderKeyStateStructure
		return this.nativeMessageKeyStore.encodeSenderKeyRecord(
			senderKeyId,
			senderChainKey.iteration,
			senderChainKey.seed,
			senderSigningKey.public,
			senderSigningKey.private
		)
	}

	public getStructure(): SenderKeyStateStructure {
		if (this.messageKeysDirty) {
			this.senderKeyStateStructure.senderMessageKeys = this.nativeMessageKeyStore.toArray()
			this.messageKeysDirty = false
		}

		return this.senderKeyStateStructure
	}
}
