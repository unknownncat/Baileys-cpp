#include "buffer_builder.h"

#include "common.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace baileys_native {

namespace {

class NativeBufferBuilder : public Napi::ObjectWrap<NativeBufferBuilder> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function func = DefineClass(
			env,
			"NativeBufferBuilder",
			{
				InstanceMethod("append", &NativeBufferBuilder::Append),
				InstanceMethod("toBuffer", &NativeBufferBuilder::ToBuffer),
				InstanceMethod("size", &NativeBufferBuilder::Size),
				InstanceMethod("clear", &NativeBufferBuilder::Clear)
			}
		);

		constructor_ = Napi::Persistent(func);
		constructor_.SuppressDestruct();
		exports.Set("NativeBufferBuilder", func);
		return func;
	}

	NativeBufferBuilder(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeBufferBuilder>(info) {}

private:
	static Napi::FunctionReference constructor_;

	std::vector<uint8_t> data_;

	Napi::Value Append(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "append(chunk) requires chunk").ThrowAsJavaScriptException();
			return env.Null();
		}

		common::ByteView chunk;
		if (!common::GetByteViewFromValue(env, info[0], "chunk", &chunk)) {
			return env.Null();
		}

		if (chunk.length > 0) {
			const size_t previous = data_.size();
			data_.resize(previous + chunk.length);
			std::memcpy(data_.data() + previous, chunk.data, chunk.length);
		}

		return env.Undefined();
	}

	Napi::Value ToBuffer(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		Napi::Value resetValue = info.Length() > 0 ? info[0] : env.Undefined();
		const bool reset = resetValue.IsBoolean() ? resetValue.As<Napi::Boolean>().Value() : true;

		const uint8_t* ptr = data_.empty() ? reinterpret_cast<const uint8_t*>("") : data_.data();
		Napi::Buffer<uint8_t> out = Napi::Buffer<uint8_t>::Copy(env, ptr, data_.size());
		if (reset) {
			data_.clear();
		}

		return out;
	}

	Napi::Value Size(const Napi::CallbackInfo& info) {
		return Napi::Number::New(info.Env(), static_cast<double>(data_.size()));
	}

	Napi::Value Clear(const Napi::CallbackInfo& info) {
		data_.clear();
		return info.Env().Undefined();
	}
};

Napi::FunctionReference NativeBufferBuilder::constructor_;

} // namespace

Napi::Function InitNativeBufferBuilder(Napi::Env env, Napi::Object exports) {
	return NativeBufferBuilder::Init(env, exports);
}

} // namespace baileys_native
