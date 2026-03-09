import { requireNativeExport } from '../Native/baileys-native'
import * as constants from './constants'
import { getNativeWABinaryCodec } from './native-codec'
import type { BinaryNode, BinaryNodeCodingOptions } from './types'

const nativeInflateZlibBuffer = requireNativeExport('inflateZlibBuffer')

export const decompressingIfRequired = async (buffer: Buffer) => {
	if (2 & buffer.readUInt8()) {
		const compressed = buffer.slice(1)
		return nativeInflateZlibBuffer(compressed)
	}

	// nodes with no compression have a 0x00 prefix, we remove that
	return buffer.slice(1)
}

export const decodeDecompressedBinaryNode = (
	buffer: Buffer,
	opts: Pick<BinaryNodeCodingOptions, 'DOUBLE_BYTE_TOKENS' | 'SINGLE_BYTE_TOKENS' | 'TAGS'>,
	indexRef: { index: number } = { index: 0 }
): BinaryNode => {
	const nativeCodec = getNativeWABinaryCodec(opts as unknown)
	const result = nativeCodec.decodeWABinaryNode(buffer, indexRef.index)
	if (!result || typeof result.nextIndex !== 'number' || !result.node) {
		throw new Error('native decodeWABinaryNode returned invalid payload')
	}

	indexRef.index = result.nextIndex
	return result.node as BinaryNode
}

export const decodeBinaryNode = async (buff: Buffer): Promise<BinaryNode> => {
	const decompBuff = await decompressingIfRequired(buff)
	return decodeDecompressedBinaryNode(decompBuff, constants)
}
