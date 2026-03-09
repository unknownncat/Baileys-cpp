import { Boom } from '@hapi/boom'
import { exec } from 'child_process'
import * as Crypto from 'crypto'
import { once } from 'events'
import { createReadStream, createWriteStream, promises as fs, WriteStream } from 'fs'
import type { Agent } from 'https'
import type { IAudioMetadata } from 'music-metadata'
import { tmpdir } from 'os'
import { join } from 'path'
import { Readable, Transform } from 'stream'
import { URL } from 'url'
import { proto } from '../../WAProto/index.js'
import { DEFAULT_ORIGIN, MEDIA_HKDF_KEY_MAPPING, MEDIA_PATH_MAP, type MediaType } from '../Defaults'
import {
	type NativeBufferBuilder,
	type NativeHashSpoolWriter,
	type NativeMediaDecryptPipeline,
	type NativeMediaEncryptToFile,
	type NativeMediaEncryptor,
	getOptionalNativeExport,
	requireNativeExport
} from '../Native/baileys-native'
import type {
	BaileysEventMap,
	DownloadableMessage,
	MediaConnInfo,
	MediaDecryptionKeyInfo,
	MessageType,
	SocketConfig,
	WAGenericMediaMessage,
	WAMediaUpload,
	WAMediaUploadFunction,
	WAMessageContent,
	WAMessageKey
} from '../Types'
import { type BinaryNode, getBinaryNodeChild, getBinaryNodeChildBuffer, jidNormalizedUser } from '../WABinary'
import { aesDecryptGCM, aesEncryptGCM, hkdf } from './crypto'
import { generateMessageIDV2 } from './generics'
import type { ILogger } from './logger'

const nativeAnalyzeWavAudioFast = requireNativeExport('analyzeWavAudioFast')
const NativeBufferBuilderCtor = requireNativeExport('NativeBufferBuilder')
const NativeMediaEncryptorCtor = requireNativeExport('NativeMediaEncryptor')
const NativeMediaDecryptPipelineCtor = requireNativeExport('NativeMediaDecryptPipeline')
const useExperimentalNativeMediaWriters =
	process.env.BAILEYS_NATIVE_MEDIA_WRITERS === '1' || process.env.BAILEYS_NATIVE_MEDIA_WRITERS === 'true'
const NativeHashSpoolWriterCtor = useExperimentalNativeMediaWriters ? getOptionalNativeExport('NativeHashSpoolWriter') : undefined
const NativeMediaEncryptToFileCtor = useExperimentalNativeMediaWriters
	? getOptionalNativeExport('NativeMediaEncryptToFile')
	: undefined

const getTmpFilesDirectory = () => tmpdir()

type MediaHashSpoolResult = {
	fileSha256: Buffer
	fileLength: number
}

type MediaHashSpoolWriter = {
	update(chunk: Uint8Array): void | Promise<void>
	final(): MediaHashSpoolResult | Promise<MediaHashSpoolResult>
	abort(): void | Promise<void>
}

type MediaEncryptToFileResult = {
	mac: Buffer
	fileSha256: Buffer
	fileEncSha256: Buffer
	fileLength: number
}

type MediaEncryptToFileWriter = {
	update(chunk: Uint8Array): void | Promise<void>
	final(): MediaEncryptToFileResult | Promise<MediaEncryptToFileResult>
	abort(): void | Promise<void>
}

const createTempMediaFilePath = (mediaType: MediaType, suffix = '') =>
	join(getTmpFilesDirectory(), `${mediaType}${generateMessageIDV2()}${suffix}`)

const normalizeReadableChunk = (chunk: Buffer | Uint8Array | string) =>
	Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk)

const writeChunkToStream = async (stream: WriteStream, chunk: Uint8Array) => {
	if (!chunk.length) {
		return
	}

	if (!stream.write(chunk)) {
		await once(stream, 'drain')
	}
}

const closeWriteStream = async (stream?: WriteStream) => {
	if (!stream) {
		return
	}

	const finishPromise = once(stream, 'finish')
	stream.end()
	await finishPromise
}

const safeUnlink = async (filePath?: string) => {
	if (!filePath) {
		return
	}

	try {
		await fs.unlink(filePath)
	} catch {
		//
	}
}

const consumeReadableStream = async (stream: Readable, onChunk: (chunk: Buffer) => Promise<void>) => {
	try {
		for await (const rawChunk of stream) {
			await onChunk(normalizeReadableChunk(rawChunk as Buffer | Uint8Array | string))
		}
	} finally {
		stream.destroy()
	}
}

