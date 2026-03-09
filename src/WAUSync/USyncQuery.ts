import { requireNativeExport } from '../Native/baileys-native'
import type { USyncQueryProtocol } from '../Types/USync'
import { type BinaryNode, getBinaryNodeChild } from '../WABinary'
import { USyncBotProfileProtocol } from './Protocols/UsyncBotProfileProtocol'
import { USyncLIDProtocol } from './Protocols/UsyncLIDProtocol'
import {
	USyncContactProtocol,
	USyncDeviceProtocol,
	USyncDisappearingModeProtocol,
	USyncStatusProtocol
} from './Protocols'
import { USyncUser } from './USyncUser'

const NATIVE_USYNC_PROTOCOLS = new Set(['contact', 'lid', 'devices', 'status', 'disappearing_mode', 'bot'])
const nativeParseUSyncQueryResultFast = requireNativeExport('parseUSyncQueryResultFast')

export type USyncQueryResultList = { [protocol: string]: unknown; id: string }

export type USyncQueryError = {
	code?: number
	text?: string
	id?: string
	protocol?: string
	tag?: string
}

export type USyncQueryResult = {
	list: USyncQueryResultList[]
	sideList: USyncQueryResultList[]
	errors: USyncQueryError[]
	backoffMs?: number
	retryAfterMs?: number
}

export class USyncQuery {
	protocols: USyncQueryProtocol[]
	users: USyncUser[]
	context: string
	mode: string

	constructor() {
		this.protocols = []
		this.users = []
		this.context = 'interactive'
		this.mode = 'query'
	}

	withMode(mode: string) {
		this.mode = mode
		return this
	}

	withContext(context: string) {
		this.context = context
		return this
	}

	withUser(user: USyncUser) {
		this.users.push(user)
		return this
	}

	parseUSyncQueryResult(result: BinaryNode | undefined): USyncQueryResult | undefined {
		if (!result || result.attrs.type !== 'result') {
			return
		}

		const protocolNames = this.protocols.map(protocol => protocol.name)
		const canUseNative = protocolNames.length > 0 && protocolNames.every(name => NATIVE_USYNC_PROTOCOLS.has(name))
		if (canUseNative) {
			const nativeParsed = nativeParseUSyncQueryResultFast(result as unknown, protocolNames)
			if (!nativeParsed || !Array.isArray(nativeParsed.list) || !Array.isArray(nativeParsed.sideList)) {
				throw new Error('native parseUSyncQueryResultFast returned invalid payload')
			}

			return {
				list: nativeParsed.list as USyncQueryResultList[],
				sideList: nativeParsed.sideList as USyncQueryResultList[],
				errors: []
			}
		}

		const protocolMap = Object.fromEntries(
			this.protocols.map(protocol => {
				return [protocol.name, protocol.parser]
			})
		)

		const queryResult: USyncQueryResult = {
			list: [],
			sideList: [],
			errors: []
		}

		const usyncNode = getBinaryNodeChild(result, 'usync')
		if (!usyncNode) {
			return queryResult
		}

		const resultNode = getBinaryNodeChild(usyncNode, 'result')
		const usyncErrorNode = getBinaryNodeChild(usyncNode, 'error')
		const resultErrorNode = resultNode ? getBinaryNodeChild(resultNode, 'error') : undefined
		if (usyncErrorNode) {
			queryResult.errors.push({
				code: usyncErrorNode.attrs.code ? +usyncErrorNode.attrs.code : undefined,
				text: usyncErrorNode.attrs.text,
				tag: 'usync'
			})
		}

		if (resultErrorNode) {
			queryResult.errors.push({
				code: resultErrorNode.attrs.code ? +resultErrorNode.attrs.code : undefined,
				text: resultErrorNode.attrs.text,
				tag: 'result'
			})
		}

		const backoff = usyncNode.attrs.backoff || resultNode?.attrs.backoff
		const retryAfter = usyncNode.attrs.retry_after || resultNode?.attrs.retry_after || resultNode?.attrs.refresh
		if (backoff !== undefined) {
			const value = +backoff
			if (Number.isFinite(value)) {
				queryResult.backoffMs = value
			}
		}

		if (retryAfter !== undefined) {
			const value = +retryAfter
			if (Number.isFinite(value)) {
				queryResult.retryAfterMs = value
			}
		}

		const parseList = (listNode: BinaryNode | undefined): USyncQueryResultList[] => {
			if (!listNode?.content || !Array.isArray(listNode.content)) {
				return []
			}

			return listNode.content.reduce((acc: USyncQueryResultList[], node) => {
				const id = node?.attrs?.jid
				if (!id) {
					return acc
				}

				const userErrorNode = getBinaryNodeChild(node, 'error')
				if (userErrorNode) {
					queryResult.errors.push({
						code: userErrorNode.attrs.code ? +userErrorNode.attrs.code : undefined,
						text: userErrorNode.attrs.text,
						id,
						tag: node.tag
					})
				}

				const data = Array.isArray(node?.content)
					? Object.fromEntries(
							node.content
								.map(content => {
									const protocol = content.tag
									const parser = protocolMap[protocol]
									if (!parser) {
										return [protocol, null]
									}

									try {
										return [protocol, parser(content)]
									} catch (error: any) {
										queryResult.errors.push({
											id,
											protocol,
											text: String(error?.message || error),
											tag: content.tag
										})
										return [protocol, null]
									}
								})
								.filter(([, parsed]) => parsed !== null) as [string, unknown][]
						)
					: {}
				acc.push({ ...data, id })
				return acc
			}, [])
		}

		queryResult.list = parseList(getBinaryNodeChild(usyncNode, 'list'))
		queryResult.sideList = parseList(getBinaryNodeChild(usyncNode, 'side_list'))
		return queryResult
	}

	withDeviceProtocol() {
		this.protocols.push(new USyncDeviceProtocol())
		return this
	}

	withContactProtocol() {
		this.protocols.push(new USyncContactProtocol())
		return this
	}

	withStatusProtocol() {
		this.protocols.push(new USyncStatusProtocol())
		return this
	}

	withDisappearingModeProtocol() {
		this.protocols.push(new USyncDisappearingModeProtocol())
		return this
	}

	withBotProfileProtocol() {
		this.protocols.push(new USyncBotProfileProtocol())
		return this
	}

	withLIDProtocol() {
		this.protocols.push(new USyncLIDProtocol())
		return this
	}
}
