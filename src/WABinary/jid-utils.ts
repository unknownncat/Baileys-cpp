import { requireNativeExport } from '../Native/baileys-native'

export const S_WHATSAPP_NET = '@s.whatsapp.net'
export const OFFICIAL_BIZ_JID = '16505361212@c.us'
export const SERVER_JID = 'server@c.us'
export const PSA_WID = '0@c.us'
export const STORIES_JID = 'status@broadcast'
export const META_AI_JID = '13135550002@c.us'

export type JidServer =
	| 'c.us'
	| 'g.us'
	| 'broadcast'
	| 's.whatsapp.net'
	| 'call'
	| 'lid'
	| 'newsletter'
	| 'bot'
	| 'hosted'
	| 'hosted.lid'

export enum WAJIDDomains {
	WHATSAPP = 0,
	LID = 1,
	HOSTED = 128,
	HOSTED_LID = 129
}

export type JidWithDevice = {
	user: string
	device?: number
}

export type FullJid = JidWithDevice & {
	server: JidServer
	domainType?: number
}

export const getServerFromDomainType = (initialServer: string, domainType?: WAJIDDomains): JidServer => {
	switch (domainType) {
		case WAJIDDomains.LID:
			return 'lid'
		case WAJIDDomains.HOSTED:
			return 'hosted'
		case WAJIDDomains.HOSTED_LID:
			return 'hosted.lid'
		case WAJIDDomains.WHATSAPP:
		default:
			return initialServer as JidServer
	}
}

// JID parsing/comparison sits on the binary hot path, so strict native mode routes it through the addon.
const nativeDecodeJidFast = requireNativeExport('decodeJidFast')
const nativeAreJidsSameUserFast = requireNativeExport('areJidsSameUserFast')
const nativeNormalizeJidUserFast = requireNativeExport('normalizeJidUserFast')

export const jidEncode = (user: string | number | null, server: JidServer, device?: number, agent?: number) => {
	return `${user || ''}${!!agent ? `_${agent}` : ''}${!!device ? `:${device}` : ''}@${server}`
}

export const jidDecode = (jid: string | undefined): FullJid | undefined => {
	if (typeof jid !== 'string') {
		return undefined
	}

	const decoded = nativeDecodeJidFast(jid)
	if (!decoded || typeof decoded.user !== 'string' || typeof decoded.server !== 'string') {
		return undefined
	}

	return decoded as FullJid
}

/** is the jid a user */
export const areJidsSameUser = (jid1: string | undefined, jid2: string | undefined) => {
	return nativeAreJidsSameUserFast(jid1, jid2)
}

/** is the jid Meta AI */
export const isJidMetaAI = (jid: string | undefined) => jid?.endsWith('@bot')
/** is the jid a PN user */
export const isPnUser = (jid: string | undefined) => jid?.endsWith('@s.whatsapp.net')
/** is the jid a LID */
export const isLidUser = (jid: string | undefined) => jid?.endsWith('@lid')
/** is the jid a broadcast */
export const isJidBroadcast = (jid: string | undefined) => jid?.endsWith('@broadcast')
/** is the jid a group */
export const isJidGroup = (jid: string | undefined) => jid?.endsWith('@g.us')
/** is the jid the status broadcast */
export const isJidStatusBroadcast = (jid: string) => jid === 'status@broadcast'
/** is the jid a newsletter */
export const isJidNewsletter = (jid: string | undefined) => jid?.endsWith('@newsletter')
/** is the jid a hosted PN */
export const isHostedPnUser = (jid: string | undefined) => jid?.endsWith('@hosted')
/** is the jid a hosted LID */
export const isHostedLidUser = (jid: string | undefined) => jid?.endsWith('@hosted.lid')

const botRegexp = /^1313555\d{4}$|^131655500\d{2}$/

export const isJidBot = (jid: string | undefined) => jid && botRegexp.test(jid.split('@')[0]!) && jid.endsWith('@c.us')

export const jidNormalizedUser = (jid: string | undefined) => {
	if (typeof jid !== 'string') {
		return ''
	}

	return nativeNormalizeJidUserFast(jid)
}

export const transferDevice = (fromJid: string, toJid: string) => {
	const fromDecoded = jidDecode(fromJid)
	const deviceId = fromDecoded?.device || 0
	const { server, user } = jidDecode(toJid)!
	return jidEncode(user, server, deviceId)
}