const createJsHashSpoolWriter = (filePath: string): MediaHashSpoolWriter => {
	const hasher = Crypto.createHash('sha256')
	const fileWriteStream = createWriteStream(filePath)
	let fileLength = 0

	return {
		async update(chunk) {
			fileLength += chunk.length
			hasher.update(chunk)
			await writeChunkToStream(fileWriteStream, chunk)
		},
		async final() {
			await closeWriteStream(fileWriteStream)
			return {
				fileSha256: hasher.digest(),
				fileLength
			}
		},
		async abort() {
			fileWriteStream.destroy()
			await safeUnlink(filePath)
		}
	}
}

const createHashSpoolWriter = (filePath: string): MediaHashSpoolWriter => {
	if (NativeHashSpoolWriterCtor) {
		const nativeWriter: NativeHashSpoolWriter = new NativeHashSpoolWriterCtor(filePath)
		return {
			update: chunk => nativeWriter.update(chunk),
			final: () => nativeWriter.final(),
			abort: () => nativeWriter.abort()
		}
	}

	return createJsHashSpoolWriter(filePath)
}

const createJsMediaEncryptToFileWriter = (
	cipherKey: Uint8Array,
	iv: Uint8Array,
	macKey: Uint8Array,
	encFilePath: string,
	originalFilePath?: string
): MediaEncryptToFileWriter => {
	const encFileWriteStream = createWriteStream(encFilePath)
	const originalFileStream = originalFilePath ? createWriteStream(originalFilePath) : undefined
	const nativeEncryptor: NativeMediaEncryptor = new NativeMediaEncryptorCtor(cipherKey, iv, macKey)
	let fileLength = 0

	return {
		async update(chunk) {
			fileLength += chunk.length

			if (originalFileStream) {
				await writeChunkToStream(originalFileStream, chunk)
			}

			await writeChunkToStream(encFileWriteStream, nativeEncryptor.update(chunk))
		},
		async final() {
			const finalResult = nativeEncryptor.final()
			await writeChunkToStream(encFileWriteStream, finalResult.finalChunk)
			await writeChunkToStream(encFileWriteStream, finalResult.mac)
			await Promise.all([closeWriteStream(encFileWriteStream), closeWriteStream(originalFileStream)])

			return {
				mac: finalResult.mac,
				fileSha256: finalResult.fileSha256,
				fileEncSha256: finalResult.fileEncSha256,
				fileLength
			}
		},
		async abort() {
			encFileWriteStream.destroy()
			originalFileStream?.destroy?.()
			await Promise.all([safeUnlink(encFilePath), safeUnlink(originalFilePath)])
		}
	}
}

const createMediaEncryptToFileWriter = (
	cipherKey: Uint8Array,
	iv: Uint8Array,
	macKey: Uint8Array,
	encFilePath: string,
	originalFilePath?: string
): MediaEncryptToFileWriter => {
	if (NativeMediaEncryptToFileCtor) {
		const nativeWriter: NativeMediaEncryptToFile = new NativeMediaEncryptToFileCtor(
			cipherKey,
			iv,
			macKey,
			encFilePath,
			originalFilePath
		)
		return {
			update: chunk => nativeWriter.update(chunk),
			final: () => nativeWriter.final(),
			abort: () => nativeWriter.abort()
		}
	}

	return createJsMediaEncryptToFileWriter(cipherKey, iv, macKey, encFilePath, originalFilePath)
}

const getImageProcessingLibrary = async () => {
	//@ts-ignore
	const [jimp, sharp] = await Promise.all([import('jimp').catch(() => {}), import('sharp').catch(() => {})])

	if (sharp) {
		return { sharp }
	}

	if (jimp) {
		return { jimp }
	}

	throw new Boom('No image processing library available')
}

export const hkdfInfoKey = (type: MediaType) => {
	const hkdfInfo = MEDIA_HKDF_KEY_MAPPING[type]
	return `WhatsApp ${hkdfInfo} Keys`
}

export const getRawMediaUploadData = async (media: WAMediaUpload, mediaType: MediaType, logger?: ILogger) => {
	const { stream } = await getStream(media)
	logger?.debug('got stream for raw upload')

	const filePath = createTempMediaFilePath(mediaType)
	const writer = createHashSpoolWriter(filePath)

	try {
		await consumeReadableStream(stream, async data => {
			await writer.update(data)
		})
		const { fileSha256, fileLength } = await writer.final()
		logger?.debug('hashed data for raw upload')
		return {
			filePath: filePath,
			fileSha256,
			fileLength
		}
	} catch (error) {
		await writer.abort()
		throw error
	}
}

