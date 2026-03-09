import { proto } from '../../WAProto/index.js'
import { requireNativeExport } from '../Native/baileys-native'
import {
	type GroupMetadata,
	type GroupParticipant,
	type ParticipantAction,
	type SocketConfig,
	type WAMessageKey,
	WAMessageStubType
} from '../Types'
import { generateMessageID, generateMessageIDV2, unixTimestampSeconds } from '../Utils'
import logger from '../Utils/logger'
import {
	type BinaryNode,
	getBinaryNodeChild,
	getBinaryNodeChildren,
	getBinaryNodeChildString,
	isHostedLidUser,
	isHostedPnUser,
	isLidUser,
	isPnUser,
	jidEncode,
	jidNormalizedUser
} from '../WABinary'
import { makeBusinessSocket } from './business'

const isLidLike = (jid: string | null | undefined): boolean =>
	!!(isLidUser(jid || undefined) || isHostedLidUser(jid || undefined))
const isPnLike = (jid: string | null | undefined): boolean =>
	!!(isPnUser(jid || undefined) || isHostedPnUser(jid || undefined))
const nativeBuildParticipantNodesFast = requireNativeExport('buildParticipantNodesFast')
const nativeExtractNodeAttrsFast = requireNativeExport('extractNodeAttrsFast')
const nativeMapParticipantActionResultsFast = requireNativeExport('mapParticipantActionResultsFast')
const nativeExtractCommunityLinkedGroupsFast = requireNativeExport('extractCommunityLinkedGroupsFast')

