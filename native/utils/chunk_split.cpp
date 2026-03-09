#include "chunk_split.h"

#include "common.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace baileys_native {

Napi::Value SplitAlignedChunk(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 2) {
		Napi::TypeError::New(env, "splitAlignedChunk(remaining, chunk, [blockSize]) requires remaining and chunk")
			.ThrowAsJavaScriptException();
		return env.Null();
	}

	std::vector<uint8_t> remaining;
	std::vector<uint8_t> chunk;
	if (!common::CopyBytesFromValue(env, info[0], "remaining", &remaining)) return env.Null();
	if (!common::CopyBytesFromValue(env, info[1], "chunk", &chunk)) return env.Null();

	uint32_t blockSize = 16;
	if (info.Length() > 2 && !info[2].IsUndefined() && !info[2].IsNull()) {
		if (!common::ReadUInt32FromValue(env, info[2], "blockSize", &blockSize)) return env.Null();
	}
	if (blockSize == 0) {
		Napi::RangeError::New(env, "blockSize must be > 0").ThrowAsJavaScriptException();
		return env.Null();
	}

	const size_t total = remaining.size() + chunk.size();
	const size_t aligned = total - (total % static_cast<size_t>(blockSize));

	std::vector<uint8_t> decryptable;
	decryptable.reserve(aligned);

	if (aligned > 0) {
		if (remaining.size() >= aligned) {
			decryptable.insert(decryptable.end(), remaining.begin(), remaining.begin() + aligned);
		} else {
			decryptable.insert(decryptable.end(), remaining.begin(), remaining.end());
			const size_t needFromChunk = aligned - remaining.size();
			decryptable.insert(decryptable.end(), chunk.begin(), chunk.begin() + needFromChunk);
		}
	}

	std::vector<uint8_t> rest;
	rest.reserve(total - aligned);

	if (remaining.size() > aligned) {
		rest.insert(rest.end(), remaining.begin() + aligned, remaining.end());
	} else {
		const size_t consumedFromChunk = aligned > remaining.size() ? aligned - remaining.size() : 0;
		if (chunk.size() > consumedFromChunk) {
			rest.insert(rest.end(), chunk.begin() + consumedFromChunk, chunk.end());
		}
	}

	Napi::Object out = Napi::Object::New(env);
	out.Set("decryptable", common::MoveVectorToBuffer(env, std::move(decryptable)));
	out.Set("remaining", common::MoveVectorToBuffer(env, std::move(rest)));
	return out;
}

} // namespace baileys_native
