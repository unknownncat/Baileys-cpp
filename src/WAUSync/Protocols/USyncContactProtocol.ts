import type { USyncQueryProtocol } from '../../Types/USync'
import { assertNodeErrorFree, type BinaryNode, jidDecode } from '../../WABinary'
import { USyncUser } from '../USyncUser'

export class USyncContactProtocol implements USyncQueryProtocol {
	name = 'contact'

	getQueryElement(): BinaryNode {
		return {
			tag: 'contact',
			attrs: {}
		}
	}

	getUserElement(user: USyncUser): BinaryNode {
		const attrs: Record<string, string> = {}
		if (user.type) {
			attrs.type = user.type
		}

		const username =
			user.username ||
			(typeof user.id === 'string' ? jidDecode(user.id)?.user : undefined) ||
			user.phone?.replace(/[^\d+]/g, '')

		if (username) {
			attrs.username = username
		}

		return {
			tag: 'contact',
			attrs,
			content: user.phone
		}
	}

	parser(node: BinaryNode): boolean {
		if (node.tag === 'contact') {
			assertNodeErrorFree(node)
			return node?.attrs?.type === 'in'
		}

		return false
	}
}
