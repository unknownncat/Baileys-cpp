import { mkdtempSync, rmSync } from 'fs'
import { tmpdir } from 'os'
import path from 'path'
import { spawnSync } from 'child_process'

describe('example native auth state smoke', () => {
	it('validates Example/example.ts with native auth state without opening a socket', () => {
		const authDir = mkdtempSync(path.join(tmpdir(), 'baileys-example-native-'))
		const tsxCliPath = path.join(process.cwd(), 'node_modules', 'tsx', 'dist', 'cli.mjs')
		const childEnv = {
			...process.env,
			AUTH_FOLDER: authDir,
			JEST_WORKER_ID: '',
			NODE_OPTIONS: ''
		}

		const result = spawnSync(
			process.execPath,
			[tsxCliPath, 'Example/example.ts', '--validate-native-auth-state'],
			{
				cwd: process.cwd(),
				encoding: 'utf8',
				env: childEnv
			}
		)

		rmSync(authDir, { recursive: true, force: true })

		expect(result.status).toBe(0)
		expect(result.stdout).toContain('native auth state smoke passed (native)')
	})
})
