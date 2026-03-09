import { createCipheriv, createDecipheriv, createHash, createHmac, randomBytes } from 'crypto'
import { crypto as libsignalCrypto } from 'libsignal'
import { KEY_BUNDLE_TYPE } from '../Defaults'
import { requireNativeExport } from '../Native/baileys-native'
import type { KeyPair } from '../Types'
import {
	calculateAgreement as bridgeCalculateAgreement,
	calculateSignature as bridgeCalculateSignature,
	generateKeyPair as bridgeGenerateKeyPair,
	hkdf,
	md5,
	verifySignature as bridgeVerifySignature
} from 'whatsapp-rust-bridge'
export { md5, hkdf }

// insure browser & node compatibility
const { subtle } = globalThis.crypto
const nativeGenerateSignalPubKeyFast = requireNativeExport('generateSignalPubKeyFast')

/** prefix version byte to the pub keys, required for some curve crypto functions */
// This keeps libsignal and the Rust bridge aligned on the exact public-key wire format.
export const generateSignalPubKey = (pubKey: Uint8Array | Buffer) => {
	const fast = nativeGenerateSignalPubKeyFast(pubKey, KEY_BUNDLE_TYPE[0])
	if (fast instanceof Uint8Array && fast.length > 0) {
		return fast
	}

	throw new Error('native generateSignalPubKeyFast returned invalid payload')
}

type RawSignalKeyPair = {
	privKey: Uint8Array
	pubKey: Uint8Array
}

export const generateSignalKeyPairRaw = (): RawSignalKeyPair => {
	return bridgeGenerateKeyPair()
}

// Keep an in-process HKDF fallback for environments where libsignal's deriveSecrets path is unavailable.
export const deriveSignalSecrets = (
	input: Uint8Array,
	salt: Uint8Array,
	info: Uint8Array,
	chunks = 3
): Uint8Array[] => {
	const resolvedChunks = chunks as 1 | 2 | 3
	if (salt.byteLength !== 32) {
		throw new Error('Got salt of incorrect length')
	}
	if (resolvedChunks < 1 || resolvedChunks > 3) {
		throw new Error('deriveSignalSecrets chunks must be between 1 and 3')
	}

	try {
		return libsignalCrypto.deriveSecrets(input, salt, info, resolvedChunks)
	} catch {
		const prk = createHmac('sha256', Buffer.from(salt)).update(Buffer.from(input)).digest()
		const derived: Uint8Array[] = []
		let previous: Uint8Array<ArrayBufferLike> = Buffer.alloc(0)

		for (let i = 1; i <= resolvedChunks; i += 1) {
			previous = createHmac('sha256', prk)
				.update(previous)
				.update(Buffer.from(info))
				.update(Buffer.from([i]))
				.digest()
			derived.push(previous)
		}

		return derived
	}
}

export const Curve = {
	generateKeyPair: (): KeyPair => {
		const { pubKey, privKey } = generateSignalKeyPairRaw()
		return {
			private: Buffer.from(privKey),
			// remove version byte
			public: Buffer.from(pubKey.slice(1))
		}
	},
	sharedKey: (privateKey: Uint8Array, publicKey: Uint8Array) => {
		const shared = bridgeCalculateAgreement(generateSignalPubKey(publicKey), privateKey)
		return Buffer.from(shared)
	},
	sign: (privateKey: Uint8Array, buf: Uint8Array) => bridgeCalculateSignature(privateKey, buf),
	verify: (pubKey: Uint8Array, message: Uint8Array, signature: Uint8Array) => {
		try {
			return bridgeVerifySignature(generateSignalPubKey(pubKey), message, signature)
		} catch (error) {
			return false
		}
	}
}

export const signedKeyPair = (identityKeyPair: KeyPair, keyId: number) => {
	const preKey = Curve.generateKeyPair()
	const pubKey = generateSignalPubKey(preKey.public)

	const signature = Curve.sign(identityKeyPair.private, pubKey)

	return { keyPair: preKey, signature, keyId }
}

const GCM_TAG_LENGTH = 128 >> 3

/**
 * encrypt AES 256 GCM;
 * where the tag tag is suffixed to the ciphertext
 * */
