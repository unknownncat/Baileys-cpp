#!/usr/bin/env node

import { spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(__dirname, '..')
const SOURCE_PROTO = path.join(ROOT, 'WAProto', 'WAProto.proto')
const OUTPUT_DIR = path.join(ROOT, 'native', 'generated')
const PATCHED_PROTO = path.join(OUTPUT_DIR, 'WAProto_cpp.proto')
const STAMP_FILE = path.join(OUTPUT_DIR, 'WAProto_cpp.sha256')
const META_FILE = path.join(OUTPUT_DIR, 'WAProto_cpp.meta.json')
const GENERATED_CPP = path.join(OUTPUT_DIR, 'WAProto_cpp.pb.cc')
const GENERATED_H = path.join(OUTPUT_DIR, 'WAProto_cpp.pb.h')
const VCPKG_ROOT = process.env.BAILEYS_VCPKG_ROOT || 'C:/vcpkg'
const VCPKG_TRIPLET = process.env.BAILEYS_VCPKG_TRIPLET || 'x64-windows-static'

const enumRegex = /enum\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([\s\S]*?)\n\s*\}/g

const patchEnumsForProto3 = source =>
	source.replace(enumRegex, (full, enumName, body) => {
		const lines = body.split(/\r?\n/)
		let firstEnumFieldLine = -1
		let firstEnumFieldValue = 0

		for (let i = 0; i < lines.length; i += 1) {
			const trimmed = lines[i].trim()
			if (!trimmed || trimmed.startsWith('//') || trimmed.startsWith('option ') || trimmed.startsWith('reserved ')) {
				continue
			}

			const field = trimmed.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?\d+)\s*;/)
			if (!field) {
				return full
			}

			firstEnumFieldLine = i
			firstEnumFieldValue = Number.parseInt(field[2], 10)
			break
		}

		if (firstEnumFieldLine < 0 || firstEnumFieldValue === 0) {
			return full
		}

		const injectedValueName = `${enumName}_UNSPECIFIED`
		if (lines.some(line => line.includes(`${injectedValueName} = 0;`))) {
			return full
		}

		const indent = lines[firstEnumFieldLine].match(/^(\s*)/)?.[1] || '    '
		lines.splice(firstEnumFieldLine, 0, `${indent}${injectedValueName} = 0;`)
		return `enum ${enumName} {${lines.join('\n')}\n}`
	})

const patchCppMacroConflicts = source => source.replace(/\bWIN32\s*=/g, 'WIN32_ENUM_VALUE =')

const hashText = text => createHash('sha256').update(text, 'utf8').digest('hex')
const hashFile = filePath => createHash('sha256').update(readFileSync(filePath)).digest('hex')

const writeGeneratedMetadata = ({ sourceHash, protocVersion, sourceProtoSha256 }) => {
	const payload = {
		sourceHash,
		protocVersion,
		sourceProtoSha256,
		patchedProtoSha256: hashFile(PATCHED_PROTO),
		generatedHeaderSha256: hashFile(GENERATED_H),
		generatedSourceSha256: hashFile(GENERATED_CPP)
	}

	writeFileSync(META_FILE, `${JSON.stringify(payload, null, 2)}\n`, 'utf8')
}

const resolveProtoc = () => {
	const candidates = [
		process.env.BAILEYS_PROTOC_PATH,
		path.join(VCPKG_ROOT, 'installed', VCPKG_TRIPLET, 'tools', 'protobuf', process.platform === 'win32' ? 'protoc.exe' : 'protoc'),
		'protoc'
	].filter(Boolean)

	for (const candidate of candidates) {
		if (candidate === 'protoc' || existsSync(candidate)) {
			return candidate
		}
	}

	return 'protoc'
}

const run = () => {
	if (process.env.BAILEYS_SKIP_NATIVE_WAPROTO === '1') {
		console.log('Skipping WAProto C++ generation (BAILEYS_SKIP_NATIVE_WAPROTO=1)')
		return
	}

	const source = readFileSync(SOURCE_PROTO, 'utf8')
	const patched = patchCppMacroConflicts(patchEnumsForProto3(source))
	const protocBinary = resolveProtoc()
	const protocVersionResult = spawnSync(protocBinary, ['--version'], { encoding: 'utf8' })
	const protocVersion = protocVersionResult.status === 0 ? protocVersionResult.stdout.trim() : 'unknown-protoc'
	const sourceHash = hashText(`${patched}\n${protocVersion}`)
	const sourceProtoSha256 = hashText(source)
	const patchedProtoSha256 = hashText(patched)

	const hasOutputs = existsSync(GENERATED_CPP) && existsSync(GENERATED_H) && existsSync(STAMP_FILE)
	const oldHash = hasOutputs ? readFileSync(STAMP_FILE, 'utf8').trim() : ''
	if (hasOutputs && sourceHash === oldHash) {
		if (!existsSync(PATCHED_PROTO) || hashFile(PATCHED_PROTO) !== patchedProtoSha256) {
			writeFileSync(PATCHED_PROTO, patched, 'utf8')
		}
		writeGeneratedMetadata({ sourceHash, protocVersion, sourceProtoSha256 })
		console.log('WAProto C++ already up to date')
		return
	}

	mkdirSync(OUTPUT_DIR, { recursive: true })
	writeFileSync(PATCHED_PROTO, patched, 'utf8')

	const protoc = spawnSync(
		protocBinary,
		['-I', '.', '--cpp_out=.', path.basename(PATCHED_PROTO)],
		{
			cwd: OUTPUT_DIR,
			encoding: 'utf8'
		}
	)

	if (protoc.status !== 0) {
		const stderr = protoc.stderr?.trim()
		const stdout = protoc.stdout?.trim()
		const msg = stderr || stdout || 'unknown protoc error'
		throw new Error(`protoc failed generating WAProto C++: ${msg}`)
	}

	writeFileSync(STAMP_FILE, `${sourceHash}\n`, 'utf8')
	writeGeneratedMetadata({ sourceHash, protocVersion, sourceProtoSha256 })
	console.log('Generated native/generated/WAProto_cpp.pb.{h,cc}')
}

run()
