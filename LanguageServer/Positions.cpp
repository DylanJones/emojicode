//
//  Positions.cpp
//  EmojicodeLanguageServer
//

#include "Positions.hpp"
#include "Lex/SourceManager.hpp"
#include <algorithm>
#include <codecvt>
#include <locale>

namespace EmojicodeLanguageServer {

LineIndex::LineIndex(const std::u32string &text) : text_(text) {
    lineStarts_.push_back(0);
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == U'\n') {
            lineStarts_.push_back(i + 1);
        }
    }
}

std::u32string_view LineIndex::line(size_t line) const {
    if (line >= lineStarts_.size()) {
        return {};
    }
    auto start = lineStarts_[line];
    auto end = line + 1 < lineStarts_.size() ? lineStarts_[line + 1] - 1 : text_.size();
    return std::u32string_view(text_).substr(start, end - start);
}

static size_t units(char32_t c, PositionEncoding encoding) {
    switch (encoding) {
        case PositionEncoding::UTF32:
            return 1;
        case PositionEncoding::UTF16:
            return c >= 0x10000 ? 2 : 1;
        case PositionEncoding::UTF8:
            return c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    }
    return 1;
}

ClientPosition LineIndex::toClient(size_t line, size_t character, PositionEncoding encoding) const {
    if (line >= lineStarts_.size()) {
        line = lineStarts_.size() - 1;
        character = SIZE_MAX;
    }
    auto text = this->line(line);
    character = std::min(character, text.size());
    size_t result = 0;
    for (size_t i = 0; i < character; i++) {
        result += units(text[i], encoding);
    }
    return ClientPosition{line, result};
}

size_t LineIndex::toCodePoint(ClientPosition position, PositionEncoding encoding) const {
    auto text = line(position.line);
    size_t counted = 0;
    size_t i = 0;
    for (; i < text.size() && counted < position.character; i++) {
        counted += units(text[i], encoding);
    }
    return i;
}

size_t LineIndex::offset(size_t line, size_t character) const {
    if (line >= lineStarts_.size()) {
        return text_.size();
    }
    return lineStarts_[line] + std::min(character, this->line(line).size());
}

std::pair<size_t, size_t> LineIndex::lineAndCharacter(size_t offset) const {
    auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    auto line = static_cast<size_t>(it - lineStarts_.begin()) - 1;
    return {line, offset - lineStarts_[line]};
}

static int hexValue(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string uriToPath(const std::string &uri) {
    const std::string scheme = "file://";
    if (uri.compare(0, scheme.size(), scheme) != 0) {
        return "";
    }
    std::string path;
    for (size_t i = scheme.size(); i < uri.size(); i++) {
        if (uri[i] == '%' && i + 2 < uri.size() && hexValue(uri[i + 1]) >= 0 && hexValue(uri[i + 2]) >= 0) {
            path.push_back(static_cast<char>(hexValue(uri[i + 1]) * 16 + hexValue(uri[i + 2])));
            i += 2;
        }
        else {
            path.push_back(uri[i]);
        }
    }
    // file://host/path: only local files are supported, so the authority is dropped.
    if (!path.empty() && path[0] != '/') {
        auto slash = path.find('/');
        path = slash == std::string::npos ? "" : path.substr(slash);
    }
    return EmojicodeCompiler::canonicalPath(path);
}

std::string pathToUri(const std::string &path) {
    static const char *hex = "0123456789ABCDEF";
    std::string uri = "file://";
    for (unsigned char c : path) {
        if (isalnum(c) || c == '/' || c == '-' || c == '.' || c == '_' || c == '~') {
            uri.push_back(static_cast<char>(c));
        }
        else {
            uri.push_back('%');
            uri.push_back(hex[c >> 4]);
            uri.push_back(hex[c & 15]);
        }
    }
    return uri;
}

std::u32string utf32(const std::string &utf8) {
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    return conv.from_bytes(utf8);
}

std::string utf8(std::u32string_view utf32) {
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    return conv.to_bytes(utf32.data(), utf32.data() + utf32.size());
}

}  // namespace EmojicodeLanguageServer
