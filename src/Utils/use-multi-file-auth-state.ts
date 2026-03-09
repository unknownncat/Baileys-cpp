import type { AuthenticationState } from '../Types'
import { useNativeAuthState } from './use-native-auth-state'

/**
 * Strict native mode keeps the legacy multi-file API as a compatibility entry point.
 * Persisted state is always served by the native KV backend behind useNativeAuthState().
 */
export const useMultiFileAuthState = async (
	folder: string
): Promise<{ state: AuthenticationState; saveCreds: () => Promise<void> }> => {
	return useNativeAuthState(folder)
}
