#include "message_waiter_registry.h"

#include "common.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace baileys_native {

namespace {

constexpr double kJsMaxSafeInteger = 9007199254740991.0;

bool ReadDeadlineMs(
	const Napi::Env& env,
	const Napi::Value& value,
	const char* field,
	uint64_t* out
) {
	double parsed = 0.0;
	if (!common::ReadDoubleFromValue(env, value, field, &parsed)) {
		return false;
	}

	if (parsed < 0.0 || parsed > kJsMaxSafeInteger || std::floor(parsed) != parsed) {
		Napi::RangeError::New(env, std::string("Invalid timestamp for field: ") + field).ThrowAsJavaScriptException();
		return false;
	}

	*out = static_cast<uint64_t>(parsed);
	return true;
}

class NativeMessageWaiterRegistry : public Napi::ObjectWrap<NativeMessageWaiterRegistry> {
public:
	static Napi::Function Init(Napi::Env env, Napi::Object exports) {
		Napi::Function func = DefineClass(
			env,
			"NativeMessageWaiterRegistry",
			{
				InstanceMethod("registerWaiter", &NativeMessageWaiterRegistry::RegisterWaiter),
				InstanceMethod("resolveMessage", &NativeMessageWaiterRegistry::ResolveMessage),
				InstanceMethod("removeWaiter", &NativeMessageWaiterRegistry::RemoveWaiter),
				InstanceMethod("evictExpired", &NativeMessageWaiterRegistry::EvictExpired),
				InstanceMethod("rejectAll", &NativeMessageWaiterRegistry::RejectAll),
				InstanceMethod("size", &NativeMessageWaiterRegistry::Size),
				InstanceMethod("clear", &NativeMessageWaiterRegistry::Clear)
			}
		);

		constructor_ = Napi::Persistent(func);
		constructor_.SuppressDestruct();
		exports.Set("NativeMessageWaiterRegistry", func);
		return func;
	}

	NativeMessageWaiterRegistry(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NativeMessageWaiterRegistry>(info) {}

private:
	struct MessageWaiterEntry {
		uint32_t token = 0;
		uint64_t deadlineMs = 0;
	};

	struct TokenEntry {
		std::string msgId;
		size_t indexInMessage = 0;
		uint64_t deadlineMs = 0;
	};

	struct DeadlineEntry {
		uint64_t deadlineMs = 0;
		uint32_t token = 0;
	};

	struct DeadlineEntryGreater {
		bool operator()(const DeadlineEntry& lhs, const DeadlineEntry& rhs) const {
			return lhs.deadlineMs > rhs.deadlineMs;
		}
	};

	static Napi::FunctionReference constructor_;

	std::unordered_map<std::string, std::vector<MessageWaiterEntry>> waitersByMessage_;
	std::unordered_map<uint32_t, TokenEntry> waitersByToken_;
	std::priority_queue<DeadlineEntry, std::vector<DeadlineEntry>, DeadlineEntryGreater> deadlineMinHeap_;

	bool RemoveTokenInternal(uint32_t token) {
		const auto tokenIt = waitersByToken_.find(token);
		if (tokenIt == waitersByToken_.end()) {
			return false;
		}

		const TokenEntry tokenEntry = tokenIt->second;
		auto messageIt = waitersByMessage_.find(tokenEntry.msgId);
		if (messageIt == waitersByMessage_.end()) {
			waitersByToken_.erase(tokenIt);
			return false;
		}

		auto& entries = messageIt->second;
		if (tokenEntry.indexInMessage >= entries.size()) {
			waitersByToken_.erase(tokenIt);
			if (entries.empty()) {
				waitersByMessage_.erase(messageIt);
			}
			return false;
		}

		const size_t lastIndex = entries.size() - 1;
		if (tokenEntry.indexInMessage != lastIndex) {
			const MessageWaiterEntry moved = entries[lastIndex];
			entries[tokenEntry.indexInMessage] = moved;
			auto movedTokenIt = waitersByToken_.find(moved.token);
			if (movedTokenIt != waitersByToken_.end()) {
				movedTokenIt->second.indexInMessage = tokenEntry.indexInMessage;
			}
		}

		entries.pop_back();
		if (entries.empty()) {
			waitersByMessage_.erase(messageIt);
		}

		waitersByToken_.erase(tokenIt);
		return true;
	}

	bool IsActiveDeadline(uint32_t token, uint64_t deadlineMs) const {
		const auto tokenIt = waitersByToken_.find(token);
		if (tokenIt == waitersByToken_.end()) {
			return false;
		}
		return tokenIt->second.deadlineMs == deadlineMs && deadlineMs > 0;
	}

	Napi::Value RegisterWaiter(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 3) {
			Napi::TypeError::New(env, "registerWaiter(msgId, token, deadlineMs) requires 3 arguments")
				.ThrowAsJavaScriptException();
			return env.Null();
		}

		std::string msgId;
		if (!common::ReadStringFromValue(env, info[0], "msgId", &msgId)) {
			return env.Null();
		}

		uint32_t token = 0;
		if (!common::ReadUInt32FromValue(env, info[1], "token", &token)) {
			return env.Null();
		}

		uint64_t deadlineMs = 0;
		if (!ReadDeadlineMs(env, info[2], "deadlineMs", &deadlineMs)) {
			return env.Null();
		}

		if (msgId.empty()) {
			Napi::RangeError::New(env, "msgId must not be empty").ThrowAsJavaScriptException();
			return env.Null();
		}

		RemoveTokenInternal(token);

		auto& entries = waitersByMessage_[msgId];
		entries.push_back({token, deadlineMs});
		const size_t indexInMessage = entries.size() - 1;
		waitersByToken_[token] = TokenEntry{msgId, indexInMessage, deadlineMs};
		if (deadlineMs > 0) {
			deadlineMinHeap_.push({deadlineMs, token});
		}

		return env.Undefined();
	}

