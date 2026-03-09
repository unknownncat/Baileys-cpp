import { promises as fs } from 'fs'
import { tmpdir } from 'os'
import { join } from 'path'
import { performance } from 'perf_hooks'
import { encryptedStream, getMediaKeys, getRawMediaUploadData } from '../src/Utils/messages-media.ts'
import { getOptionalNativeExport, requireNativeExport } from '../src/Native/baileys-native.ts'

const NativeHashSpoolWriterCtor = getOptionalNativeExport('NativeHashSpoolWriter')
const NativeMediaEncryptToFileCtor = getOptionalNativeExport('NativeMediaEncryptToFile')
const NativeAlignedChunkerCtor = getOptionalNativeExport('NativeAlignedChunker')
const NativeRangeFilterCtor = getOptionalNativeExport('NativeRangeFilter')
const NativeMediaDecryptPipelineCtor = requireNativeExport('NativeMediaDecryptPipeline')

const iterations = Number(process.env.BAILEYS_MEDIA_BENCH_ITERATIONS ?? 3)
const sizeMb = Number(process.env.BAILEYS_MEDIA_BENCH_SIZE_MB ?? 8)
const chunkSize = 64 * 1024
const payload = Buffer.alloc(sizeMb * 1024 * 1024, 0xab)

type Row = {
	name: string
	iterations: number
	totalMs: number
	avgMs: number
}

const rows: Row[] = []

const makeTempPath = (prefix: string) =>
	join(tmpdir(), `${prefix}-${process.pid}-${Date.now()}-${Math.random().toString(16).slice(2)}.bin`)

const bench = async (name: string, fn: () => Promise<void>) => {
	const started = performance.now()
	for (let i = 0; i < iterations; i += 1) {
		await fn()
	}

	const totalMs = performance.now() - started
	rows.push({
		name,
		iterations,
		totalMs,
		avgMs: totalMs / iterations
	})
}

const runChunked = (buffer: Buffer, onChunk: (chunk: Buffer) => void) => {
	for (let offset = 0; offset < buffer.length; offset += chunkSize + 7) {
		onChunk(buffer.subarray(offset, Math.min(buffer.length, offset + chunkSize + 7)))
	}
}

await bench('getRawMediaUploadData', async () => {
	const result = await getRawMediaUploadData(payload, 'image')
	await fs.unlink(result.filePath)
})

await bench('encryptedStream', async () => {
	const result = await encryptedStream(payload, 'image')
	await fs.unlink(result.encFilePath)
	if (result.originalFilePath) {
		await fs.unlink(result.originalFilePath)
	}
})

if (NativeHashSpoolWriterCtor) {
	await bench('NativeHashSpoolWriter', async () => {
		const filePath = makeTempPath('baileys-native-hash')
		const writer = new NativeHashSpoolWriterCtor(filePath)
		runChunked(payload, chunk => writer.update(chunk))
		writer.final()
		await fs.unlink(filePath)
	})
}

if (NativeMediaEncryptToFileCtor) {
	await bench('NativeMediaEncryptToFile', async () => {
		const encPath = makeTempPath('baileys-native-enc')
		const writer = new NativeMediaEncryptToFileCtor(
			Buffer.alloc(32, 0x11),
			Buffer.alloc(16, 0x22),
			Buffer.alloc(32, 0x33),
			encPath
		)
		runChunked(payload, chunk => writer.update(chunk))
		writer.final()
		await fs.unlink(encPath)
	})
}

if (NativeAlignedChunkerCtor) {
	await bench('NativeAlignedChunker', async () => {
		const chunker = new NativeAlignedChunkerCtor(16)
		runChunked(payload, chunk => {
			chunker.push(chunk)
		})
		chunker.takeRemaining()
	})
}

if (NativeRangeFilterCtor) {
	await bench('NativeRangeFilter', async () => {
		const filter = new NativeRangeFilterCtor(32 * 1024, 160 * 1024, 0)
		runChunked(payload, chunk => {
			filter.push(chunk)
		})
	})
}

const encryptedFixture = await encryptedStream(payload, 'image')
const encryptedBytesWithMac = await fs.readFile(encryptedFixture.encFilePath)
const encryptedBytes = encryptedBytesWithMac.subarray(0, encryptedBytesWithMac.length - 10)
const keys = await getMediaKeys(encryptedFixture.mediaKey, 'image')

await bench('NativeMediaDecryptPipeline', async () => {
	const pipeline = new NativeMediaDecryptPipelineCtor(keys.cipherKey, keys.iv, { autoPadding: true })
	runChunked(encryptedBytes, chunk => {
		pipeline.update(chunk)
	})
	pipeline.final()
})

await fs.unlink(encryptedFixture.encFilePath)
if (encryptedFixture.originalFilePath) {
	await fs.unlink(encryptedFixture.originalFilePath)
}

console.log('Media Hot Path Benchmark')
console.log(`payload_mb | ${sizeMb}`)
console.log('name | iterations | total_ms | avg_ms')
for (const row of rows) {
	console.log(`${row.name} | ${row.iterations} | ${row.totalMs.toFixed(2)} | ${row.avgMs.toFixed(2)}`)
}
