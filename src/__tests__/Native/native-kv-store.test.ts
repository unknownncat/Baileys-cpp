import { mkdtemp, rm } from 'fs/promises'
import { tmpdir } from 'os'
import { join } from 'path'
import type { NativeKVStore } from '../../Native/baileys-native'
import { requireNativeExport } from '../../Native/baileys-native'

const NativeKVStoreCtor = requireNativeExport('NativeKVStore')

describe('NativeKVStore integration', () => {
	let testDir = ''

	beforeEach(async () => {
		testDir = await mkdtemp(join(tmpdir(), 'baileys-native-kv-'))
	})

	afterEach(async () => {
		if (testDir) {
			await rm(testDir, { recursive: true, force: true })
		}
	})

	it('supports set/get/getMany/delete/compact/clear with stable API', () => {
		const store = new NativeKVStoreCtor(join(testDir, 'state.kv'), {
			compactThresholdBytes: 256,
			compactRatio: 1.0,
			maxQueuedBytes: 4 * 1024
		}) as NativeKVStore

		expect(store.size()).toBe(0)
		expect(store.get('missing')).toBeNull()

		store.setMany([
			{ key: 'a', value: Buffer.from('1') },
			{ key: 'b', value: Buffer.from('2') }
		])

		expect(store.size()).toBe(2)
		expect(store.get('a')).toEqual(Buffer.from('1'))
		expect(store.getMany(['a', 'b', 'c'])).toEqual([Buffer.from('1'), Buffer.from('2'), null])

		store.deleteMany(['a'])
		expect(store.get('a')).toBeNull()
		expect(store.size()).toBe(1)

		expect(store.compact()).toBe(true)
		store.clear()
		expect(store.size()).toBe(0)
	})

	it('enforces writer queue overflow control without mutating in-memory state', () => {
		const store = new NativeKVStoreCtor(join(testDir, 'overflow.kv'), {
			compactThresholdBytes: 1024,
			compactRatio: 2.0,
			maxQueuedBytes: 32
		}) as NativeKVStore

		expect(() => store.setMany([{ key: 'k1', value: Buffer.alloc(128, 1) }])).toThrow(
			'NativeKVStore write queue overflow'
		)
		expect(store.size()).toBe(0)
		expect(store.get('k1')).toBeNull()
	})
})
