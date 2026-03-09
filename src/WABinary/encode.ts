import * as constants from './constants'
import { getNativeWABinaryCodec } from './native-codec'
import type { BinaryNode, BinaryNodeCodingOptions } from './types'

export const encodeBinaryNode = (
	node: BinaryNode,
	opts: Pick<BinaryNodeCodingOptions, 'TAGS' | 'TOKEN_MAP'> = constants,
	buffer: number[] = [0]
): Buffer => {
	if (!Array.isArray(buffer) || buffer.length !== 1 || buffer[0] !== 0) {
		throw new Error('strict native mode requires default buffer marker [0] for encodeBinaryNode')
	}

	const nativeCodec = getNativeWABinaryCodec(opts as unknown)
	return nativeCodec.encodeWABinaryNode(node, true)
}