export function aesEncryptGCM(plaintext: Uint8Array, key: Uint8Array, iv: Uint8Array, additionalData: Uint8Array) {
	const cipher = createCipheriv('aes-256-gcm', key, iv)
	cipher.setAAD(additionalData)
	return Buffer.concat([cipher.update(plaintext), cipher.final(), cipher.getAuthTag()])
}

/**
 * decrypt AES 256 GCM;
 * where the auth tag is suffixed to the ciphertext
 * */
export function aesDecryptGCM(ciphertext: Uint8Array, key: Uint8Array, iv: Uint8Array, additionalData: Uint8Array) {
	const decipher = createDecipheriv('aes-256-gcm', key, iv)
	// decrypt additional adata
	const enc = ciphertext.slice(0, ciphertext.length - GCM_TAG_LENGTH)
	const tag = ciphertext.slice(ciphertext.length - GCM_TAG_LENGTH)
	// set additional data
	decipher.setAAD(additionalData)
	decipher.setAuthTag(tag)

	return Buffer.concat([decipher.update(enc), decipher.final()])
}

export function aesEncryptCTR(plaintext: Uint8Array, key: Uint8Array, iv: Uint8Array) {
	const cipher = createCipheriv('aes-256-ctr', key, iv)
	return Buffer.concat([cipher.update(plaintext), cipher.final()])
}

export function aesDecryptCTR(ciphertext: Uint8Array, key: Uint8Array, iv: Uint8Array) {
	const decipher = createDecipheriv('aes-256-ctr', key, iv)
	return Buffer.concat([decipher.update(ciphertext), decipher.final()])
}

/** decrypt AES 256 CBC; where the IV is prefixed to the buffer */
export function aesDecrypt(buffer: Uint8Array, key: Uint8Array) {
	return aesDecryptWithIV(buffer.subarray(16), key, buffer.subarray(0, 16))
}

/** decrypt AES 256 CBC */
export function aesDecryptWithIV(buffer: Uint8Array, key: Uint8Array, IV: Uint8Array) {
	const aes = createDecipheriv('aes-256-cbc', key, IV)
	return Buffer.concat([aes.update(buffer), aes.final()])
}

// encrypt AES 256 CBC; where a random IV is prefixed to the buffer
export function aesEncrypt(buffer: Uint8Array, key: Uint8Array) {
	const IV = randomBytes(16)
	const aes = createCipheriv('aes-256-cbc', key, IV)
	return Buffer.concat([IV, aes.update(buffer), aes.final()]) // prefix IV to the buffer
}

// encrypt AES 256 CBC with a given IV
export function aesEncrypWithIV(buffer: Buffer, key: Buffer, IV: Buffer) {
	const aes = createCipheriv('aes-256-cbc', key, IV)
	return Buffer.concat([aes.update(buffer), aes.final()]) // prefix IV to the buffer
}

// sign HMAC using SHA 256
export function hmacSign(
	buffer: Buffer | Uint8Array,
	key: Buffer | Uint8Array,
	variant: 'sha256' | 'sha512' = 'sha256'
) {
	return createHmac(variant, key).update(buffer).digest()
}

export function sha256(buffer: Buffer) {
	return createHash('sha256').update(buffer).digest()
}

export async function derivePairingCodeKey(pairingCode: string, salt: Buffer): Promise<Buffer> {
	// Convert inputs to formats Web Crypto API can work with
	const encoder = new TextEncoder()
	const pairingCodeBuffer = encoder.encode(pairingCode)
	const saltBuffer = new Uint8Array(salt instanceof Uint8Array ? salt : new Uint8Array(salt))

	// Import the pairing code as key material
	const keyMaterial = await subtle.importKey('raw', pairingCodeBuffer as BufferSource, { name: 'PBKDF2' }, false, [
		'deriveBits'
	])

	// Derive bits using PBKDF2 with the same parameters
	// 2 << 16 = 131,072 iterations
	const derivedBits = await subtle.deriveBits(
		{
			name: 'PBKDF2',
			salt: saltBuffer as BufferSource,
			iterations: 2 << 16,
			hash: 'SHA-256'
		},
		keyMaterial,
		32 * 8 // 32 bytes * 8 = 256 bits
	)

	return Buffer.from(derivedBits)
}
