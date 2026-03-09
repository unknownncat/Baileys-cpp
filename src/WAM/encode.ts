import { type NativeWAMEncodeItem, requireNativeExport } from '../Native/baileys-native'
import { BinaryInfo } from './BinaryInfo'
import { FLAG_EVENT, FLAG_EXTENDED, FLAG_FIELD, FLAG_GLOBAL, type Value, WEB_EVENTS, WEB_GLOBALS } from './constants'

const nativeEncodeWAMFast = requireNativeExport('encodeWAMFast')
const webEventsByName = new Map(WEB_EVENTS.map(event => [event.name, event]))
const webGlobalsByName = new Map(WEB_GLOBALS.map(global => [global.name, global.id]))

const normalizeValue = (value: Value | boolean): Value => {
	if (typeof value === 'boolean') {
		return value ? 1 : 0
	}

	return value
}

export const encodeWAM = (binaryInfo: BinaryInfo) => {
	const entries: NativeWAMEncodeItem[] = []
	collectWAMEntries(binaryInfo, (key, value, flag) => entries.push({ key, value, flag }))
	const encoded = nativeEncodeWAMFast(binaryInfo.protocolVersion, binaryInfo.sequence, entries)
	if (!Buffer.isBuffer(encoded)) {
		throw new Error('native encodeWAMFast returned invalid payload')
	}

	binaryInfo.buffer = [encoded]
	return encoded
}

function collectGlobalAttributes(
	globals: { [key: string]: Value },
	consume: (key: number, value: Value, flag: number) => void
) {
	for (const [key, _value] of Object.entries(globals)) {
		const id = webGlobalsByName.get(key)
		if (typeof id === 'undefined') {
			throw new Error(`Unknown WAM global "${key}"`)
		}

		consume(id, normalizeValue(_value), FLAG_GLOBAL)
	}
}

function collectWAMEntries(binaryInfo: BinaryInfo, consume: (key: number, value: Value, flag: number) => void) {
	for (const [name, { props, globals }] of binaryInfo.events.map(a => Object.entries(a)[0]!)) {
		collectGlobalAttributes(globals, consume)
		const event = webEventsByName.get(name)
		if (!event) {
			throw new Error(`Unknown WAM event "${name}"`)
		}

		const props_ = Object.entries(props)

		let extended = false

		for (const [, value] of props_) {
			extended ||= value !== null
		}

		const eventFlag = extended ? FLAG_EVENT : FLAG_EVENT | FLAG_EXTENDED
		consume(event.id, -event.weight, eventFlag)

		for (let i = 0; i < props_.length; i++) {
			const [key, _value] = props_[i]!
			const id = event.props[key]?.[0]
			if (typeof id === 'undefined') {
				throw new Error(`Unknown WAM field "${name}.${key}"`)
			}

			extended = i < props_.length - 1
			const value = normalizeValue(_value)

			const fieldFlag = extended ? FLAG_EVENT : FLAG_FIELD | FLAG_EXTENDED
			consume(id, value, fieldFlag)
		}
	}
}
