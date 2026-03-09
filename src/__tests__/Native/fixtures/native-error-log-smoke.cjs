const path = require('path')

const candidates = [
	path.resolve(process.cwd(), 'build/Release/baileys_native.node'),
	path.resolve(process.cwd(), 'build/Debug/baileys_native.node')
]

let binding = null
let lastError = null

for (const candidate of candidates) {
	try {
		binding = require(candidate)
		break
	} catch (error) {
		lastError = error
	}
}

if (!binding) {
	console.error(lastError ? String(lastError.message || lastError) : 'failed to load native binding')
	process.exit(2)
}

const swallow = fn => {
	try {
		fn()
	} catch {
		// The fixture only exists to trigger internal native logging side-effects.
	}
}

swallow(() => new binding.NativeKVStore())
swallow(() => binding.decodeProtoMessageFromPadded(Buffer.alloc(0)))

if (process.platform === 'win32' && typeof binding.NativeMediaDecryptor === 'function') {
	swallow(() => new binding.NativeMediaDecryptor(Buffer.alloc(31), Buffer.alloc(16)))
}
