//
//  EmojicodeCompiler.h
//  Emojicode
//
//  Created by Theo Weidmann on 01.03.15.
//  Copyright (c) 2015 Theo Weidmann. All rights reserved.
//

#ifndef EmojicodeCompiler_hpp
#define EmojicodeCompiler_hpp

#include <algorithm>
#include <sstream>
#include <string>

namespace EmojicodeCompiler {

/// Encodes UTF-8. Surrogates and values above U+10FFFF, which cannot be encoded, become U+FFFD.
inline std::string utf8(const std::u32string &s) {
    std::string result;
    result.reserve(s.size());
    for (char32_t c : s) {
        if (c > 0x10FFFF || (0xD800 <= c && c <= 0xDFFF)) {
            c = 0xFFFD;
        }
        if (c < 0x80) {
            result.push_back(static_cast<char>(c));
        }
        else if (c < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (c >> 6)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else if (c < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (c >> 12)));
            result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else {
            result.push_back(static_cast<char>(0xF0 | (c >> 18)));
            result.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return result;
}

/// Decodes UTF-8. Invalid sequences, surrogates and overlong encodings become U+FFFD instead of raising an error,
/// so that text from editors and files can always be processed.
inline std::u32string utf32(const std::string &s) {
    std::u32string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        auto byte = static_cast<unsigned char>(s[i]);
        size_t length = byte < 0x80 ? 1 : (byte >> 5) == 0x6 ? 2 : (byte >> 4) == 0xE ? 3 : (byte >> 3) == 0x1E ? 4 : 0;
        char32_t c = length == 1 ? byte : length == 2 ? byte & 0x1F : length == 3 ? byte & 0x0F : byte & 0x07;
        bool valid = length > 0 && i + length <= s.size();
        for (size_t j = 1; valid && j < length; j++) {
            auto continuation = static_cast<unsigned char>(s[i + j]);
            valid = (continuation >> 6) == 0x2;
            c = (c << 6) | (continuation & 0x3F);
        }
        static const char32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
        if (!valid || c < minimum[length] || c > 0x10FFFF || (0xD800 <= c && c <= 0xDFFF)) {
            result.push_back(0xFFFD);
            i++;
            continue;
        }
        result.push_back(c);
        i += length;
    }
    return result;
}

inline bool endsWith(const std::string &value, const std::string &ending) {
    if (ending.size() > value.size()) {
        return false;
    }
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

template<typename Head>
void appendToStream(std::stringstream &stream, Head head) {
    stream << head;
}

template<typename Head, typename... Args>
void appendToStream(std::stringstream &stream, Head head, Args... args) {
    stream << head;
    appendToStream(stream, args...);
}

}  // namespace EmojicodeCompiler

#endif /* EmojicodeCompiler_hpp */
