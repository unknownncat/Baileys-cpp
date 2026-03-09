import { mkdir, stat } from 'fs/promises'
import { join } from 'path'
import { type NativeKVMutation, type NativeKVStore, requireNativeExport } from '../Native/baileys-native'
import type { AuthenticationCreds, AuthenticationState, SignalDataTypeMap } from '../Types'
import { initAuthCreds } from './auth-utils'

const STORE_FILE_NAME = 'auth-state.kv'
const CREDS_KEY = '__creds__'
const KEY_SEPARATOR = '\x1f'

const toStoreKey = (type: keyof SignalDataTypeMap, id: string) => `${type}${KEY_SEPARATOR}${id}`

const NativeKVStoreCtor = requireNativeExport('NativeKVStore')
const nativeEncodeAuthValue = requireNativeExport('encodeAuthValue')
const nativeDecodeAuthValue = requireNativeExport('decodeAuthValue')

const encodeValue = (value: unknown) => {
	return nativeEncodeAuthValue(value)
}

const decodeValue = <T>(value: Uint8Array): T => {
	return nativeDecodeAuthValue(value) as T
}

const createNativeStore = async (folder: string): Promise<NativeKVStore> => {
	const folderInfo = await stat(folder).catch(() => undefined)
	if (folderInfo) {
		if (!folderInfo.isDirectory()) {
			throw new Error(
				`found something that is not a directory at ${folder}, either delete it or specify a different location`
			)
		}
	} else {
		await mkdir(folder, { recursive: true })
	}

	return new NativeKVStoreCtor(join(folder, STORE_FILE_NAME), {
		compactThresholdBytes: 32 * 1024 * 1024,
		compactRatio: 2
	})
}

/**
 * stores the full authentication state in a native single-file KV store.
 * optimized for lower fs overhead vs multi-file json.
 */
export const useNativeAuthState = async (
	folder: string
): Promise<{ state: AuthenticationState; saveCreds: () => Promise<void> }> => {
	const store = await createNativeStore(folder)
	const credsRaw = store.get(CREDS_KEY)
	const creds = credsRaw ? decodeValue<AuthenticationCreds>(credsRaw) : initAuthCreds()

	return {
		state: {
			creds,
			keys: {
				get: async (type, ids) => {
					const data: { [_: string]: SignalDataTypeMap[typeof type] } = {}
					if (ids.length === 0) {
						return data
					}

					const rawValues = store.getMany(ids.map(id => toStoreKey(type, id)))
					for (let i = 0; i < ids.length; i++) {
						const id = ids[i]!
						const rawValue = rawValues[i]
						if (!rawValue) {
							continue
						}

						const parsed = decodeValue<SignalDataTypeMap[typeof type]>(rawValue)

						data[id] = parsed
					}

					return data
				},
				set: async data => {
					const mutations: NativeKVMutation[] = []

					for (const category in data) {
						const typedCategory = category as keyof SignalDataTypeMap
						const categoryData = data[typedCategory]
						if (!categoryData) {
							continue
						}

						for (const id in categoryData) {
							const value = categoryData[id]
							mutations.push({
								key: toStoreKey(typedCategory, id),
								value: value === null || typeof value === 'undefined' ? null : encodeValue(value)
							})
						}
					}

					if (mutations.length > 0) {
						store.setMany(mutations)
					}
				},
				clear: async () => {
					store.clear()
				}
			}
		},
		saveCreds: async () => {
			store.setMany([{ key: CREDS_KEY, value: encodeValue(creds) }])
			// Preserve the old async durability expectation of saveCreds().
			store.compact()
		}
	}
}
