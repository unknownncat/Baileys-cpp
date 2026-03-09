import { proto } from '../../WAProto/index.js'
import { requireNativeExport } from '../Native/baileys-native'
import type { WAMessageContent, WAMessageKey } from '../Types'
import type { BinaryNode } from '../WABinary'
import { hkdf } from './crypto'

export type ReportingField = {
	f: number
	m?: boolean
	s?: ReportingField[]
}

const nativeBuildReportingTokenV2 = requireNativeExport('buildReportingTokenV2')

const ENC_SECRET_REPORT_TOKEN = 'Report Token'

export const shouldIncludeReportingToken = (message: proto.IMessage): boolean =>
	!message.reactionMessage &&
	!message.encReactionMessage &&
	!message.encEventResponseMessage &&
	!message.pollUpdateMessage

const generateMsgSecretKey = (
	modificationType: string,
	origMsgId: string,
	origMsgSender: string,
	modificationSender: string,
	origMsgSecret: Uint8Array
) => {
	const useCaseSecret = Buffer.concat([
		Buffer.from(origMsgId, 'utf8'),
		Buffer.from(origMsgSender, 'utf8'),
		Buffer.from(modificationSender, 'utf8'),
		Buffer.from(modificationType, 'utf8')
	])

	return hkdf(origMsgSecret, 32, { info: useCaseSecret.toString('latin1') })
}

export const getMessageReportingToken = async (
	msgProtobuf: Uint8Array,
	message: WAMessageContent,
	key: WAMessageKey
): Promise<BinaryNode | null> => {
	const msgSecret = message.messageContextInfo?.messageSecret
	if (!msgSecret || !key.id) {
		return null
	}

	const from = key.fromMe ? key.remoteJid! : key.participant || key.remoteJid!
	const to = key.fromMe ? key.participant || key.remoteJid! : key.remoteJid!
	const reportingSecret = generateMsgSecretKey(ENC_SECRET_REPORT_TOKEN, key.id, from, to, msgSecret)

	const msgBuffer = Buffer.isBuffer(msgProtobuf) ? msgProtobuf : Buffer.from(msgProtobuf)
	const nativeToken = nativeBuildReportingTokenV2(msgBuffer, reportingSecret)
	if (!(nativeToken instanceof Uint8Array) || nativeToken.length !== 16) {
		throw new Error('native buildReportingTokenV2 returned invalid payload')
	}

	const reportingToken = nativeToken

	return {
		tag: 'reporting',
		attrs: {},
		content: [
			{
				tag: 'reporting_token',
				attrs: { v: '2' },
				content: reportingToken
			}
		]
	}
}
