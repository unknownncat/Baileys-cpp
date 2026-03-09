#include "message_node_decoder.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace baileys_native {

namespace {

bool EndsWith(const std::string& value, const char* suffix) {
	const size_t suffixLen = std::char_traits<char>::length(suffix);
	return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

bool IsPnUser(const std::string& jid) {
	return EndsWith(jid, "@s.whatsapp.net");
}

bool IsLidUser(const std::string& jid) {
	return EndsWith(jid, "@lid");
}

bool IsHostedPnUser(const std::string& jid) {
	return EndsWith(jid, "@hosted");
}

bool IsHostedLidUser(const std::string& jid) {
	return EndsWith(jid, "@hosted.lid");
}

bool IsJidBroadcast(const std::string& jid) {
	return EndsWith(jid, "@broadcast");
}

bool IsJidGroup(const std::string& jid) {
	return EndsWith(jid, "@g.us");
}

bool IsJidStatusBroadcast(const std::string& jid) {
	return jid == "status@broadcast";
}

bool IsJidNewsletter(const std::string& jid) {
	return EndsWith(jid, "@newsletter");
}

bool IsJidMetaAI(const std::string& jid) {
	return EndsWith(jid, "@bot");
}

bool ParseJidUser(const std::string& jid, std::string* outUser) {
	const size_t sepIdx = jid.find('@');
	if (sepIdx == std::string::npos) {
		return false;
	}

	*outUser = jid.substr(0, sepIdx);
	return true;
}

bool AreJidsSameUser(const std::string& jid1, const std::string& jid2) {
	std::string user1;
	std::string user2;
	const bool has1 = ParseJidUser(jid1, &user1);
	const bool has2 = ParseJidUser(jid2, &user2);
	if (!has1 && !has2) {
		return true;
	}
	if (!has1 || !has2) {
		return false;
	}

	return user1 == user2;
}

bool ReadAttrString(const Napi::Object& attrs, const char* key, std::string* out) {
	const Napi::Value value = attrs.Get(key);
	if (value.IsUndefined() || value.IsNull()) {
		return false;
	}

	if (!value.IsString() && !value.IsNumber() && !value.IsBoolean()) {
		return false;
	}

	*out = value.ToString().Utf8Value();
	return true;
}

double ParseNumberLikeJs(const std::string& value) {
	if (value.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	errno = 0;
	char* endPtr = nullptr;
	const double parsed = std::strtod(value.c_str(), &endPtr);
	if (endPtr == value.c_str()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	while (*endPtr != '\0') {
		if (*endPtr != ' ' && *endPtr != '\t' && *endPtr != '\n' && *endPtr != '\r' && *endPtr != '\f' &&
			*endPtr != '\v') {
			return std::numeric_limits<double>::quiet_NaN();
		}
		++endPtr;
	}

	if (errno == ERANGE) {
		return parsed > 0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
	}

	return parsed;
}

const std::string& FirstNonEmpty(const std::string& a, const std::string& b, const std::string& c) {
	if (!a.empty()) {
		return a;
	}
	if (!b.empty()) {
		return b;
	}
	return c;
}

} // namespace

Napi::Value DecodeMessageNodeFast(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	if (info.Length() < 3 || !info[0].IsObject()) {
		return env.Null();
	}

	std::string meId;
	std::string meLid;
	if (info[1].IsString()) {
		meId = info[1].As<Napi::String>().Utf8Value();
	}
	if (info[2].IsString()) {
		meLid = info[2].As<Napi::String>().Utf8Value();
	}

	const Napi::Object attrs = info[0].As<Napi::Object>();
	std::string from;
	if (!ReadAttrString(attrs, "from", &from) || from.empty()) {
		return env.Null();
	}

	std::string participant;
	std::string recipient;
	std::string msgId;
	std::string category;
	std::string notify;
	std::string serverId;
	std::string timestampRaw;

	ReadAttrString(attrs, "participant", &participant);
	ReadAttrString(attrs, "recipient", &recipient);
	ReadAttrString(attrs, "id", &msgId);
	ReadAttrString(attrs, "category", &category);
	ReadAttrString(attrs, "notify", &notify);
	ReadAttrString(attrs, "server_id", &serverId);
	ReadAttrString(attrs, "t", &timestampRaw);

	const std::string senderForAddressing = !participant.empty() ? participant : from;

	std::string addressingMode;
	if (!ReadAttrString(attrs, "addressing_mode", &addressingMode) || addressingMode.empty()) {
		addressingMode = EndsWith(senderForAddressing, "lid") ? "lid" : "pn";
	}

	std::string participantPn;
	std::string senderPn;
	std::string peerRecipientPn;
	std::string recipientPn;
	std::string participantLid;
	std::string senderLid;
	std::string peerRecipientLid;
	std::string recipientLid;

	ReadAttrString(attrs, "participant_pn", &participantPn);
	ReadAttrString(attrs, "sender_pn", &senderPn);
	ReadAttrString(attrs, "peer_recipient_pn", &peerRecipientPn);
	ReadAttrString(attrs, "recipient_pn", &recipientPn);
	ReadAttrString(attrs, "participant_lid", &participantLid);
	ReadAttrString(attrs, "sender_lid", &senderLid);
	ReadAttrString(attrs, "peer_recipient_lid", &peerRecipientLid);
	ReadAttrString(attrs, "recipient_lid", &recipientLid);

	const std::string senderAlt = addressingMode == "lid"
		? FirstNonEmpty(participantPn, senderPn, peerRecipientPn)
		: FirstNonEmpty(participantLid, senderLid, peerRecipientLid);

	const std::string recipientAlt = addressingMode == "lid" ? recipientPn : recipientLid;

	bool fromMe = false;
	std::string chatId;
	std::string author;
	std::string sender;
	bool newsletter = false;

	if (IsPnUser(from) || IsLidUser(from) || IsHostedLidUser(from) || IsHostedPnUser(from)) {
		const bool isFromMe = AreJidsSameUser(from, meId) || AreJidsSameUser(from, meLid);
		if (!recipient.empty() && !IsJidMetaAI(recipient)) {
			if (isFromMe) {
				fromMe = true;
				chatId = recipient;
			} else {
				// Recent LID-addressed direct messages can include recipient=peer_lid.
				// Treat them as inbound chats instead of rejecting the envelope.
				chatId = from;
			}
		} else {
			chatId = from;
		}

		author = from;
		sender = author;
	} else if (IsJidGroup(from)) {
		if (participant.empty()) {
			return env.Null();
		}

		if (AreJidsSameUser(participant, meId) || AreJidsSameUser(participant, meLid)) {
			fromMe = true;
		}

		author = participant;
		chatId = from;
		sender = chatId;
	} else if (IsJidBroadcast(from)) {
		if (participant.empty()) {
			return env.Null();
		}

		const bool isParticipantMe = AreJidsSameUser(participant, meId);
		fromMe = isParticipantMe;
		author = participant;
		chatId = from;
		sender = chatId;
	} else if (IsJidNewsletter(from)) {
		newsletter = true;
		chatId = from;
		author = from;
		sender = chatId;

		if (AreJidsSameUser(from, meId) || AreJidsSameUser(from, meLid)) {
			fromMe = true;
		}
	} else {
		return env.Null();
	}

	const Napi::Object key = Napi::Object::New(env);
	key.Set("remoteJid", chatId);
	key.Set("fromMe", Napi::Boolean::New(env, fromMe));
	if (!msgId.empty()) {
		key.Set("id", msgId);
	}
	if (!participant.empty()) {
		key.Set("participant", participant);
	}
	key.Set("addressingMode", addressingMode);

	if (IsJidGroup(chatId)) {
		if (!senderAlt.empty()) {
			key.Set("participantAlt", senderAlt);
		}
	} else if (!senderAlt.empty()) {
		key.Set("remoteJidAlt", senderAlt);
	}

	if (newsletter && !serverId.empty()) {
		key.Set("server_id", serverId);
	}

	const Napi::Object out = Napi::Object::New(env);
	out.Set("key", key);
	out.Set("author", author);
	out.Set("sender", sender);
	if (!category.empty()) {
		out.Set("category", category);
	}
	out.Set("messageTimestamp", Napi::Number::New(env, ParseNumberLikeJs(timestampRaw)));
	if (!notify.empty()) {
		out.Set("pushName", notify);
	}
	out.Set("broadcast", Napi::Boolean::New(env, IsJidBroadcast(from)));
	if (!recipientAlt.empty()) {
		out.Set("recipientAlt", recipientAlt);
	}

	return out;
}

} // namespace baileys_native
