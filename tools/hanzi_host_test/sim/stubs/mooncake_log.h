/*
 * Host-sim stub for components/mooncake_log/src/mooncake_log.h.
 *
 * The real component checks format strings against fmt's `{}` syntax, but the
 * app_hanzi view/engine call sites use printf-style specifiers (%u, %s), so
 * this stub matches call-site reality: plain C varargs to stderr.
 */
#pragma once
#include <cstdarg>
#include <cstdio>
#include <string_view>

namespace mclog {

namespace internal {

inline void vlog(const char* level, std::string_view tag, const char* fmt, va_list args)
{
    std::fprintf(stderr, "[%s][%.*s] ", level, static_cast<int>(tag.size()), tag.data());
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
}

}  // namespace internal

#define MCLOG_DEFINE_TAG_FN(name, level)                                     \
    inline void name(std::string_view tag, const char* fmt, ...)             \
    {                                                                        \
        va_list args;                                                       \
        va_start(args, fmt);                                                 \
        internal::vlog(level, tag, fmt, args);                              \
        va_end(args);                                                       \
    }

MCLOG_DEFINE_TAG_FN(tagInfo, "I")
MCLOG_DEFINE_TAG_FN(tagWarn, "W")
MCLOG_DEFINE_TAG_FN(tagError, "E")
MCLOG_DEFINE_TAG_FN(tagDebug, "D")

#undef MCLOG_DEFINE_TAG_FN

}  // namespace mclog
