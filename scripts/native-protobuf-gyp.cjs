const fs = require('fs')
const path = require('path')

const mode = process.argv[2]
const vcpkgRoot = process.env.BAILEYS_VCPKG_ROOT || 'C:/vcpkg'
const triplet = process.env.BAILEYS_VCPKG_TRIPLET || 'x64-windows-static'
const installRoot = path.join(vcpkgRoot, 'installed', triplet)
const includeDir = path.join(installRoot, 'include')
const libDir = path.join(installRoot, 'lib')

const normalize = p => p.replace(/\\/g, '/')

if (mode === 'include') {
	process.stdout.write(normalize(includeDir))
	process.exit(0)
}

if (mode === 'libs') {
	if (!fs.existsSync(libDir)) {
		process.exit(0)
	}

	const preferred = ['libprotobuf.lib', 'libprotobuf-lite.lib']
	const available = new Set(fs.readdirSync(libDir))
	const selected = preferred.find(name => available.has(name))
	const wanted = new Set(selected ? [selected] : [])
	const allLibs = fs
		.readdirSync(libDir)
		.filter(name => name.endsWith('.lib'))
		.filter(name => wanted.has(name) || name.startsWith('absl_') || name.startsWith('utf8_'))
		.map(name => normalize(path.join(libDir, name)))
		.sort()

	process.stdout.write(allLibs.join(' '))
	process.exit(0)
}

process.exit(0)