export const makeCommunitiesSocket = (config: SocketConfig) => {
	const sock = makeBusinessSocket(config)
	const { authState, ev, query, upsertMessage } = sock

	const communityQuery = async (jid: string, type: 'get' | 'set', content: BinaryNode[]) =>
		query({
			tag: 'iq',
			attrs: {
				type,
				xmlns: 'w:g2',
				to: jid
			},
			content
		})

	const communityMetadata = async (jid: string) => {
		const result = await communityQuery(jid, 'get', [{ tag: 'query', attrs: { request: 'interactive' } }])
		return extractCommunityMetadata(result)
	}

	const communityFetchAllParticipating = async () => {
		const result = await query({
			tag: 'iq',
			attrs: {
				to: '@g.us',
				xmlns: 'w:g2',
				type: 'get'
			},
			content: [
				{
					tag: 'participating',
					attrs: {},
					content: [
						{ tag: 'participants', attrs: {} },
						{ tag: 'description', attrs: {} }
					]
				}
			]
		})
		const data: { [_: string]: GroupMetadata } = {}
		const communitiesChild = getBinaryNodeChild(result, 'communities')
		if (communitiesChild) {
			const communities = getBinaryNodeChildren(communitiesChild, 'community')
			for (const communityNode of communities) {
				const meta = extractCommunityMetadata({
					tag: 'result',
					attrs: {},
					content: [communityNode]
				})
				data[meta.id] = meta
			}
		}

		sock.ev.emit('groups.update', Object.values(data))

		return data
	}

	async function parseGroupResult(node: BinaryNode) {
		logger.info({ node }, 'parseGroupResult')
		const groupNode = getBinaryNodeChild(node, 'group')
		if (groupNode) {
			try {
				logger.info({ groupNode }, 'groupNode')
				const metadata = await sock.groupMetadata(`${groupNode.attrs.id}@g.us`)
				return metadata ? metadata : Optional.empty()
			} catch (error) {
				console.error('Error parsing group metadata:', error)
				return Optional.empty()
			}
		}

		return Optional.empty()
	}

	const Optional = {
		empty: () => null,
		of: (value: null) => (value !== null ? { value } : null)
	}

	sock.ws.on('CB:ib,,dirty', async (node: BinaryNode) => {
		const { attrs } = getBinaryNodeChild(node, 'dirty')!
		if (attrs.type !== 'communities') {
			return
		}

		await communityFetchAllParticipating()
		await sock.cleanDirtyBits('groups')
	})

	return {
		...sock,
		communityMetadata,
		communityCreate: async (subject: string, body: string) => {
			const descriptionId = generateMessageID().substring(0, 12)

			const result = await communityQuery('@g.us', 'set', [
				{
					tag: 'create',
					attrs: { subject },
					content: [
						{
							tag: 'description',
							attrs: { id: descriptionId },
							content: [
								{
									tag: 'body',
									attrs: {},
									content: Buffer.from(body || '', 'utf-8')
								}
							]
						},
						{
							tag: 'parent',
							attrs: { default_membership_approval_mode: 'request_required' }
						},
						{
							tag: 'allow_non_admin_sub_group_creation',
							attrs: {}
						},
						{
							tag: 'create_general_chat',
							attrs: {}
						}
					]
				}
			])

			return await parseGroupResult(result)
		},
		communityCreateGroup: async (subject: string, participants: string[], parentCommunityJid: string) => {
			const key = generateMessageIDV2()
			const participantNodes = nativeBuildParticipantNodesFast(participants)
			if (!Array.isArray(participantNodes)) {
				throw new Error('native buildParticipantNodesFast returned invalid payload')
			}

			const result = await communityQuery('@g.us', 'set', [
				{
					tag: 'create',
					attrs: {
						subject,
						key
					},
					content: [...(participantNodes as BinaryNode[]), { tag: 'linked_parent', attrs: { jid: parentCommunityJid } }]
				}
			])
			return await parseGroupResult(result)
		},
		communityLeave: async (id: string) => {
			await communityQuery('@g.us', 'set', [
				{
					tag: 'leave',
					attrs: {},
					content: [{ tag: 'community', attrs: { id } }]
				}
			])
		},
		communityUpdateSubject: async (jid: string, subject: string) => {
			await communityQuery(jid, 'set', [
				{
					tag: 'subject',
					attrs: {},
					content: Buffer.from(subject, 'utf-8')
				}
			])
		},
		communityLinkGroup: async (groupJid: string, parentCommunityJid: string) => {
			await communityQuery(parentCommunityJid, 'set', [
				{
					tag: 'links',
					attrs: {},
					content: [
						{
							tag: 'link',
							attrs: { link_type: 'sub_group' },
							content: [{ tag: 'group', attrs: { jid: groupJid } }]
						}
					]
				}
			])
		},
		communityUnlinkGroup: async (groupJid: string, parentCommunityJid: string) => {
			await communityQuery(parentCommunityJid, 'set', [
				{
					tag: 'unlink',
					attrs: { unlink_type: 'sub_group' },
					content: [{ tag: 'group', attrs: { jid: groupJid } }]
				}
			])
		},
		communityFetchLinkedGroups: async (jid: string) => {
			let communityJid = jid
			let isCommunity = false

			// Try to determine if it is a subgroup or a community
			const metadata = await sock.groupMetadata(jid)
			if (metadata.linkedParent) {
				// It is a subgroup, get the community jid
				communityJid = metadata.linkedParent
			} else {
				// It is a community
				isCommunity = true
			}

			// Fetch all subgroups of the community
			const result = await communityQuery(communityJid, 'get', [{ tag: 'sub_groups', attrs: {} }])

			let linkedGroupsData: unknown[] = []
			const subGroupsNode = getBinaryNodeChild(result, 'sub_groups')
			if (subGroupsNode) {
				const groupNodes = getBinaryNodeChildren(subGroupsNode, 'group')
				const extracted = nativeExtractCommunityLinkedGroupsFast(groupNodes as unknown[])
				if (!Array.isArray(extracted)) {
					throw new Error('native extractCommunityLinkedGroupsFast returned invalid payload')
				}

				linkedGroupsData = extracted
			}

			return {
				communityJid,
				isCommunity,
				linkedGroups: linkedGroupsData as Array<{
					id?: string
					subject: string
					creation?: number
					owner?: string
					size?: number
				}>
			}
		},
		communityRequestParticipantsList: async (jid: string) => {
			const result = await communityQuery(jid, 'get', [
				{
					tag: 'membership_approval_requests',
					attrs: {}
				}
			])
			const node = getBinaryNodeChild(result, 'membership_approval_requests')
			const participants = getBinaryNodeChildren(node, 'membership_approval_request')
			const attrs = nativeExtractNodeAttrsFast(participants as unknown[])
			if (!Array.isArray(attrs)) {
				throw new Error('native extractNodeAttrsFast returned invalid payload')
			}

			return attrs
		},
		communityRequestParticipantsUpdate: async (jid: string, participants: string[], action: 'approve' | 'reject') => {
			const participantNodes = nativeBuildParticipantNodesFast(participants)
			if (!Array.isArray(participantNodes)) {
				throw new Error('native buildParticipantNodesFast returned invalid payload')
			}

			const result = await communityQuery(jid, 'set', [
				{
					tag: 'membership_requests_action',
					attrs: {},
					content: [
						{
							tag: action,
							attrs: {},
							content: participantNodes as BinaryNode[]
						}
					]
				}
			])
			const node = getBinaryNodeChild(result, 'membership_requests_action')
			const nodeAction = getBinaryNodeChild(node, action)
			const participantsAffected = getBinaryNodeChildren(nodeAction, 'participant')
			const mapped = nativeMapParticipantActionResultsFast(participantsAffected as unknown[], false)
			if (!Array.isArray(mapped)) {
				throw new Error('native mapParticipantActionResultsFast returned invalid payload')
			}

			return mapped as Array<{ status: string; jid: string }>
		},
		communityParticipantsUpdate: async (jid: string, participants: string[], action: ParticipantAction) => {
			const participantNodes = nativeBuildParticipantNodesFast(participants)
			if (!Array.isArray(participantNodes)) {
				throw new Error('native buildParticipantNodesFast returned invalid payload')
			}

			const result = await communityQuery(jid, 'set', [
				{
					tag: action,
					attrs: action === 'remove' ? { linked_groups: 'true' } : {},
					content: participantNodes as BinaryNode[]
				}
			])
			const node = getBinaryNodeChild(result, action)
			const participantsAffected = getBinaryNodeChildren(node, 'participant')
			const mapped = nativeMapParticipantActionResultsFast(participantsAffected as unknown[], true)
			if (!Array.isArray(mapped)) {
				throw new Error('native mapParticipantActionResultsFast returned invalid payload')
			}

			return mapped as Array<{ status: string; jid: string; content: BinaryNode }>
		},
		communityUpdateDescription: async (jid: string, description?: string) => {
			const metadata = await communityMetadata(jid)
			const prev = metadata.descId ?? null

			await communityQuery(jid, 'set', [
				{
					tag: 'description',
					attrs: {
						...(description ? { id: generateMessageID() } : { delete: 'true' }),
						...(prev ? { prev } : {})
					},
					content: description ? [{ tag: 'body', attrs: {}, content: Buffer.from(description, 'utf-8') }] : undefined
				}
			])
		},
		communityInviteCode: async (jid: string) => {
			const result = await communityQuery(jid, 'get', [{ tag: 'invite', attrs: {} }])
			const inviteNode = getBinaryNodeChild(result, 'invite')
			return inviteNode?.attrs.code
		},
		communityRevokeInvite: async (jid: string) => {
			const result = await communityQuery(jid, 'set', [{ tag: 'invite', attrs: {} }])
			const inviteNode = getBinaryNodeChild(result, 'invite')
			return inviteNode?.attrs.code
		},
		communityAcceptInvite: async (code: string) => {
			const results = await communityQuery('@g.us', 'set', [{ tag: 'invite', attrs: { code } }])
			const result = getBinaryNodeChild(results, 'community')
			return result?.attrs.jid
		},

		/**
		 * revoke a v4 invite for someone
		 * @param communityJid community jid
		 * @param invitedJid jid of person you invited
		 * @returns true if successful
		 */
		communityRevokeInviteV4: async (communityJid: string, invitedJid: string) => {
			const result = await communityQuery(communityJid, 'set', [
				{ tag: 'revoke', attrs: {}, content: [{ tag: 'participant', attrs: { jid: invitedJid } }] }
			])
			return !!result
		},

		/**
		 * accept a CommunityInviteMessage
		 * @param key the key of the invite message, or optionally only provide the jid of the person who sent the invite
		 * @param inviteMessage the message to accept
		 */
		communityAcceptInviteV4: ev.createBufferedFunction(
			async (key: string | WAMessageKey, inviteMessage: proto.Message.IGroupInviteMessage) => {
				key = typeof key === 'string' ? { remoteJid: key } : key
				const results = await communityQuery(inviteMessage.groupJid!, 'set', [
					{
						tag: 'accept',
						attrs: {
							code: inviteMessage.inviteCode!,
							expiration: inviteMessage.inviteExpiration!.toString(),
							admin: key.remoteJid!
						}
					}
				])

				// if we have the full message key
				// update the invite message to be expired
				if (key.id) {
					// create new invite message that is expired
					inviteMessage = proto.Message.GroupInviteMessage.fromObject(inviteMessage)
					inviteMessage.inviteExpiration = 0
					inviteMessage.inviteCode = ''
					ev.emit('messages.update', [
						{
							key,
							update: {
								message: {
									groupInviteMessage: inviteMessage
								}
							}
						}
					])
				}

				// generate the community add message
				await upsertMessage(
					{
						key: {
							remoteJid: inviteMessage.groupJid,
							id: generateMessageIDV2(sock.user?.id),
							fromMe: false,
							participant: key.participant || key.remoteJid
						},
						messageStubType: WAMessageStubType.GROUP_PARTICIPANT_ADD,
						messageStubParameters: [JSON.stringify(authState.creds.me)],
						participant: key.remoteJid,
						messageTimestamp: unixTimestampSeconds()
					},
					'notify'
				)

				return results.attrs.from
			}
		),
		communityGetInviteInfo: async (code: string) => {
			const results = await communityQuery('@g.us', 'get', [{ tag: 'invite', attrs: { code } }])
			return extractCommunityMetadata(results)
		},
		communityToggleEphemeral: async (jid: string, ephemeralExpiration: number) => {
			const content: BinaryNode = ephemeralExpiration
				? { tag: 'ephemeral', attrs: { expiration: ephemeralExpiration.toString() } }
				: { tag: 'not_ephemeral', attrs: {} }
			await communityQuery(jid, 'set', [content])
		},
		communitySettingUpdate: async (
			jid: string,
			setting: 'announcement' | 'not_announcement' | 'locked' | 'unlocked'
		) => {
			await communityQuery(jid, 'set', [{ tag: setting, attrs: {} }])
		},
		communityMemberAddMode: async (jid: string, mode: 'admin_add' | 'all_member_add') => {
			await communityQuery(jid, 'set', [{ tag: 'member_add_mode', attrs: {}, content: mode }])
		},
		communityJoinApprovalMode: async (jid: string, mode: 'on' | 'off') => {
			await communityQuery(jid, 'set', [
				{ tag: 'membership_approval_mode', attrs: {}, content: [{ tag: 'community_join', attrs: { state: mode } }] }
			])
		},
		communityFetchAllParticipating
	}
}

