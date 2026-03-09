import { requireNativeExport } from '../Native/baileys-native'
import * as constants from './constants'

const nativeInitWABinaryCodec = requireNativeExport('initWABinaryCodec')
const nativeEncodeWABinaryNode = requireNativeExport('encodeWABinaryNode')
const nativeDecodeWABinaryNode = requireNativeExport('decodeWABinaryNode')

let initialized = false
let lastOptionsRef: unknown = undefined

const nativeWABinaryCodec = {
	initWABinaryCodec: nativeInitWABinaryCodec,
	encodeWABinaryNode: nativeEncodeWABinaryNode,
	decodeWABinaryNode: nativeDecodeWABinaryNode
}

export const getNativeWABinaryCodec = (options: unknown = constants) => {
	if (!initialized || lastOptionsRef !== options) {
		initialized = true
		lastOptionsRef = options
		const ready = !!nativeInitWABinaryCodec(options as never)
		if (!ready) {
			throw new Error('strict native mode failed to initialize WABinary codec')
		}
	}

	return nativeWABinaryCodec
}
