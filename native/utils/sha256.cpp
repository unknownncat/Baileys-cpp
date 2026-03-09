#include "utils/sha256.h"

#include <array>
#include <cstring>

namespace baileys_native::utils {

namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
	0x428a2f98u,
	0x71374491u,
	0xb5c0fbcfu,
	0xe9b5dba5u,
	0x3956c25bu,
	0x59f111f1u,
	0x923f82a4u,
	0xab1c5ed5u,
	0xd807aa98u,
	0x12835b01u,
	0x243185beu,
	0x550c7dc3u,
	0x72be5d74u,
	0x80deb1feu,
	0x9bdc06a7u,
	0xc19bf174u,
	0xe49b69c1u,
	0xefbe4786u,
	0x0fc19dc6u,
	0x240ca1ccu,
	0x2de92c6fu,
	0x4a7484aau,
	0x5cb0a9dcu,
	0x76f988dau,
	0x983e5152u,
	0xa831c66du,
	0xb00327c8u,
	0xbf597fc7u,
	0xc6e00bf3u,
	0xd5a79147u,
	0x06ca6351u,
	0x14292967u,
	0x27b70a85u,
	0x2e1b2138u,
	0x4d2c6dfcu,
	0x53380d13u,
	0x650a7354u,
	0x766a0abbu,
	0x81c2c92eu,
	0x92722c85u,
	0xa2bfe8a1u,
	0xa81a664bu,
	0xc24b8b70u,
	0xc76c51a3u,
	0xd192e819u,
	0xd6990624u,
	0xf40e3585u,
	0x106aa070u,
	0x19a4c116u,
	0x1e376c08u,
	0x2748774cu,
	0x34b0bcb5u,
	0x391c0cb3u,
	0x4ed8aa4au,
	0x5b9cca4fu,
	0x682e6ff3u,
	0x748f82eeu,
	0x78a5636fu,
	0x84c87814u,
	0x8cc70208u,
	0x90befffau,
	0xa4506cebu,
	0xbef9a3f7u,
	0xc67178f2u
};

constexpr std::array<uint32_t, 8> kInitialState = {
	0x6a09e667u,
	0xbb67ae85u,
	0x3c6ef372u,
	0xa54ff53au,
	0x510e527fu,
	0x9b05688cu,
	0x1f83d9abu,
	0x5be0cd19u
};

inline uint32_t RotRight(uint32_t value, uint32_t bits) {
	return (value >> bits) | (value << (32u - bits));
}

inline uint32_t BigSigma0(uint32_t x) {
	return RotRight(x, 2u) ^ RotRight(x, 13u) ^ RotRight(x, 22u);
}

inline uint32_t BigSigma1(uint32_t x) {
	return RotRight(x, 6u) ^ RotRight(x, 11u) ^ RotRight(x, 25u);
}

inline uint32_t SmallSigma0(uint32_t x) {
	return RotRight(x, 7u) ^ RotRight(x, 18u) ^ (x >> 3u);
}

inline uint32_t SmallSigma1(uint32_t x) {
	return RotRight(x, 17u) ^ RotRight(x, 19u) ^ (x >> 10u);
}

inline uint32_t Choose(uint32_t x, uint32_t y, uint32_t z) {
	return (x & y) ^ (~x & z);
}

inline uint32_t Majority(uint32_t x, uint32_t y, uint32_t z) {
	return (x & y) ^ (x & z) ^ (y & z);
}

class Sha256State {
public:
	Sha256State() {
		state_ = kInitialState;
	}

	void Update(const uint8_t* data, size_t length) {
		if (length == 0) {
			return;
		}

		for (size_t i = 0; i < length; ++i) {
			buffer_[bufferLength_++] = data[i];
			if (bufferLength_ == buffer_.size()) {
				Transform(buffer_.data());
				totalBits_ += static_cast<uint64_t>(buffer_.size()) * 8u;
				bufferLength_ = 0;
			}
		}
	}

	void Finalize(std::array<uint8_t, 32>* out) {
		totalBits_ += static_cast<uint64_t>(bufferLength_) * 8u;

		buffer_[bufferLength_++] = 0x80u;
		if (bufferLength_ > 56u) {
			while (bufferLength_ < 64u) {
				buffer_[bufferLength_++] = 0;
			}
			Transform(buffer_.data());
			bufferLength_ = 0;
		}

		while (bufferLength_ < 56u) {
			buffer_[bufferLength_++] = 0;
		}

		for (int i = 7; i >= 0; --i) {
			buffer_[bufferLength_++] = static_cast<uint8_t>((totalBits_ >> (i * 8)) & 0xffu);
		}
		Transform(buffer_.data());

		for (size_t i = 0; i < state_.size(); ++i) {
			(*out)[i * 4] = static_cast<uint8_t>((state_[i] >> 24u) & 0xffu);
			(*out)[i * 4 + 1] = static_cast<uint8_t>((state_[i] >> 16u) & 0xffu);
			(*out)[i * 4 + 2] = static_cast<uint8_t>((state_[i] >> 8u) & 0xffu);
			(*out)[i * 4 + 3] = static_cast<uint8_t>(state_[i] & 0xffu);
		}
	}

private:
	void Transform(const uint8_t* block) {
		std::array<uint32_t, 64> w{};
		for (size_t i = 0; i < 16; ++i) {
			const size_t j = i * 4;
			w[i] = (static_cast<uint32_t>(block[j]) << 24u) | (static_cast<uint32_t>(block[j + 1]) << 16u) |
				(static_cast<uint32_t>(block[j + 2]) << 8u) | static_cast<uint32_t>(block[j + 3]);
		}
		for (size_t i = 16; i < w.size(); ++i) {
			w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];
		}

		uint32_t a = state_[0];
		uint32_t b = state_[1];
		uint32_t c = state_[2];
		uint32_t d = state_[3];
		uint32_t e = state_[4];
		uint32_t f = state_[5];
		uint32_t g = state_[6];
		uint32_t h = state_[7];

		for (size_t i = 0; i < 64; ++i) {
			const uint32_t t1 = h + BigSigma1(e) + Choose(e, f, g) + kRoundConstants[i] + w[i];
			const uint32_t t2 = BigSigma0(a) + Majority(a, b, c);
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}

		state_[0] += a;
		state_[1] += b;
		state_[2] += c;
		state_[3] += d;
		state_[4] += e;
		state_[5] += f;
		state_[6] += g;
		state_[7] += h;
	}

	std::array<uint32_t, 8> state_{};
	std::array<uint8_t, 64> buffer_{};
	size_t bufferLength_ = 0;
	uint64_t totalBits_ = 0;
};

} // namespace

bool Sha256(const uint8_t* data, size_t length, std::array<uint8_t, 32>* out) {
	if (out == nullptr) {
		return false;
	}
	if (length > 0 && data == nullptr) {
		return false;
	}

	Sha256State state;
	state.Update(data, length);
	state.Finalize(out);
	return true;
}

} // namespace baileys_native::utils
