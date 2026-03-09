import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'fs'
import { tmpdir } from 'os'
import path from 'path'
import { spawnSync } from 'child_process'

describe('native dependency governance', () => {
	const scriptPath = path.join(process.cwd(), 'scripts', 'verify-native-deps.mjs')
	const manifestPath = path.join(process.cwd(), 'native', 'native_dependency_manifest.json')

	it('verifies the locked native dependency manifest', () => {
		const result = spawnSync(process.execPath, [scriptPath], {
			cwd: process.cwd(),
			encoding: 'utf8'
		})

		expect(result.status).toBe(0)
		expect(result.stdout).toContain('Native dependency manifest verified')
	})

	it('fails deterministically on manifest hash drift', () => {
		const tempDir = mkdtempSync(path.join(tmpdir(), 'baileys-native-deps-'))
		const tempManifest = path.join(tempDir, 'manifest.json')
		const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'))
		manifest.thirdParty.miniz.files['miniz.c'] = '0000000000000000000000000000000000000000000000000000000000000000'
		writeFileSync(tempManifest, `${JSON.stringify(manifest, null, 2)}\n`, 'utf8')

		const result = spawnSync(process.execPath, [scriptPath, '--manifest', tempManifest], {
			cwd: process.cwd(),
			encoding: 'utf8'
		})

		rmSync(tempDir, { recursive: true, force: true })

		expect(result.status).not.toBe(0)
		expect(result.stderr).toMatch(/hash mismatch for native[\\/]+third_party[\\/]+miniz[\\/]+miniz\.c/i)
	})
})
