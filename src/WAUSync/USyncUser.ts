export class USyncUser {
	id?: string
	lid?: string
	phone?: string
	type?: string
	username?: string
	personaId?: string
	devicePhash?: string
	deviceTimestamp?: number
	deviceExpectedTimestamp?: number

	withId(id: string) {
		this.id = id
		return this
	}

	withLid(lid: string) {
		this.lid = lid
		return this
	}

	withPhone(phone: string) {
		this.phone = phone
		return this
	}

	withType(type: string) {
		this.type = type
		return this
	}

	withUsername(username: string) {
		this.username = username
		return this
	}

	withPersonaId(personaId: string) {
		this.personaId = personaId
		return this
	}

	withDevicePhash(devicePhash: string) {
		this.devicePhash = devicePhash
		return this
	}

	withDeviceTimestamp(deviceTimestamp: number) {
		this.deviceTimestamp = deviceTimestamp
		return this
	}

	withDeviceExpectedTimestamp(deviceExpectedTimestamp: number) {
		this.deviceExpectedTimestamp = deviceExpectedTimestamp
		return this
	}
}
