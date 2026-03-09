#include "audio_wav_analyzer.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace baileys_native {

namespace {

constexpr uint32_t kDefaultSamples = 64;
constexpr uint32_t kMinSamples = 8;
constexpr uint32_t kMaxSamples = 512;

struct WavDataView {
	uint16_t audioFormat = 0;
	uint16_t channelCount = 0;
	uint32_t sampleRate = 0;
	uint16_t blockAlign = 0;
	uint16_t bitsPerSample = 0;
	const uint8_t* payload = nullptr;
	size_t payloadLength = 0;
};

inline uint16_t ReadU16LE(const uint8_t* ptr) {
	return static_cast<uint16_t>(ptr[0] | (static_cast<uint16_t>(ptr[1]) << 8u));
}

inline uint32_t ReadU32LE(const uint8_t* ptr) {
	return static_cast<uint32_t>(ptr[0]) | (static_cast<uint32_t>(ptr[1]) << 8u) |
		(static_cast<uint32_t>(ptr[2]) << 16u) | (static_cast<uint32_t>(ptr[3]) << 24u);
}

bool ParseWav(const uint8_t* data, size_t length, WavDataView* out) {
	if (!data || length < 44) {
		return false;
	}
	if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
		return false;
	}

	bool hasFmt = false;
	bool hasData = false;
	size_t offset = 12;
	while (offset + 8 <= length) {
		const uint8_t* chunk = data + offset;
		const uint32_t chunkSize = ReadU32LE(chunk + 4);
		const size_t chunkDataOffset = offset + 8;
		if (chunkDataOffset > length) {
			break;
		}

		const size_t available = length - chunkDataOffset;
		const size_t safeChunkSize = std::min(static_cast<size_t>(chunkSize), available);
		const uint8_t* chunkData = data + chunkDataOffset;

		if (std::memcmp(chunk, "fmt ", 4) == 0 && safeChunkSize >= 16) {
			out->audioFormat = ReadU16LE(chunkData);
			out->channelCount = ReadU16LE(chunkData + 2);
			out->sampleRate = ReadU32LE(chunkData + 4);
			out->blockAlign = ReadU16LE(chunkData + 12);
			out->bitsPerSample = ReadU16LE(chunkData + 14);
			hasFmt = true;
		} else if (std::memcmp(chunk, "data", 4) == 0 && safeChunkSize > 0) {
			out->payload = chunkData;
			out->payloadLength = safeChunkSize;
			hasData = true;
		}

		size_t nextOffset = chunkDataOffset + static_cast<size_t>(chunkSize);
		if ((chunkSize & 1u) != 0u) {
			nextOffset += 1;
		}
		if (nextOffset <= offset) {
			break;
		}
		offset = nextOffset;
	}

	if (!hasFmt || !hasData) {
		return false;
	}
	if (out->sampleRate == 0 || out->channelCount == 0 || out->blockAlign == 0 || out->bitsPerSample == 0) {
		return false;
	}
	if (out->payloadLength < out->blockAlign) {
		return false;
	}
	return true;
}

bool DecodeSampleNormalized(const WavDataView& wav, const uint8_t* frame, double* out) {
	if (wav.audioFormat == 1) {
		switch (wav.bitsPerSample) {
			case 8: {
				const int32_t sample = static_cast<int32_t>(frame[0]) - 128;
				*out = static_cast<double>(sample) / 128.0;
				return true;
			}
			case 16: {
				const int16_t sample = static_cast<int16_t>(ReadU16LE(frame));
				*out = static_cast<double>(sample) / 32768.0;
				return true;
			}
			case 24: {
				int32_t sample = (static_cast<int32_t>(frame[0])) | (static_cast<int32_t>(frame[1]) << 8u) |
					(static_cast<int32_t>(frame[2]) << 16u);
				if ((sample & 0x00800000) != 0) {
					sample |= 0xFF000000;
				}
				*out = static_cast<double>(sample) / 8388608.0;
				return true;
			}
			case 32: {
				const int32_t sample = static_cast<int32_t>(ReadU32LE(frame));
				*out = static_cast<double>(sample) / 2147483648.0;
				return true;
			}
			default:
				return false;
		}
	}

	if (wav.audioFormat == 3 && wav.bitsPerSample == 32) {
		float sample = 0.0f;
		std::memcpy(&sample, frame, sizeof(float));
		*out = std::clamp(static_cast<double>(sample), -1.0, 1.0);
		return true;
	}

	return false;
}

} // namespace

Napi::Value AnalyzeWavAudioFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1) {
		Napi::TypeError::New(env, "analyzeWavAudioFast(data, [sampleCount]) requires data").ThrowAsJavaScriptException();
		return env.Null();
	}

	common::ByteView input;
	if (!common::GetByteViewFromValue(env, info[0], "data", &input)) {
		return env.Null();
	}

	uint32_t sampleCount = kDefaultSamples;
	if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
		if (!common::ReadUInt32FromValue(env, info[1], "sampleCount", &sampleCount)) {
			return env.Null();
		}
		if (sampleCount < kMinSamples || sampleCount > kMaxSamples) {
			Napi::RangeError::New(env, "sampleCount must be between 8 and 512").ThrowAsJavaScriptException();
			return env.Null();
		}
	}

	WavDataView wav;
	if (!ParseWav(input.data, input.length, &wav)) {
		return env.Null();
	}

	const size_t frameCount = wav.payloadLength / wav.blockAlign;
	if (frameCount == 0 || frameCount > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
		return env.Null();
	}

	const double durationSec = static_cast<double>(frameCount) / static_cast<double>(wav.sampleRate);
	std::vector<double> envelope(sampleCount, 0.0);

	for (uint32_t i = 0; i < sampleCount; ++i) {
		const size_t frameStart = (static_cast<uint64_t>(i) * frameCount) / sampleCount;
		size_t frameEnd = (static_cast<uint64_t>(i + 1u) * frameCount) / sampleCount;
		if (frameEnd <= frameStart) {
			frameEnd = std::min(frameStart + 1, frameCount);
		}

		double acc = 0.0;
		size_t taken = 0;
		for (size_t frameIndex = frameStart; frameIndex < frameEnd; ++frameIndex) {
			const uint8_t* frame = wav.payload + (frameIndex * wav.blockAlign);
			double normalized = 0.0;
			if (!DecodeSampleNormalized(wav, frame, &normalized)) {
				return env.Null();
			}
			acc += std::fabs(normalized);
			taken += 1;
		}

		envelope[i] = taken > 0 ? (acc / static_cast<double>(taken)) : 0.0;
	}

	double peak = 0.0;
	for (double value : envelope) {
		if (value > peak) {
			peak = value;
		}
	}

	std::vector<uint8_t> waveform(sampleCount, 0);
	if (peak > 0.0) {
		const double invPeak = 1.0 / peak;
		for (uint32_t i = 0; i < sampleCount; ++i) {
			const double scaled = std::clamp(envelope[i] * invPeak, 0.0, 1.0);
			const double value = std::floor(100.0 * scaled);
			waveform[i] = static_cast<uint8_t>(value);
		}
	}

	Napi::Object out = Napi::Object::New(env);
	out.Set("durationSec", Napi::Number::New(env, durationSec));
	out.Set("waveform", Napi::Buffer<uint8_t>::Copy(env, waveform.data(), waveform.size()));
	return out;
}

} // namespace baileys_native

