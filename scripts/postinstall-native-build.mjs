#!/usr/bin/env node

import { spawnSync } from 'node:child_process'
import { existsSync } from 'node:fs'
import { createRequire } from 'node:module'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const require = createRequire(import.meta.url)
const __dirname = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(__dirname, '..')
const SUPPORTED_PLATFORMS = new Set(['win32', 'darwin', 'linux'])

const log = message => {
	console.log(`native postinstall: ${message}`)
}

const fail = message => {
	console.error(`native postinstall: ${message}`)
	process.exit(1)
}

if (process.env.BAILEYS_SKIP_NATIVE_BUILD === '1') {
	log('skipping native build because BAILEYS_SKIP_NATIVE_BUILD=1')
	process.exit(0)
}

if (!SUPPORTED_PLATFORMS.has(process.platform)) {
	log(`skipping native build on unsupported platform ${process.platform}`)
	process.exit(0)
}

const bindingGypPath = path.join(ROOT, 'binding.gyp')
const nativeDir = path.join(ROOT, 'native')

if (!existsSync(bindingGypPath) || !existsSync(nativeDir)) {
	log('native sources not packaged; skipping native build')
	process.exit(0)
}

const verifyScriptPath = path.join(ROOT, 'scripts', 'verify-native-deps.mjs')
if (existsSync(verifyScriptPath)) {
	const verify = spawnSync(process.execPath, [verifyScriptPath], {
		cwd: ROOT,
		stdio: 'inherit',
		env: process.env
	})

	if (verify.status !== 0) {
		fail(`dependency verification failed with exit code ${verify.status ?? 1}`)
	}
}

const nodeGypBin = require.resolve('node-gyp/bin/node-gyp.js')
const result = spawnSync(process.execPath, [nodeGypBin, 'rebuild', '-j', 'max'], {
	cwd: ROOT,
	stdio: 'inherit',
	env: {
		...process.env,
		BAILEYS_SKIP_NATIVE_WAPROTO: process.env.BAILEYS_SKIP_NATIVE_WAPROTO || '1'
	}
})

if (result.status !== 0) {
	fail(`node-gyp rebuild failed with exit code ${result.status ?? 1}`)
}

log('native addon build completed')
