#include "utils/zlib_inflate.h"

#include "third_party/miniz_adapter.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace baileys_native::utils {

namespace {

constexpr size_t kInitialInflateGuessMultiplier = 3;
constexpr size_t kMinInflateBufferSize = 1024;
constexpr size_t kMaxInflateBufferSize = 512u * 1024u * 1024u;

const char* MinizStatusToText(int status) {
	switch (status) {
		case MZ_OK:
			return "ok";
		case MZ_STREAM_END:
			return "stream end";
		case MZ_NEED_DICT:
			return "need dictionary";
		case MZ_ERRNO:
			return "errno";
		case MZ_STREAM_ERROR:
			return "stream error";
		case MZ_DATA_ERROR:
			return "data error";
		case MZ_MEM_ERROR:
			return "memory error";
		case MZ_BUF_ERROR:
			return "buffer too small";
		case MZ_VERSION_ERROR:
			return "version error";
		case MZ_PARAM_ERROR:
			return "invalid parameter";
		default:
			return "unknown";
	}
}

} // namespace

bool InflateZlibBytes(const uint8_t* input, size_t length, std::vector<uint8_t>* out, std::string* error) {
	out->clear();

	if (length == 0) {
		return true;
	}
	if (length > static_cast<size_t>(std::numeric_limits<mz_ulong>::max())) {
		*error = "zlib payload too large";
		return false;
	}

	const size_t maxBufferByMiniz = static_cast<size_t>(std::numeric_limits<mz_ulong>::max());
	const size_t hardCap = std::min(kMaxInflateBufferSize, maxBufferByMiniz);
	size_t guess = length * kInitialInflateGuessMultiplier;
	if (guess < kMinInflateBufferSize) {
		guess = kMinInflateBufferSize;
	}
	if (guess > hardCap) {
		guess = hardCap;
	}

	size_t capacity = guess;
	while (true) {
		out->resize(capacity);
		mz_ulong dstLen = static_cast<mz_ulong>(capacity);
		mz_ulong srcLen = static_cast<mz_ulong>(length);

		const int status = mz_uncompress2(
			out->data(),
			&dstLen,
			input,
			&srcLen
		);

		if (status == MZ_OK) {
			out->resize(static_cast<size_t>(dstLen));
			return true;
		}

		if (status == MZ_BUF_ERROR && srcLen == static_cast<mz_ulong>(length) && capacity < hardCap) {
			const size_t nextCapacity = capacity > (hardCap / 2) ? hardCap : (capacity * 2);
			if (nextCapacity == capacity) {
				break;
			}
			capacity = nextCapacity;
			continue;
		}

		*error = std::string("zlib inflate failed: ") + MinizStatusToText(status);
		out->clear();
		return false;
	}

	*error = "zlib inflate failed: exceeded max inflate buffer";
	out->clear();
	return false;
}

} // namespace baileys_native::utils
