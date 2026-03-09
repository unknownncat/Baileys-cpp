import { BAILEYS_NATIVE } from '../../Native/baileys-native'

const native = BAILEYS_NATIVE

describe('native socket codec parity', () => {
	it('roundtrips group participant stubs with stable shape', () => {
		const participants = [
			{ id: 'abc', phoneNumber: '5511999999999', admin: null },
			{ id: 'def', lid: '5511888888888@lid', admin: 'superadmin' }
		]

		const encoded = native.encodeGroupParticipantStubsFast!(participants)
		expect(encoded).not.toBeNull()
		const encodedList = encoded as string[]
		expect(encodedList).toHaveLength(2)

		const decodedRaw = native.parseGroupParticipantStubsFast!(encodedList)
		expect(decodedRaw).not.toBeNull()
		const decoded = decodedRaw as Array<Record<string, unknown>>
		expect(decoded).toEqual([
			{ id: 'abc', phoneNumber: '5511999999999', admin: null },
			{ id: 'def', lid: '5511888888888@lid', admin: 'superadmin' }
		])
	})

	it('keeps parse fallback behavior for malformed flat json items', () => {
		expect(native.parseGroupParticipantStubsFast!(['{"id":1}'])).toBeNull()
	})

	it('keeps participant hash deterministic regardless of input order', () => {
		const first = native.generateParticipantHashV2Fast!(['b@s.whatsapp.net', 'a@s.whatsapp.net'])
		const second = native.generateParticipantHashV2Fast!(['a@s.whatsapp.net', 'b@s.whatsapp.net'])

		expect(first).toBe(second)
		expect(first).toMatch(/^2:[A-Za-z0-9+/]{6}$/)
	})

	it('keeps participant hash guard clauses/messages', () => {
		expect(() => (native.generateParticipantHashV2Fast as unknown as () => unknown)()).toThrow(
			'generateParticipantHashV2Fast(participants) requires participants'
		)
		expect(() => (native.generateParticipantHashV2Fast as unknown as (v: unknown) => unknown)(1)).toThrow(
			'participants must be an array'
		)
	})

	it('accepts inbound direct LID envelopes that still include recipient attrs', () => {
		const decoded = native.decodeMessageNodeFast!(
			{
				from: '133526875812046@lid',
				type: 'text',
				id: 'AC263CF9BAC16FCBD38783BB1C76588C',
				recipient: '133526875812046@lid',
				notify: 'sem nome 零',
				peer_recipient_pn: '559691862528@s.whatsapp.net',
				t: '1773085220'
			},
			'5511999999999@s.whatsapp.net',
			'123456789012345@lid'
		)

		expect(decoded).toEqual({
			key: {
				remoteJid: '133526875812046@lid',
				remoteJidAlt: '559691862528@s.whatsapp.net',
				fromMe: false,
				id: 'AC263CF9BAC16FCBD38783BB1C76588C',
				addressingMode: 'lid'
			},
			author: '133526875812046@lid',
			sender: '133526875812046@lid',
			messageTimestamp: 1773085220,
			pushName: 'sem nome 零',
			broadcast: false
		})
	})
})
