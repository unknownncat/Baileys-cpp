import { SenderKeyRecord } from '../../../Signal/Group/sender-key-record'
import { SenderKeyState } from '../../../Signal/Group/sender-key-state'
import { SenderMessageKey } from '../../../Signal/Group/sender-message-key'

describe('SenderKeyState regression: missing senderMessageKeys array', () => {
	it('should initialize senderMessageKeys when absent in provided structure', () => {
		const chainSeed = Buffer.alloc(32, 1)
		const publicKey = Buffer.alloc(32, 2)
		const messageSeed = Buffer.alloc(32, 3)

		const legacyStructure = {
			senderKeyId: 42,
			senderChainKey: { iteration: 0, seed: chainSeed },
			senderSigningKey: { public: publicKey }
		}

		const state = new SenderKeyState(null, null, null, null, null, null, legacyStructure as any)
		const msgKey = new SenderMessageKey(0, messageSeed)
		state.addSenderMessageKey(msgKey)

		const structure = state.getStructure()
		expect(structure.senderMessageKeys).toBeDefined()
		expect(Array.isArray(structure.senderMessageKeys)).toBe(true)
		expect(structure.senderMessageKeys.length).toBe(1)
		expect(structure.senderMessageKeys[0]?.iteration).toBe(0)
	})

	it('should keep senderMessageKeys coherent across repeated getStructure calls and removals', () => {
		const chainSeed = Buffer.alloc(32, 1)
		const publicKey = Buffer.alloc(32, 2)
		const messageSeed = Buffer.alloc(32, 3)

		const state = new SenderKeyState(
			42,
			0,
			chainSeed,
			{
				public: publicKey,
				private: Buffer.alloc(32, 4)
			}
		)

		state.addSenderMessageKey(new SenderMessageKey(0, messageSeed))

		const firstRead = state.getStructure()
		const secondRead = state.getStructure()
		expect(firstRead.senderMessageKeys).toHaveLength(1)
		expect(secondRead.senderMessageKeys).toHaveLength(1)

		const removed = state.removeSenderMessageKey(0)
		expect(removed?.getIteration()).toBe(0)

		const afterRemoval = state.getStructure()
		expect(afterRemoval.senderMessageKeys).toHaveLength(0)
	})

	it('should invalidate serialized sender-key payload when state mutates after deserialize', () => {
		const record = new SenderKeyRecord()
		record.setSenderKeyState(42, 0, Buffer.alloc(32, 1), {
			public: Buffer.alloc(32, 2),
			private: Buffer.alloc(32, 3)
		})

		const encoded = record.serializeForStorage()
		const hydrated = SenderKeyRecord.deserialize(encoded)
		expect(hydrated.serializeForStorage()).toEqual(encoded)

		const state = hydrated.getSenderKeyState()
		expect(state).toBeDefined()
		state!.addSenderMessageKey(new SenderMessageKey(7, Buffer.alloc(32, 4)))

		const mutated = hydrated.serializeForStorage()
		expect(mutated).not.toEqual(encoded)
	})

	it('should preserve multi-state sender-key round-trip via fallback path', () => {
		const record = new SenderKeyRecord()
		record.addSenderKeyState(41, 0, Buffer.alloc(32, 1), Buffer.alloc(32, 2))
		record.addSenderKeyState(42, 7, Buffer.alloc(32, 3), Buffer.alloc(32, 4))

		const encoded = record.serializeForStorage()
		const hydrated = SenderKeyRecord.deserialize(encoded)
		const latest = hydrated.getSenderKeyState()
		const older = hydrated.getSenderKeyState(41)

		expect(latest?.getKeyId()).toBe(42)
		expect(latest?.getSenderChainKey().getIteration()).toBe(7)
		expect(older?.getKeyId()).toBe(41)
		expect(older?.getSenderChainKey().getIteration()).toBe(0)
	})
})
