#!/usr/bin/env node

import { createHash } from 'node:crypto'
import { existsSync, readFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(__dirname, '..')

const args = process.argv.slice(2)
let manifestPath = path.join(ROOT, 'native', 'native_dependency_manifest.json')

for (let i = 0; i < args.length; i += 1) {
	if (args[i] === '--manifest' && i + 1 < args.length) {
		manifestPath = path.resolve(ROOT, args[i + 1])
		i += 1
	}
}

const hashFile = filePath =>
	createHash('sha256').update(readFileSync(filePath)).digest('hex').toUpperCase()

const fail = message => {
	console.error(`verify-native-deps: ${message}`)
	process.exit(1)
}

const assertFileHash = (rootDir, expectedPath, expectedHash) => {
	const absolutePath = path.resolve(rootDir, expectedPath)
	if (!existsSync(absolutePath)) {
		fail(`missing locked file: ${expectedPath}`)
	}

	const actualHash = hashFile(absolutePath)
	if (actualHash !== expectedHash.toUpperCase()) {
		fail(`hash mismatch for ${expectedPath}: expected ${expectedHash}, got ${actualHash}`)
	}
}

if (!existsSync(manifestPath)) {
	fail(`manifest not found: ${path.relative(ROOT, manifestPath)}`)
}

const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'))
const waProto = manifest.generated?.waProto
const miniz = manifest.thirdParty?.miniz

if (!waProto || !miniz) {
	fail('manifest missing generated.waProto or thirdParty.miniz section')
}

assertFileHash(ROOT, waProto.sourceProto.path, waProto.sourceProto.sha256)
assertFileHash(ROOT, waProto.generatedFiles.proto.path, waProto.generatedFiles.proto.sha256)
assertFileHash(ROOT, waProto.generatedFiles.header.path, waProto.generatedFiles.header.sha256)
assertFileHash(ROOT, waProto.generatedFiles.source.path, waProto.generatedFiles.source.sha256)

const metaPath = path.join(ROOT, 'native', 'generated', 'WAProto_cpp.meta.json')
if (!existsSync(metaPath)) {
	fail('missing native/generated/WAProto_cpp.meta.json; run scripts/generate-waproto-cpp.mjs first')
}

const meta = JSON.parse(readFileSync(metaPath, 'utf8'))
if (meta.protocVersion !== waProto.generator.protocVersion) {
	fail(`WAProto protoc version mismatch: expected ${waProto.generator.protocVersion}, got ${meta.protocVersion}`)
}
if (meta.sourceHash !== waProto.generator.sourceHash) {
	fail(`WAProto source hash mismatch: expected ${waProto.generator.sourceHash}, got ${meta.sourceHash}`)
}
if (meta.sourceProtoSha256.toUpperCase() !== waProto.sourceProto.sha256.toUpperCase()) {
	fail('WAProto source proto hash metadata mismatch')
}
if (meta.patchedProtoSha256.toUpperCase() !== waProto.generatedFiles.proto.sha256.toUpperCase()) {
	fail('WAProto generated proto hash metadata mismatch')
}
if (meta.generatedHeaderSha256.toUpperCase() !== waProto.generatedFiles.header.sha256.toUpperCase()) {
	fail('WAProto generated header hash metadata mismatch')
}
if (meta.generatedSourceSha256.toUpperCase() !== waProto.generatedFiles.source.sha256.toUpperCase()) {
	fail('WAProto generated source hash metadata mismatch')
}

const minizHeaderPath = path.join(ROOT, 'native', 'third_party', 'miniz', 'miniz.h')
const firstLine = readFileSync(minizHeaderPath, 'utf8').split(/\r?\n/, 1)[0]
const versionMatch = firstLine.match(/miniz\.c\s+([0-9.]+)/)
if (!versionMatch) {
	fail('unable to extract miniz declared version from native/third_party/miniz/miniz.h')
}
if (versionMatch[1] !== miniz.declaredVersion) {
	fail(`miniz version mismatch: expected ${miniz.declaredVersion}, got ${versionMatch[1]}`)
}

for (const [relativeName, expectedHash] of Object.entries(miniz.files)) {
	assertFileHash(ROOT, path.join('native', 'third_party', 'miniz', relativeName), expectedHash)
}

console.log('Native dependency manifest verified')
