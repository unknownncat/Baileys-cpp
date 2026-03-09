import path from 'path'
import { spawnSync } from 'child_process'

describe('native error logging', () => {
	const fixturePath = path.join(process.cwd(), 'src', '__tests__', 'Native', 'fixtures', 'native-error-log-smoke.cjs')

	it('emits contextual stderr logs only when enabled', () => {
		const result = spawnSync(process.execPath, [fixturePath], {
			cwd: process.cwd(),
			encoding: 'utf8',
			env: {
				...process.env,
				BAILEYS_NATIVE_ERROR_LOG: '1'
			}
		})

		expect(result.status).toBe(0)
		expect(result.stderr).toContain('[baileys-native][storage.kv] op=constructor')
		expect(result.stderr).toContain('[baileys-native][proto.codec] op=unpadded.empty')
		if (process.platform === 'win32') {
			expect(result.stderr).toContain('[baileys-native][media.decryptor] op=constructor')
		}
	})

	it('stays silent when logging is disabled', () => {
		const result = spawnSync(process.execPath, [fixturePath], {
			cwd: process.cwd(),
			encoding: 'utf8',
			env: {
				...process.env,
				BAILEYS_NATIVE_ERROR_LOG: ''
			}
		})

		expect(result.status).toBe(0)
		expect(result.stderr).not.toContain('[baileys-native][')
	})
})