/** generates all the keys required to encrypt/decrypt & sign a media message */
export async function getMediaKeys(
	buffer: Uint8Array | string | null | undefined,
	mediaType: MediaType
): Promise<MediaDecryptionKeyInfo> {
	if (!buffer) {
		throw new Boom('Cannot derive from empty media key')
	}

	if (typeof buffer === 'string') {
		buffer = Buffer.from(buffer.replace('data:;base64,', ''), 'base64')
	}

	// expand using HKDF to 112 bytes, also pass in the relevant app info
	const expandedMediaKey = hkdf(buffer, 112, { info: hkdfInfoKey(mediaType) })
	return {
		iv: expandedMediaKey.slice(0, 16),
		cipherKey: expandedMediaKey.slice(16, 48),
		macKey: expandedMediaKey.slice(48, 80)
	}
}

/** Extracts video thumb using FFMPEG */
const extractVideoThumb = async (
	path: string,
	destPath: string,
	time: string,
	size: { width: number; height: number }
) =>
	new Promise<void>((resolve, reject) => {
		const cmd = `ffmpeg -ss ${time} -i ${path} -y -vf scale=${size.width}:-1 -vframes 1 -f image2 ${destPath}`
		exec(cmd, err => {
			if (err) {
				reject(err)
			} else {
				resolve()
			}
		})
	})

export const extractImageThumb = async (bufferOrFilePath: Readable | Buffer | string, width = 32) => {
	// Prefer sharp when available; keep jimp fallback for environments without sharp.
	if (bufferOrFilePath instanceof Readable) {
		bufferOrFilePath = await toBuffer(bufferOrFilePath)
	}

	const lib = await getImageProcessingLibrary()
	if ('sharp' in lib && typeof lib.sharp?.default === 'function') {
		const img = lib.sharp.default(bufferOrFilePath)
		const dimensions = await img.metadata()

		const buffer = await img.resize(width).jpeg({ quality: 50 }).toBuffer()
		return {
			buffer,
			original: {
				width: dimensions.width,
				height: dimensions.height
			}
		}
	} else if ('jimp' in lib && typeof lib.jimp?.Jimp === 'object') {
		const jimp = await (lib.jimp.Jimp as any).read(bufferOrFilePath)
		const dimensions = {
			width: jimp.width,
			height: jimp.height
		}
		const buffer = await jimp
			.resize({ w: width, mode: lib.jimp.ResizeStrategy.BILINEAR })
			.getBuffer('image/jpeg', { quality: 50 })
		return {
			buffer,
			original: dimensions
		}
	} else {
		throw new Boom('No image processing library available')
	}
}

