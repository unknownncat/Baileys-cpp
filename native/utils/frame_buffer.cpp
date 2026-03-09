#include "frame_buffer.h"

#include "common.h"

#include <cstdint>
#include <list>
#include <string>
#include <vector>

namespace baileys_native {

namespace {

class NativeFrameBuffer : public Napi::ObjectWrap<NativeFrameBuffer> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function func = DefineClass(
			env,
			"NativeFrameBuffer",
			{
				InstanceMethod("append", &NativeFrameBuffer::Append),
				InstanceMethod("popFrame", &NativeFrameBuffer::PopFrame),
				InstanceMethod("popFrames", &NativeFrameBuffer::PopFrames),
				InstanceMethod("size", &NativeFrameBuffer::Size),
				InstanceMethod("clear", &NativeFrameBuffer::Clear)
			}
		);

		constructor_ = Napi::Persistent(func);
		constructor_.SuppressDestruct();
		exports.Set("NativeFrameBuffer", func);
		return func;
	}

	NativeFrameBuffer(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeFrameBuffer>(info) {}

private:
	static Napi::FunctionReference constructor_;

	std::vector<uint8_t> buffer_;
	size_t readOffset_ = 0;

	size_t Available() const {
		if (readOffset_ > buffer_.size()) return 0;
		return buffer_.size() - readOffset_;
	}

	void CompactIfNeeded() {
		if (readOffset_ == 0) return;
		const size_t avail = Available();
		if (avail == 0) {
			buffer_.clear();
			readOffset_ = 0;
			return;
		}

		// Compact periodically to keep append/pop O(1) amortized and avoid extra allocations.
		if (readOffset_ > 65536 || readOffset_ * 2 > buffer_.size()) {
			std::vector<uint8_t> compacted;
			compacted.reserve(avail);
			compacted.insert(compacted.end(), buffer_.begin() + static_cast<std::ptrdiff_t>(readOffset_), buffer_.end());
			buffer_.swap(compacted);
			readOffset_ = 0;
		}
	}

	bool TryPopFrame(const uint8_t** framePtr, uint32_t* frameSize) {
		const size_t avail = Available();
		if (avail < 3) {
			return false;
		}

		const size_t base = readOffset_;
		const uint32_t size = (static_cast<uint32_t>(buffer_[base]) << 16u) |
			(static_cast<uint32_t>(buffer_[base + 1]) << 8u) |
			static_cast<uint32_t>(buffer_[base + 2]);

		if (avail < static_cast<size_t>(size) + 3u) {
			return false;
		}

		*framePtr = buffer_.data() + base + 3;
		*frameSize = size;
		readOffset_ += static_cast<size_t>(size) + 3u;
		return true;
	}

	Napi::Value Append(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "append(data) requires data").ThrowAsJavaScriptException();
			return env.Null();
		}

		std::vector<uint8_t> data;
		if (!common::CopyBytesFromValue(env, info[0], "data", &data)) {
			return env.Null();
		}

		if (!data.empty()) {
			buffer_.insert(buffer_.end(), data.begin(), data.end());
		}
		return env.Undefined();
	}

	Napi::Value PopFrame(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		const uint8_t* framePtr = nullptr;
		uint32_t frameSize = 0;
		if (!TryPopFrame(&framePtr, &frameSize)) {
			return env.Null();
		}

		Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::Copy(env, framePtr, frameSize);
		CompactIfNeeded();
		return out;
	}

	Napi::Value PopFrames(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		std::vector<std::pair<const uint8_t*, uint32_t>> slices;

		while (true) {
			const uint8_t* framePtr = nullptr;
			uint32_t frameSize = 0;
			if (!TryPopFrame(&framePtr, &frameSize)) {
				break;
			}
			slices.push_back({framePtr, frameSize});
		}

		Napi::Array out = Napi::Array::New(env, slices.size());
		for (size_t i = 0; i < slices.size(); ++i) {
			out.Set(static_cast<uint32_t>(i), Napi::Buffer<uint8_t>::Copy(env, slices[i].first, slices[i].second));
		}
		CompactIfNeeded();
		return out;
	}

	Napi::Value Size(const Napi::CallbackInfo& info) {
		return Napi::Number::New(info.Env(), static_cast<double>(Available()));
	}

	Napi::Value Clear(const Napi::CallbackInfo& info) {
		buffer_.clear();
		readOffset_ = 0;
		return info.Env().Undefined();
	}
};

Napi::FunctionReference NativeFrameBuffer::constructor_;

} // namespace

Napi::Function InitNativeFrameBuffer(Napi::Env env, Napi::Object exports) {
	return NativeFrameBuffer::Init(env, exports);
}

} // namespace baileys_native
