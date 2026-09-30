#ifndef ByteSearch_h
#define ByteSearch_h

#include "../runtime/Runtime.h"
#include <algorithm>

namespace s {

/// Finds the first occurrence of the needle in the haystack at or after offset. Returns NoValue if offset is not
/// inside the haystack (negative or >= count) or the needle cannot be found.
inline runtime::SimpleOptional<runtime::Integer> findBytesFromOffset(const char *haystack, runtime::Integer count,
                                                                     const char *needle, runtime::Integer needleCount,
                                                                     runtime::Integer offset) {
    if (offset < 0 || offset >= count) {
        return runtime::NoValue;
    }
    auto end = haystack + count;
    auto pos = std::search(haystack + offset, end, needle, needle + needleCount);
    if (pos != end) {
        return pos - haystack;
    }
    return runtime::NoValue;
}

}  // namespace s

#endif /* ByteSearch_h */
