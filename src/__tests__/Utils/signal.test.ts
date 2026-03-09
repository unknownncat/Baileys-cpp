import { jest } from '@jest/globals'
import type { SignalRepositoryWithLIDStore } from '../../Types'
import { parseAndInjectE2ESessions } from '../../Utils/signal'
import type { BinaryNode } from '../../WABinary/types'

describe('parseAndInjectE2ESessions', () => {
	it('should process all user node', async () => {
		const injectE2ESessions = jest.fn(async () => undefined)
		const mockRepository = {
			injectE2ESessions
		} as Pick<SignalRepositoryWithLIDStore, 'injectE2ESessions'>

		const createUserNode = (jid: string): BinaryNode => ({
			tag: 'user',
			attrs: { jid },
			content: [
				{
					tag: 'skey',
					attrs: {},
					content: [
						{ tag: 'id', attrs: {}, content: Buffer.from([0, 0, 1]) },
						{ tag: 'value', attrs: {}, content: Buffer.alloc(33) },
						{ tag: 'signature', attrs: {}, content: Buffer.alloc(64) }
					]
				},
				{
					tag: 'key',
					attrs: {},
					content: [
						{ tag: 'id', attrs: {}, content: Buffer.from([0, 0, 2]) },
						{ tag: 'value', attrs: {}, content: Buffer.alloc(33) }
					]
				},
				{ tag: 'identity', attrs: {}, content: Buffer.alloc(32) },
				{ tag: 'registration', attrs: {}, content: Buffer.alloc(4) }
			]
		})

		const mockNode: BinaryNode = {
			tag: 'iq',
			attrs: {},
			content: [
				{
					tag: 'list',
					attrs: {},
					content: [
						createUserNode('user1@s.whatsapp.net'),
						createUserNode('user2@s.whatsapp.net'),
						createUserNode('user3@s.whatsapp.net')
					]
				}
			]
		}

		await parseAndInjectE2ESessions(mockNode, mockRepository as any)

		expect(injectE2ESessions).toHaveBeenCalledTimes(1)
		expect(injectE2ESessions).toHaveBeenCalledWith([
			expect.objectContaining({ jid: 'user1@s.whatsapp.net' }),
			expect.objectContaining({ jid: 'user2@s.whatsapp.net' }),
			expect.objectContaining({ jid: 'user3@s.whatsapp.net' })
		])
	})
})
