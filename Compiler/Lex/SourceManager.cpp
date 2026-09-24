//
// Created by Theo Weidmann on 22.03.18.
//

#include "CompilerError.hpp"
#include "SourceManager.hpp"
#include "SourcePosition.hpp"
#include <algorithm>
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <locale>

namespace EmojicodeCompiler {

std::string canonicalPath(const std::string &path) {
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path, error), error);
    return error ? path : canonical.string();
}

void SourceManager::setOverlay(const std::string &path, std::u32string content) {
    overlays_[canonicalPath(path)] = std::move(content);
}

SourceFile* SourceManager::read(std::string file) {
    auto find = cache_.find(file);
    if (find != cache_.end() && !find->second->wasCleared()) {
        return find->second.get();
    }

    std::u32string content;
    auto overlay = overlays_.empty() ? overlays_.end() : overlays_.find(canonicalPath(file));
    if (overlay != overlays_.end()) {
        content = overlay->second;
    }
    else {
        std::ifstream f(file, std::ios_base::binary | std::ios_base::in);
        if (f.fail()) {
            throw CompilerError(SourcePosition(), "Couldn't read input file ", file, ".");
        }

        auto string = std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
        content = conv.from_bytes(string);
    }

    if (find != cache_.end()) {
        find->second->setContent(std::move(content));
        return find->second.get();
    }

    auto cache = std::make_unique<SourceFile>(std::move(content), file);
    auto ptr = cache.get();
    cache_.emplace(file, std::move(cache));
    return ptr;
}

void SourceFile::findComments(const SourcePosition &a, const SourcePosition &b,
                              const std::function<void (const Token &)> &comment) const {
    auto end = comments_.upper_bound(std::make_pair(b.line, b.character));
    auto it = comments_.lower_bound(std::make_pair(a.line, a.character));
    if (it != comments_.end()) {
        for (; it != end; it++) {
            comment(it->second);
        }
    }
}

}  // namespace EmojicodeCompiler