export const extractCommunityMetadata = (result: BinaryNode) => {
	const community = getBinaryNodeChild(result, 'community')!
	const descChild = getBinaryNodeChild(community, 'description')
	let desc: string | undefined
	let descId: string | undefined
	if (descChild) {
		desc = getBinaryNodeChildString(descChild, 'body')
		descId = descChild.attrs.id
	}

	const communityId = community.attrs.id?.includes('@')
		? community.attrs.id
		: jidEncode(community.attrs.id || '', 'g.us')
	const eph = getBinaryNodeChild(community, 'ephemeral')?.attrs.expiration
	const memberAddMode = getBinaryNodeChildString(community, 'member_add_mode') === 'all_member_add'
	const metadata: GroupMetadata = {
		id: communityId,
		subject: community.attrs.subject || '',
		subjectOwner: community.attrs.s_o,
		subjectTime: Number(community.attrs.s_t || 0),
		size: getBinaryNodeChildren(community, 'participant').length,
		creation: Number(community.attrs.creation || 0),
		owner: community.attrs.creator ? jidNormalizedUser(community.attrs.creator) : undefined,
		desc,
		descId,
		linkedParent: getBinaryNodeChild(community, 'linked_parent')?.attrs.jid || undefined,
		restrict: !!getBinaryNodeChild(community, 'locked'),
		announce: !!getBinaryNodeChild(community, 'announcement'),
		isCommunity: !!getBinaryNodeChild(community, 'parent'),
		isCommunityAnnounce: !!getBinaryNodeChild(community, 'default_sub_community'),
		joinApprovalMode: !!getBinaryNodeChild(community, 'membership_approval_mode'),
		memberAddMode,
		participants: getBinaryNodeChildren(community, 'participant').reduce<GroupParticipant[]>((acc, { attrs }) => {
			const id = attrs.jid || attrs.phone_number || attrs.lid
			if (!id) {
				return acc
			}

			const phoneNumber =
				(isLidLike(attrs.jid) && isPnLike(attrs.phone_number) && attrs.phone_number) ||
				(isPnLike(attrs.jid) && attrs.jid) ||
				undefined
			const lid =
				(isPnLike(attrs.jid) && isLidLike(attrs.lid) && attrs.lid) || (isLidLike(attrs.jid) && attrs.jid) || undefined

			acc.push({
				id,
				phoneNumber,
				lid,
				admin: (attrs.type || null) as GroupParticipant['admin']
			})
			return acc
		}, []),
		ephemeralDuration: eph ? +eph : undefined,
		addressingMode: getBinaryNodeChildString(community, 'addressing_mode')! as GroupMetadata['addressingMode']
	}
	return metadata
}
