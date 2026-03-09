import { MessageRetryManager, RetryReason } from '../../Utils/message-retry-manager'
import type { ILogger } from '../../Utils/logger'

const makeLogger = (): ILogger =>
	({
		level: 'debug',
		child: () => makeLogger(),
		trace: () => undefined,
		debug: () => undefined,
		info: () => undefined,
		warn: () => undefined,
		error: () => undefined
	}) as ILogger

describe('MessageRetryManager', () => {
	it('forces session recreation for Invalid PreKey ID errors', () => {
		const manager = new MessageRetryManager(makeLogger(), 5)

		const result = manager.shouldRecreateSession('123@s.whatsapp.net', true, RetryReason.SignalErrorInvalidKeyId)

		expect(result.recreate).toBe(true)
		expect(result.reason).toContain('pre-key mismatch')
	})

	it('blocks a message after a terminal retry failure', () => {
		const manager = new MessageRetryManager(makeLogger(), 2)

		manager.incrementRetryCount('msg-1')
		manager.incrementRetryCount('msg-1')
		expect(manager.hasExceededMaxRetries('msg-1')).toBe(true)

		manager.markRetryFailed('msg-1', true)

		expect(manager.isRetryBlocked('msg-1')).toBe(true)
		expect(manager.getRetryCount('msg-1')).toBe(0)
	})

	it('clears terminal retry blocks after a successful recovery', () => {
		const manager = new MessageRetryManager(makeLogger(), 2)

		manager.markRetryFailed('msg-2', true)
		expect(manager.isRetryBlocked('msg-2')).toBe(true)

		manager.markRetrySuccess('msg-2')

		expect(manager.isRetryBlocked('msg-2')).toBe(false)
	})
})
