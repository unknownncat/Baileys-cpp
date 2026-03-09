import { requireNativeExport } from '../../Native/baileys-native'
import { SenderKeyState } from './sender-key-state'

export interface SenderKeyStateStructure {
	senderKeyId: number
	senderChainKey: {
		iteration: number
		seed: Uint8Array
	}
	senderSigningKey: {
		public: Uint8Array
		private?: Uint8Array
	}
	senderMessageKeys: Array<{
		iteration: number
		seed: Uint8Array
	}>
}

// Sender-key records now use a compact native binary format instead of JSON blobs.
const SENDER_KEY_MAGIC = Buffer.from([0x42, 0x53, 0x4b, 0x52]) // BSKR
const SENDER_KEY_VERSION = 1
const nativeEncodeSenderKeyStates = requireNativeExport('encodeSenderKeyStates')
const nativeDecodeSenderKeyStates = requireNativeExport('decodeSenderKeyStates')

const isBinarySenderKeyData = (data: Uint8Array) =>
	data.length >= 7 &&
	data[0] === SENDER_KEY_MAGIC[0] &&
	data[1] === SENDER_KEY_MAGIC[1] &&
	data[2] === SENDER_KEY_MAGIC[2] &&
	data[3] === SENDER_KEY_MAGIC[3] &&
	data[4] === SENDER_KEY_VERSION

export class SenderKeyRecord {
	private readonly MAX_STATES = 5
	private readonly senderKeyStates: SenderKeyState[] = []
	private serializedCache?: Buffer
	private dirty = true

	constructor(serialized?: SenderKeyStateStructure[], serializedCache?: Uint8Array) {
		if (serialized) {
			for (const structure of serialized) {
				this.senderKeyStates.push(new SenderKeyState(null, null, null, null, null, null, structure, () => this.markDirty()))
			}
		}

		if (serializedCache) {
			this.serializedCache = Buffer.from(serializedCache)
			this.dirty = false
		}
	}

	private markDirty(): void {
		this.serializedCache = undefined
		this.dirty = true
	}

	public isEmpty(): boolean {
		return this.senderKeyStates.length === 0
	}

	public getSenderKeyState(keyId?: number): SenderKeyState | undefined {
		if (keyId === undefined && this.senderKeyStates.length) {
			return this.senderKeyStates[this.senderKeyStates.length - 1]
		}

		return this.senderKeyStates.find(state => state.getKeyId() === keyId)
	}

	public addSenderKeyState(id: number, iteration: number, chainKey: Uint8Array, signatureKey: Uint8Array): void {
		this.senderKeyStates.push(new SenderKeyState(id, iteration, chainKey, null, signatureKey, null, null, () => this.markDirty()))
		if (this.senderKeyStates.length > this.MAX_STATES) {
			this.senderKeyStates.shift()
		}

		this.markDirty()
	}

	public setSenderKeyState(
		id: number,
		iteration: number,
		chainKey: Uint8Array,
		keyPair: { public: Uint8Array; private: Uint8Array }
	): void {
		this.senderKeyStates.length = 0
		this.senderKeyStates.push(new SenderKeyState(id, iteration, chainKey, keyPair, null, null, null, () => this.markDirty()))
		this.markDirty()
	}

	public serialize(): SenderKeyStateStructure[] {
		return this.senderKeyStates.map(state => state.getStructure())
	}

	public serializeForStorage(): Buffer {
		// Prefer the single-state native layout, then fall back to the generic multi-state container.
		if (!this.dirty && this.serializedCache) {
			return this.serializedCache
		}

		if (this.senderKeyStates.length === 1) {
			const nativeSingleState = this.senderKeyStates[0]?.serializeRecordForStorage()
			if (nativeSingleState) {
				this.serializedCache = nativeSingleState
				this.dirty = false
				return this.serializedCache
			}
		}

		const states = this.serialize()
		this.serializedCache = nativeEncodeSenderKeyStates(states)
		this.dirty = false
		return this.serializedCache
	}

	static deserialize(data: Uint8Array): SenderKeyRecord {
		if (isBinarySenderKeyData(data)) {
			const decoded = nativeDecodeSenderKeyStates(data) as SenderKeyStateStructure[]
			return new SenderKeyRecord(decoded, data)
		}

		throw new Error('strict native mode expects binary sender-key state payload')
	}
}