	Napi::Value ResolveMessage(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "resolveMessage(msgId) requires msgId").ThrowAsJavaScriptException();
			return env.Null();
		}

		std::string msgId;
		if (!common::ReadStringFromValue(env, info[0], "msgId", &msgId)) {
			return env.Null();
		}

		const auto messageIt = waitersByMessage_.find(msgId);
		if (messageIt == waitersByMessage_.end() || messageIt->second.empty()) {
			return Napi::Array::New(env, 0);
		}

		std::vector<uint32_t> tokens;
		tokens.reserve(messageIt->second.size());
		for (const auto& entry : messageIt->second) {
			tokens.push_back(entry.token);
		}

		for (uint32_t token : tokens) {
			RemoveTokenInternal(token);
		}

		Napi::Array out = Napi::Array::New(env, tokens.size());
		for (uint32_t i = 0; i < static_cast<uint32_t>(tokens.size()); ++i) {
			out.Set(i, Napi::Number::New(env, tokens[i]));
		}

		return out;
	}

	Napi::Value RemoveWaiter(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "removeWaiter(token) requires token").ThrowAsJavaScriptException();
			return env.Null();
		}

		uint32_t token = 0;
		if (!common::ReadUInt32FromValue(env, info[0], "token", &token)) {
			return env.Null();
		}

		const bool removed = RemoveTokenInternal(token);
		return Napi::Boolean::New(env, removed);
	}

	Napi::Value EvictExpired(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		if (info.Length() < 1) {
			Napi::TypeError::New(env, "evictExpired(nowMs) requires nowMs").ThrowAsJavaScriptException();
			return env.Null();
		}

		uint64_t nowMs = 0;
		if (!ReadDeadlineMs(env, info[0], "nowMs", &nowMs)) {
			return env.Null();
		}

		std::vector<uint32_t> expiredTokens;
		while (!deadlineMinHeap_.empty()) {
			const DeadlineEntry current = deadlineMinHeap_.top();
			if (current.deadlineMs > nowMs) {
				break;
			}

			deadlineMinHeap_.pop();
			if (!IsActiveDeadline(current.token, current.deadlineMs)) {
				continue;
			}

			if (RemoveTokenInternal(current.token)) {
				expiredTokens.push_back(current.token);
			}
		}

		Napi::Array out = Napi::Array::New(env, expiredTokens.size());
		for (uint32_t i = 0; i < static_cast<uint32_t>(expiredTokens.size()); ++i) {
			out.Set(i, Napi::Number::New(env, expiredTokens[i]));
		}
		return out;
	}

	Napi::Value RejectAll(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env();
		Napi::Array out = Napi::Array::New(env, waitersByToken_.size());

		uint32_t index = 0;
		for (const auto& pair : waitersByToken_) {
			out.Set(index++, Napi::Number::New(env, pair.first));
		}

		waitersByMessage_.clear();
		waitersByToken_.clear();
		deadlineMinHeap_ = std::priority_queue<DeadlineEntry, std::vector<DeadlineEntry>, DeadlineEntryGreater>();
		return out;
	}

	Napi::Value Size(const Napi::CallbackInfo& info) {
		return Napi::Number::New(info.Env(), static_cast<double>(waitersByToken_.size()));
	}

	Napi::Value Clear(const Napi::CallbackInfo& info) {
		waitersByMessage_.clear();
		waitersByToken_.clear();
		deadlineMinHeap_ = std::priority_queue<DeadlineEntry, std::vector<DeadlineEntry>, DeadlineEntryGreater>();
		return info.Env().Undefined();
	}
};

Napi::FunctionReference NativeMessageWaiterRegistry::constructor_;

} // namespace

Napi::Function InitNativeMessageWaiterRegistry(Napi::Env env, Napi::Object exports) {
	return NativeMessageWaiterRegistry::Init(env, exports);
}

} // namespace baileys_native

