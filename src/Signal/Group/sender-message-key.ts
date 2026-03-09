import { deriveSignalSecrets } from '../../Utils/crypto'

export class SenderMessageKey {
	private readonly iteration: number
	private readonly iv: Uint8Array
	private readonly cipherKey: Uint8Array
	private readonly seed: Uint8Array

	constructor(iteration: number, seed: Uint8Array) {
		const derivatives = deriveSignalSecrets(seed, Buffer.alloc(32), Buffer.from('WhisperGroup'), 2)
		const firstDerivative = derivatives[0]
		const secondDerivative = derivatives[1]
		if (!firstDerivative || !secondDerivative) {
			throw new Error('Failed to derive sender message key')
		}

		const keys = new Uint8Array(32)
		keys.set(firstDerivative.slice(16))
		keys.set(secondDerivative.slice(0, 16), 16)

		this.iv = Buffer.from(firstDerivative.slice(0, 16))
		this.cipherKey = Buffer.from(keys)
		this.iteration = iteration
		this.seed = seed
	}

	public getIteration(): number {
		return this.iteration
	}

	public getIv(): Uint8Array {
		return this.iv
	}

	public getCipherKey(): Uint8Array {
		return this.cipherKey
	}

	public getSeed(): Uint8Array {
		return this.seed
	}
}