export const encodeBase64EncodedStringForUpload = (b64: string) =>
	encodeURIComponent(b64.replace(/\+/g, '-').replace(/\//g, '_').replace(/\=+$/, ''))

export const generateProfilePicture = async (
	mediaUpload: WAMediaUpload,
	dimensions?: { width: number; height: number }
) => {
	let buffer: Buffer

	const { width: w = 640, height: h = 640 } = dimensions || {}

	if (Buffer.isBuffer(mediaUpload)) {
		buffer = mediaUpload
	} else {
		// Use getStream to handle all WAMediaUpload types (Buffer, Stream, URL)
		const { stream } = await getStream(mediaUpload)
		// Convert the resulting stream to a buffer
		buffer = await toBuffer(stream)
	}

	const lib = await getImageProcessingLibrary()
	let img: Promise<Buffer>
	if ('sharp' in lib && typeof lib.sharp?.default === 'function') {
		img = lib.sharp
			.default(buffer)
			.resize(w, h)
			.jpeg({
				quality: 50
			})
			.toBuffer()
	} else if ('jimp' in lib && typeof lib.jimp?.Jimp === 'function') {
		const jimp = await (lib.jimp.Jimp as any).read(buffer)
		const min = Math.min(jimp.width, jimp.height)
		const cropped = jimp.crop({ x: 0, y: 0, w: min, h: min })

		img = cropped.resize({ w, h, mode: lib.jimp.ResizeStrategy.BILINEAR }).getBuffer('image/jpeg', { quality: 50 })
	} else {
		throw new Boom('No image processing library available')
	}

	return {
		img: await img
	}
}

/** gets the SHA256 of the given media message */
export const mediaMessageSHA256B64 = (message: WAMessageContent) => {
	const media = Object.values(message)[0] as WAGenericMediaMessage
	return media?.fileSha256 && Buffer.from(media.fileSha256).toString('base64')
}

const tryNativeAnalyzeWavAudio = (data: Uint8Array, sampleCount?: number) => {
	const analyzed = nativeAnalyzeWavAudioFast(data, sampleCount)
	if (analyzed && typeof analyzed.durationSec === 'number') {
		return analyzed
	}

	throw new Error('native analyzeWavAudioFast returned invalid payload')
}

export async function getAudioDuration(buffer: Buffer | string | Readable) {
	if (Buffer.isBuffer(buffer)) {
		const analyzed = tryNativeAnalyzeWavAudio(buffer)
		if (analyzed) {
			return analyzed.durationSec
		}
	} else if (typeof buffer === 'string' && buffer.toLowerCase().endsWith('.wav')) {
		try {
			const wavBuffer = await fs.readFile(buffer)
			const analyzed = tryNativeAnalyzeWavAudio(wavBuffer)
			if (analyzed) {
				return analyzed.durationSec
			}
		} catch {}
	}

	const musicMetadata = await import('music-metadata')
	let metadata: IAudioMetadata
	const options = {
		duration: true
	}
	if (Buffer.isBuffer(buffer)) {
		metadata = await musicMetadata.parseBuffer(buffer, undefined, options)
	} else if (typeof buffer === 'string') {
		metadata = await musicMetadata.parseFile(buffer, options)
	} else {
		metadata = await musicMetadata.parseStream(buffer, undefined, options)
	}

	return metadata.format.duration
}

/**
  referenced from and modifying https://github.com/wppconnect-team/wa-js/blob/main/src/chat/functions/prepareAudioWaveform.ts
 */
export async function getAudioWaveform(buffer: Buffer | string | Readable, logger?: ILogger) {
	try {
		let audioData: Buffer
		if (Buffer.isBuffer(buffer)) {
			audioData = buffer
		} else if (typeof buffer === 'string') {
			const rStream = createReadStream(buffer)
			audioData = await toBuffer(rStream)
		} else {
			audioData = await toBuffer(buffer)
		}

		const nativeAnalyzed = tryNativeAnalyzeWavAudio(audioData, 64)
		if (nativeAnalyzed?.waveform) {
			return new Uint8Array(nativeAnalyzed.waveform)
		}

		// @ts-ignore
		const { default: decoder } = await import('audio-decode')
		const audioBuffer = await decoder(audioData)

		const rawData = audioBuffer.getChannelData(0) // We only need to work with one channel of data
		const samples = 64 // Number of samples we want to have in our final data set
		const blockSize = Math.floor(rawData.length / samples) // the number of samples in each subdivision
		const filteredData: number[] = []
		for (let i = 0; i < samples; i++) {
			const blockStart = blockSize * i // the location of the first sample in the block
			let sum = 0
			for (let j = 0; j < blockSize; j++) {
				sum = sum + Math.abs(rawData[blockStart + j]) // find the sum of all the samples in the block
			}

			filteredData.push(sum / blockSize) // divide the sum by the block size to get the average
		}

		// This guarantees that the largest data point will be set to 1, and the rest of the data will scale proportionally.
		const multiplier = Math.pow(Math.max(...filteredData), -1)
		const normalizedData = filteredData.map(n => n * multiplier)

		// Generate waveform like WhatsApp
		const waveform = new Uint8Array(normalizedData.map(n => Math.floor(100 * n)))

		return waveform
	} catch (e) {
		logger?.debug('Failed to generate waveform: ' + e)
	}
}

export const toReadable = (buffer: Buffer) => {
	const readable = new Readable({ read: () => {} })
	readable.push(buffer)
	readable.push(null)
	return readable
}

export const toBuffer = async (stream: Readable) => {
	const builder: NativeBufferBuilder = new NativeBufferBuilderCtor()
	for await (const chunk of stream) {
		builder.append(chunk)
	}

	stream.destroy()
	return builder.toBuffer(true)
}

export const getStream = async (item: WAMediaUpload, opts?: RequestInit & { maxContentLength?: number }) => {
	if (Buffer.isBuffer(item)) {
		return { stream: toReadable(item), type: 'buffer' } as const
	}

	if ('stream' in item) {
		return { stream: item.stream, type: 'readable' } as const
	}

	const urlStr = item.url.toString()

	if (urlStr.startsWith('data:')) {
		const buffer = Buffer.from(urlStr.split(',')[1]!, 'base64')
		return { stream: toReadable(buffer), type: 'buffer' } as const
	}

	if (urlStr.startsWith('http://') || urlStr.startsWith('https://')) {
		return { stream: await getHttpStream(item.url, opts), type: 'remote' } as const
	}

	return { stream: createReadStream(item.url), type: 'file' } as const
}

/** generates a thumbnail for a given media, if required */
export async function generateThumbnail(
	file: string,
	mediaType: 'video' | 'image',
	options: {
		logger?: ILogger
	}
) {
	let thumbnail: string | undefined
	let originalImageDimensions: { width: number; height: number } | undefined
	if (mediaType === 'image') {
		const { buffer, original } = await extractImageThumb(file)
		thumbnail = buffer.toString('base64')
		if (original.width && original.height) {
			originalImageDimensions = {
				width: original.width,
				height: original.height
			}
		}
	} else if (mediaType === 'video') {
		const imgFilename = join(getTmpFilesDirectory(), generateMessageIDV2() + '.jpg')
		try {
			await extractVideoThumb(file, imgFilename, '00:00:00', { width: 32, height: 32 })
			const buff = await fs.readFile(imgFilename)
			thumbnail = buff.toString('base64')

			await fs.unlink(imgFilename)
		} catch (err) {
			options.logger?.debug('could not generate video thumb: ' + err)
		}
	}

	return {
		thumbnail,
		originalImageDimensions
	}
}

export const getHttpStream = async (url: string | URL, options: RequestInit & { isStream?: true } = {}) => {
	const response = await fetch(url.toString(), {
		dispatcher: options.dispatcher,
		method: 'GET',
		headers: options.headers as HeadersInit
	})
	if (!response.ok) {
		throw new Boom(`Failed to fetch stream from ${url}`, { statusCode: response.status, data: { url } })
	}

	// @ts-ignore Node18+ Readable.fromWeb exists
	return response.body instanceof Readable ? response.body : Readable.fromWeb(response.body as any)
}

type EncryptedStreamOptions = {
	saveOriginalFileIfRequired?: boolean
	logger?: ILogger
	opts?: RequestInit
}

export const encryptedStream = async (
	media: WAMediaUpload,
	mediaType: MediaType,
	{ logger, saveOriginalFileIfRequired, opts }: EncryptedStreamOptions = {}
) => {
	const { stream, type } = await getStream(media, opts)

	logger?.debug('fetched media stream')

	const mediaKey = Crypto.randomBytes(32)
	const { cipherKey, iv, macKey } = await getMediaKeys(mediaKey, mediaType)

	const encFilePath = createTempMediaFilePath(mediaType, '-enc')
	const originalFilePath = saveOriginalFileIfRequired ? createTempMediaFilePath(mediaType, '-original') : undefined
	const writer = createMediaEncryptToFileWriter(cipherKey, iv, macKey!, encFilePath, originalFilePath)
	let totalBytesRead = 0

	try {
		await consumeReadableStream(stream, async data => {
			totalBytesRead += data.length
			if (
				type === 'remote' &&
				(opts as any)?.maxContentLength &&
				totalBytesRead > (opts as any).maxContentLength
			) {
				throw new Boom(`content length exceeded when encrypting "${type}"`, {
					data: { media, type }
				})
			}

			await writer.update(data)
		})
		const { mac, fileSha256, fileEncSha256, fileLength } = await writer.final()

		logger?.debug('encrypted data successfully')

		return {
			mediaKey,
			originalFilePath,
			encFilePath,
			mac,
			fileEncSha256,
			fileSha256,
			fileLength
		}
	} catch (error) {
		try {
			await writer.abort()
		} catch (err) {
			logger?.error({ err }, 'failed deleting tmp files')
		}

		throw error
	}
}

const DEF_HOST = 'mmg.whatsapp.net'
const AES_CHUNK_SIZE = 16

const toSmallestChunkSize = (num: number) => {
	return Math.floor(num / AES_CHUNK_SIZE) * AES_CHUNK_SIZE
}

export type MediaDownloadOptions = {
	startByte?: number
	endByte?: number
	options?: RequestInit
}

export const getUrlFromDirectPath = (directPath: string) => `https://${DEF_HOST}${directPath}`

export const downloadContentFromMessage = async (
	{ mediaKey, directPath, url }: DownloadableMessage,
	type: MediaType,
	opts: MediaDownloadOptions = {}
) => {
	const isValidMediaUrl = url?.startsWith('https://mmg.whatsapp.net/')
	const downloadUrl = isValidMediaUrl ? url : getUrlFromDirectPath(directPath!)
	if (!downloadUrl) {
		throw new Boom('No valid media URL or directPath present in message', { statusCode: 400 })
	}

	const keys = await getMediaKeys(mediaKey, type)

	return downloadEncryptedContent(downloadUrl, keys, opts)
}

/**
 * Decrypts and downloads an AES256-CBC encrypted file given the keys.
 * Assumes the SHA256 of the plaintext is appended to the end of the ciphertext
 * */
export const downloadEncryptedContent = async (
	downloadUrl: string,
	{ cipherKey, iv }: MediaDecryptionKeyInfo,
	{ startByte, endByte, options }: MediaDownloadOptions = {}
) => {
	const hasStartByte = typeof startByte === 'number' && startByte > 0
	const hasEndByte = typeof endByte === 'number'
	let bytesFetched = 0
	let startChunk = 0
	let firstBlockIsIV = false
	// if a start byte is specified -- then we need to fetch the previous chunk as that will form the IV
	if (hasStartByte) {
		const chunk = toSmallestChunkSize(startByte)
		if (chunk) {
			startChunk = chunk - AES_CHUNK_SIZE
			bytesFetched = chunk

			firstBlockIsIV = true
		}
	}

	const endChunk = hasEndByte ? toSmallestChunkSize(endByte) + AES_CHUNK_SIZE : undefined

	const headersInit = options?.headers ? options.headers : undefined
	const headers: Record<string, string> = {
		...(headersInit
			? Array.isArray(headersInit)
				? Object.fromEntries(headersInit)
				: (headersInit as Record<string, string>)
			: {}),
		Origin: DEFAULT_ORIGIN
	}
	if (startChunk || endChunk) {
		headers.Range = `bytes=${startChunk}-`
		if (endChunk) {
			headers.Range += `${Math.max(startChunk, endChunk - 1)}`
		}
	}

	// download the message
	const fetched = await getHttpStream(downloadUrl, {
		...(options || {}),
		headers
	})

	const nativeDecryptPipeline: NativeMediaDecryptPipeline = new NativeMediaDecryptPipelineCtor(cipherKey, iv, {
		firstBlockIsIV: firstBlockIsIV,
		autoPadding: !hasEndByte,
		startByte,
		endByte,
		initialOffset: bytesFetched
	})

	const output = new Transform({
		transform(chunk, _, callback) {
			try {
				const decrypted = nativeDecryptPipeline.update(chunk)
				if (decrypted.length > 0) {
					this.push(decrypted)
				}

				callback()
			} catch (error: any) {
				callback(error)
			}
		},
		final(callback) {
			try {
				const finalBytes = nativeDecryptPipeline.final()
				if (finalBytes.length > 0) {
					this.push(finalBytes)
				}

				callback()
			} catch (error: any) {
				callback(error)
			}
		}
	})
	return fetched.pipe(output, { end: true })
}

export function extensionForMediaMessage(message: WAMessageContent) {
	const getExtension = (mimetype: string) => mimetype.split(';')[0]?.split('/')[1]
	const type = Object.keys(message)[0] as Exclude<MessageType, 'toJSON'>
	let extension: string
	if (type === 'locationMessage' || type === 'liveLocationMessage' || type === 'productMessage') {
		extension = '.jpeg'
	} else {
		const messageContent = message[type] as WAGenericMediaMessage
		extension = getExtension(messageContent.mimetype!)!
	}

	return extension
}

const isNodeRuntime = (): boolean => {
	return (
		typeof process !== 'undefined' &&
		process.versions?.node !== null &&
		typeof process.versions.bun === 'undefined' &&
		typeof (globalThis as any).Deno === 'undefined'
	)
}

type MediaUploadResult = {
	url?: string
	direct_path?: string
	meta_hmac?: string
	ts?: number
	fbid?: number
}

export type UploadParams = {
	url: string
	filePath: string
	headers: Record<string, string>
	timeoutMs?: number
	agent?: Agent
}

export const uploadWithNodeHttp = async (
	{ url, filePath, headers, timeoutMs, agent }: UploadParams,
	redirectCount = 0
): Promise<MediaUploadResult | undefined> => {
	if (redirectCount > 5) {
		throw new Error('Too many redirects')
	}

	const parsedUrl = new URL(url)
	const httpModule = parsedUrl.protocol === 'https:' ? await import('https') : await import('http')

	// Get file size for Content-Length header (required for Node.js streaming)
	const fileStats = await fs.stat(filePath)
	const fileSize = fileStats.size

	return new Promise((resolve, reject) => {
		const req = httpModule.request(
			{
				hostname: parsedUrl.hostname,
				port: parsedUrl.port || (parsedUrl.protocol === 'https:' ? 443 : 80),
				path: parsedUrl.pathname + parsedUrl.search,
				method: 'POST',
				headers: {
					...headers,
					'Content-Length': fileSize
				},
				agent,
				timeout: timeoutMs
			},
			res => {
				// Handle redirects (3xx)
				if (res.statusCode && res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
					res.resume() // Consume response to free resources
					const newUrl = new URL(res.headers.location, url).toString()
					resolve(
						uploadWithNodeHttp(
							{
								url: newUrl,
								filePath,
								headers,
								timeoutMs,
								agent
							},
							redirectCount + 1
						)
					)
					return
				}

				let body = ''
				res.on('data', chunk => (body += chunk))
				res.on('end', () => {
					try {
						resolve(JSON.parse(body))
					} catch {
						resolve(undefined)
					}
				})
			}
		)

		req.on('error', reject)
		req.on('timeout', () => {
			req.destroy()
			reject(new Error('Upload timeout'))
		})

		const stream = createReadStream(filePath)
		stream.pipe(req)
		stream.on('error', err => {
			req.destroy()
			reject(err)
		})
	})
}

const uploadWithFetch = async ({
	url,
	filePath,
	headers,
	timeoutMs,
	agent
}: UploadParams): Promise<MediaUploadResult | undefined> => {
	// Convert Node.js Readable to Web ReadableStream
	const nodeStream = createReadStream(filePath)
	const webStream = Readable.toWeb(nodeStream) as ReadableStream

	const response = await fetch(url, {
		dispatcher: agent,
		method: 'POST',
		body: webStream,
		headers,
		duplex: 'half',
		signal: timeoutMs ? AbortSignal.timeout(timeoutMs) : undefined
	})

	try {
		return (await response.json()) as MediaUploadResult
	} catch {
		return undefined
	}
}

/**
 * Uploads media to WhatsApp servers.
 *
 * ## Why we have two upload implementations:
 *
 * Node.js's native `fetch` (powered by undici) has a known bug where it buffers
 * the entire request body in memory before sending, even when using streams.
 * This causes memory issues with large files (e.g., 1GB file = 1GB+ memory usage).
 * See: https://github.com/nodejs/undici/issues/4058
 *
 * Other runtimes (Bun, Deno, browsers) correctly stream the request body without
 * buffering, so we can use the web-standard Fetch API there.
 *
 * ## Future considerations:
 * Once the undici bug is fixed, we can simplify this to use only the Fetch API
 * across all runtimes. Monitor the GitHub issue for updates.
 */
const uploadMedia = async (params: UploadParams, logger?: ILogger): Promise<MediaUploadResult | undefined> => {
	if (isNodeRuntime()) {
		logger?.debug('Using Node.js https module for upload (avoids undici buffering bug)')
		return uploadWithNodeHttp(params)
	} else {
		logger?.debug('Using web-standard Fetch API for upload')
		return uploadWithFetch(params)
	}
}

export const getWAUploadToServer = (
	{ customUploadHosts, fetchAgent, logger, options }: SocketConfig,
	refreshMediaConn: (force: boolean) => Promise<MediaConnInfo>
): WAMediaUploadFunction => {
	return async (filePath, { mediaType, fileEncSha256B64, timeoutMs }) => {
		// send a query JSON to obtain the url & auth token to upload our media
		let uploadInfo = await refreshMediaConn(false)

		let urls: { mediaUrl: string; directPath: string; meta_hmac?: string; ts?: number; fbid?: number } | undefined
		const hosts = [...customUploadHosts, ...uploadInfo.hosts]

		fileEncSha256B64 = encodeBase64EncodedStringForUpload(fileEncSha256B64)

		// Prepare common headers
		const customHeaders = (() => {
			const hdrs = options?.headers
			if (!hdrs) return {}
			return Array.isArray(hdrs) ? Object.fromEntries(hdrs) : (hdrs as Record<string, string>)
		})()

		const headers = {
			...customHeaders,
			'Content-Type': 'application/octet-stream',
			Origin: DEFAULT_ORIGIN
		}

		for (const { hostname } of hosts) {
			logger.debug(`uploading to "${hostname}"`)

			const auth = encodeURIComponent(uploadInfo.auth)
			const url = `https://${hostname}${MEDIA_PATH_MAP[mediaType]}/${fileEncSha256B64}?auth=${auth}&token=${fileEncSha256B64}`

			let result: MediaUploadResult | undefined
			try {
				result = await uploadMedia(
					{
						url,
						filePath,
						headers,
						timeoutMs,
						agent: fetchAgent
					},
					logger
				)

				if (result?.url || result?.direct_path) {
					urls = {
						mediaUrl: result.url!,
						directPath: result.direct_path!,
						meta_hmac: result.meta_hmac,
						fbid: result.fbid,
						ts: result.ts
					}
					break
				} else {
					uploadInfo = await refreshMediaConn(true)
					throw new Error(`upload failed, reason: ${JSON.stringify(result)}`)
				}
			} catch (error: any) {
				const isLast = hostname === hosts[uploadInfo.hosts.length - 1]?.hostname
				logger.warn(
					{ trace: error?.stack, uploadResult: result },
					`Error in uploading to ${hostname} ${isLast ? '' : ', retrying...'}`
				)
			}
		}

		if (!urls) {
			throw new Boom('Media upload failed on all hosts', { statusCode: 500 })
		}

		return urls
	}
}

const getMediaRetryKey = (mediaKey: Buffer | Uint8Array) => {
	return hkdf(mediaKey, 32, { info: 'WhatsApp Media Retry Notification' })
}

/**
 * Generate a binary node that will request the phone to re-upload the media & return the newly uploaded URL
 */
export const encryptMediaRetryRequest = (key: WAMessageKey, mediaKey: Buffer | Uint8Array, meId: string) => {
	const recp: proto.IServerErrorReceipt = { stanzaId: key.id }
	const recpBuffer = proto.ServerErrorReceipt.encode(recp).finish()

	const iv = Crypto.randomBytes(12)
	const retryKey = getMediaRetryKey(mediaKey)
	const ciphertext = aesEncryptGCM(recpBuffer, retryKey, iv, Buffer.from(key.id!))

	const req: BinaryNode = {
		tag: 'receipt',
		attrs: {
			id: key.id!,
			to: jidNormalizedUser(meId),
			type: 'server-error'
		},
		content: [
			// this encrypt node is actually pretty useless
			// the media is returned even without this node
			// keeping it here to maintain parity with WA Web
			{
				tag: 'encrypt',
				attrs: {},
				content: [
					{ tag: 'enc_p', attrs: {}, content: ciphertext },
					{ tag: 'enc_iv', attrs: {}, content: iv }
				]
			},
			{
				tag: 'rmr',
				attrs: {
					jid: key.remoteJid!,
					from_me: (!!key.fromMe).toString(),
					// @ts-ignore
					participant: key.participant || undefined
				}
			}
		]
	}

	return req
}

export const decodeMediaRetryNode = (node: BinaryNode) => {
	const rmrNode = getBinaryNodeChild(node, 'rmr')!

	const event: BaileysEventMap['messages.media-update'][number] = {
		key: {
			id: node.attrs.id,
			remoteJid: rmrNode.attrs.jid,
			fromMe: rmrNode.attrs.from_me === 'true',
			participant: rmrNode.attrs.participant
		}
	}

	const errorNode = getBinaryNodeChild(node, 'error')
	if (errorNode) {
		const errorCode = +errorNode.attrs.code!
		event.error = new Boom(`Failed to re-upload media (${errorCode})`, {
			data: errorNode.attrs,
			statusCode: getStatusCodeForMediaRetry(errorCode)
		})
	} else {
		const encryptedInfoNode = getBinaryNodeChild(node, 'encrypt')
		const ciphertext = getBinaryNodeChildBuffer(encryptedInfoNode, 'enc_p')
		const iv = getBinaryNodeChildBuffer(encryptedInfoNode, 'enc_iv')
		if (ciphertext && iv) {
			event.media = { ciphertext, iv }
		} else {
			event.error = new Boom('Failed to re-upload media (missing ciphertext)', { statusCode: 404 })
		}
	}

	return event
}

export const decryptMediaRetryData = (
	{ ciphertext, iv }: { ciphertext: Uint8Array; iv: Uint8Array },
	mediaKey: Uint8Array,
	msgId: string
) => {
	const retryKey = getMediaRetryKey(mediaKey)
	const plaintext = aesDecryptGCM(ciphertext, retryKey, iv, Buffer.from(msgId))
	return proto.MediaRetryNotification.decode(plaintext)
}

export const getStatusCodeForMediaRetry = (code: number) =>
	MEDIA_RETRY_STATUS_MAP[code as proto.MediaRetryNotification.ResultType]

const MEDIA_RETRY_STATUS_MAP = {
	[proto.MediaRetryNotification.ResultType.SUCCESS]: 200,
	[proto.MediaRetryNotification.ResultType.DECRYPTION_ERROR]: 412,
	[proto.MediaRetryNotification.ResultType.NOT_FOUND]: 404,
	[proto.MediaRetryNotification.ResultType.GENERAL_ERROR]: 418
} as const
