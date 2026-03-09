#pragma once

#if defined(BAILEYS_HAS_NATIVE_WAPROTO) && BAILEYS_HAS_NATIVE_WAPROTO
#define BAILEYS_NATIVE_HAS_WAPROTO_BUILD 1
#else
#define BAILEYS_NATIVE_HAS_WAPROTO_BUILD 0
#endif

#ifdef _WIN32
#define BAILEYS_NATIVE_IS_WINDOWS_BUILD 1
#else
#define BAILEYS_NATIVE_IS_WINDOWS_BUILD 0
#endif

namespace baileys_native::common::feature_gates {

inline constexpr bool kHasNativeWaProto = BAILEYS_NATIVE_HAS_WAPROTO_BUILD != 0;
inline constexpr bool kIsWindowsBuild = BAILEYS_NATIVE_IS_WINDOWS_BUILD != 0;

} // namespace baileys_native::common::feature_gates
