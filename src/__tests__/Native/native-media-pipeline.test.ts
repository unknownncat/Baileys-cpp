import { createCipheriv, createHash } from 'crypto'
import { promises as fs } from 'fs'
import { tmpdir } from 'os'
import { join } from 'path'
import type {
	NativeHashSpoolWriter,
	NativeMediaDecryptor,
	NativeMediaDecryptPipeline,
	NativeMediaEncryptToFile,
	NativeMediaEncryptor
} from '../../Native/baileys-native'
import { requireNativeExport } from '../../Native/baileys-native'

const NativeHashSpoolWriterCtor = requireNativeExport('NativeHashSpoolWriter')
const NativeMediaDecryptPipelineCtor = requireNativeExport('NativeMediaDecryptPipeline')
const NativeMediaDecryptorCtor = requireNativeExport('NativeMediaDecryptor')
const NativeMediaEncryptorCtor = requireNativeExport('NativeMediaEncryptor')
const NativeMediaEncryptToFileCtor = requireNativeExport('NativeMediaEncryptToFile')

const key = Buffer.alloc(32, 0x11)
const iv = Buffer.alloc(16, 0x22)
const macKey = Buffer.alloc(32, 0x33)

const windowsOnly = process.platform === 'win32' ? describe : describe.skip

windowsOnly('native media decrypt hardening', () => {
	it('validates constructor key lengths and option types', () => {
		expect(() => new NativeMediaDecryptPipelineCtor(Buffer.alloc(31), iv)).toThrow(
			'NativeMediaDecryptPipeline expects 32-byte key and 16-byte iv'
		)
		expect(() => new NativeMediaDecryptPipelineCtor(key, Buffer.alloc(15))).toThrow(
			'NativeMediaDecryptPipeline expects 32-byte key and 16-byte iv'
		)
		expect(() => new NativeMediaDecryptPipelineCtor(key, iv, { autoPadding: 'x' as unknown as boolean })).toThrow(
			'options.autoPadding must be a boolean'
		)
	})

	it('fails on final when pending encrypted bytes are not block-aligned', () => {
		const pipeline = new NativeMediaDecryptPipelineCtor(key, iv) as NativeMediaDecryptPipeline
		expect(pipeline.update(Buffer.alloc(15))).toEqual(Buffer.alloc(0))
		expect(() => pipeline.final()).toThrow('NativeMediaDecryptPipeline final chunk is not block-aligned')
	})

	it('fails deterministicly on invalid PKCS7 padding', () => {
		const plain = Buffer.alloc(16, 0x41)
		plain[15] = 0

		const cipher = createCipheriv('aes-256-cbc', key, iv)
		cipher.setAutoPadding(false)
		const encrypted = Buffer.concat([cipher.update(plain), cipher.final()])

		const pipeline = new NativeMediaDecryptPipelineCtor(key, iv, { autoPadding: true }) as NativeMediaDecryptPipeline
		expect(pipeline.update(encrypted)).toEqual(Buffer.alloc(0))
		expect(() => pipeline.final()).toThrow('invalid PKCS7 padding')
	})

	it('keeps block-alignment error contract on NativeMediaDecryptor.update', () => {
		const decryptor = new NativeMediaDecryptorCtor(key, iv, true) as NativeMediaDecryptor
		expect(() => decryptor.update(Buffer.alloc(15))).toThrow('NativeMediaDecryptor.update expects block-aligned chunk')
	})

	it('encrypts empty payloads without failing on final padding', () => {
		const encryptor = new NativeMediaEncryptorCtor(key, iv, macKey) as NativeMediaEncryptor

		expect(encryptor.update(Buffer.alloc(0))).toEqual(Buffer.alloc(0))

		const result = encryptor.final()
		expect(result.finalChunk).toHaveLength(16)
		expect(result.mac).toHaveLength(10)
		expect(result.fileSha256).toHaveLength(32)
		expect(result.fileEncSha256).toHaveLength(32)
	})

	it('spools raw media to disk while hashing in the same native object', async () => {
		const filePath = join(tmpdir(), `baileys-native-hash-${Date.now()}.bin`)
		const payload = Buffer.from('native spool payload')
		const writer = new NativeHashSpoolWriterCtor(filePath) as NativeHashSpoolWriter

		writer.update(payload.subarray(0, 7))
		writer.update(payload.subarray(7))
		const result = writer.final()

		expect(result.fileLength).toBe(payload.length)
		expect(result.fileSha256).toEqual(createHash('sha256').update(payload).digest())
		await expect(fs.readFile(filePath)).resolves.toEqual(payload)

		await fs.unlink(filePath)
	})

	it('removes the partial raw spool file on abort', async () => {
		const filePath = join(tmpdir(), `baileys-native-hash-abort-${Date.now()}.bin`)
		const writer = new NativeHashSpoolWriterCtor(filePath) as NativeHashSpoolWriter

		writer.update(Buffer.from('partial'))
		writer.abort()

		await expect(fs.access(filePath)).rejects.toThrow()
	})

	it('encrypts directly to files and keeps decrypt parity', async () => {
		const encPath = join(tmpdir(), `baileys-native-enc-${Date.now()}.bin`)
		const originalPath = join(tmpdir(), `baileys-native-orig-${Date.now()}.bin`)
		const payload = Buffer.from('native encrypt to file payload that spans multiple blocks')
		const writer = new NativeMediaEncryptToFileCtor(
			key,
			iv,
			macKey,
			encPath,
			originalPath
		) as NativeMediaEncryptToFile

		writer.update(payload.subarray(0, 11))
		writer.update(payload.subarray(11))
		const result = writer.final()

		const encryptedWithMac = await fs.readFile(encPath)
		const cipherBytes = encryptedWithMac.subarray(0, encryptedWithMac.length - 10)
		const macBytes = encryptedWithMac.subarray(encryptedWithMac.length - 10)
		const decryptor = new NativeMediaDecryptPipelineCtor(key, iv, { autoPadding: true }) as NativeMediaDecryptPipeline
		const decrypted = Buffer.concat([decryptor.update(cipherBytes), decryptor.final()])

		expect(result.fileLength).toBe(payload.length)
		expect(result.mac).toEqual(macBytes)
		expect(result.fileSha256).toEqual(createHash('sha256').update(payload).digest())
		expect(decrypted).toEqual(payload)
		await expect(fs.readFile(originalPath)).resolves.toEqual(payload)

		await Promise.all([fs.unlink(encPath), fs.unlink(originalPath)])
	})
})
