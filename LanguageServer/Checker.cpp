//
//  Checker.cpp
//  EmojicodeLanguageServer
//

#include "Checker.hpp"
#include "Index.hpp"
#include "CompilerError.hpp"
#include "Lex/SourceManager.hpp"
#include "Package/RecordingPackage.hpp"
#include "Positions.hpp"
#include "Tokens.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

#ifndef defaultPackagesDirectory
#define defaultPackagesDirectory "/usr/local/EmojicodePackages"
#endif

namespace EmojicodeLanguageServer {

namespace fs = std::filesystem;
using namespace EmojicodeCompiler;

namespace {

/// Collects the errors and warnings of a compilation.
class CollectingDelegate : public CompilerDelegate {
public:
    CollectingDelegate(std::vector<Diagnostic> *diagnostics, std::string rootPath)
        : diagnostics_(diagnostics), rootPath_(std::move(rootPath)) {}

    void begin() override {}
    void finish() override {}

    void error(Compiler *compiler, const CompilerError &ce) override {
        Diagnostic diagnostic{location(ce.position()), Diagnostic::Severity::Error, ce.message(), {}};
        for (auto &note : ce.notes()) {
            diagnostic.notes.emplace_back(location(note.position), note.message);
        }
        diagnostics_->push_back(std::move(diagnostic));
    }

    void warn(Compiler *compiler, const std::string &message, const SourcePosition &p) override {
        // Warnings without a position are about the compiler, not the code.
        if (!p.isUnknown()) {
            diagnostics_->push_back(Diagnostic{location(p), Diagnostic::Severity::Warning, message, {}});
        }
    }

private:
    /// Errors without a position, e.g. about a file that cannot be read, are shown at the start of the root file.
    Location location(const SourcePosition &p) const {
        if (p.isUnknown()) {
            return Location{rootPath_, 1, 1};
        }
        return Location{canonicalPath(p.file->path()), p.line, p.character};
    }

    std::vector<Diagnostic> *diagnostics_;
    std::string rootPath_;
};

bool isSourceFile(const fs::path &path) {
    auto name = path.filename().string();
    return endsWith(name, ".emojic") || endsWith(name, ".🍇");
}

bool isInterfaceFile(const fs::path &path) {
    auto name = path.filename().string();
    return endsWith(name, "🏛") || endsWith(name, ".emojii");
}

/// Calls @p body with each token that is not in a block, i.e. that starts a declaration or statement of the
/// document. The lexer's tokens leave out those in comments and strings.
template <typename Body>
void eachTopLevelToken(const std::vector<TokenSpan> &tokens, Body body) {
    size_t depth = 0;
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i].type == TokenType::BlockBegin) {
            depth++;
        }
        else if (tokens[i].type == TokenType::BlockEnd) {
            depth -= depth > 0 ? 1 : 0;
        }
        else if (depth == 0) {
            body(i);
        }
    }
}

bool isTopLevelIdentifier(const TokenSpan &token, char32_t emoji) {
    return token.type == TokenType::Identifier && !token.value.empty() && token.value.front() == emoji;
}

}  // namespace

Analysis::Analysis() = default;
Analysis::Analysis(Analysis &&) = default;
Analysis& Analysis::operator=(Analysis &&) = default;
Analysis::~Analysis() = default;

std::u32string Checker::read(const std::string &path) const {
    // Overlays are keyed by canonical path, as in SourceManager.
    auto overlay = overlays_.find(canonicalPath(path));
    if (overlay != overlays_.end()) {
        return overlay->second;
    }
    try {
        return readSourceFile(path);
    }
    catch (CompilerError &) {
        return U"";
    }
}

std::vector<std::string> Checker::includes(const std::string &path) const {
    std::vector<std::string> includes;
    auto content = read(path);
    if (content.find(U'📜') == std::u32string::npos) {
        return includes;
    }
    // The compiler only reads 📜 where a declaration can start, and resolves the path relative to the document.
    auto tokens = lex(content);
    eachTopLevelToken(tokens, [&](size_t i) {
        if (isTopLevelIdentifier(tokens[i], U'📜') && i + 1 < tokens.size() &&
            tokens[i + 1].type == TokenType::String) {
            includes.emplace_back(canonicalPath((fs::path(path).parent_path() / utf8(tokens[i + 1].value)).string()));
        }
    });
    return includes;
}

std::string Checker::includer(const std::string &path) const {
    // A 📜 path is relative to the including document, which can be anywhere. It is looked for near the included
    // file, the nearest first: in its directory, then in the directory above and its subdirectories, and then in the
    // one above that and its subdirectories up to two levels deep.
    auto directory = fs::path(path).parent_path();
    std::set<fs::path> checked;
    auto ancestor = directory;
    for (int level = 0; level < 3; level++) {
        std::vector<fs::path> candidates;
        std::error_code error;
        size_t entries = 0;
        for (auto it = fs::recursive_directory_iterator(ancestor, fs::directory_options::skip_permission_denied,
                                                        error);
             !error && it != fs::recursive_directory_iterator() && entries++ < kMaxIncluderSearchEntries;
             it.increment(error)) {
            std::error_code entryError;
            if (it->is_directory(entryError)) {
                if (it.depth() >= level || it->path().filename().string().front() == '.') {
                    it.disable_recursion_pending();
                }
            }
            else if (isSourceFile(it->path()) && it->path() != fs::path(path) && it->is_regular_file(entryError) &&
                     checked.insert(it->path()).second) {
                candidates.push_back(it->path());
            }
        }
        auto distance = [&](const fs::path &candidate) {
            auto parent = candidate.parent_path();
            auto mismatch = std::mismatch(directory.begin(), directory.end(), parent.begin(), parent.end());
            return std::distance(mismatch.first, directory.end()) + std::distance(mismatch.second, parent.end());
        };
        std::sort(candidates.begin(), candidates.end(), [&](auto &a, auto &b) {
            return std::make_pair(distance(a), a) < std::make_pair(distance(b), b);
        });
        for (auto &candidate : candidates) {
            auto included = includes(candidate.string());
            if (std::find(included.begin(), included.end(), path) != included.end()) {
                return candidate.string();
            }
        }
        if (!ancestor.has_relative_path()) {
            break;
        }
        ancestor = ancestor.parent_path();
    }
    return "";
}

std::string Checker::rootFile(const std::string &path) const {
    std::set<std::string> visited{path};
    auto root = path;
    while (true) {
        auto next = includer(root);
        if (next.empty() || !visited.insert(next).second) {
            return root;
        }
        root = canonicalPath(next);
    }
}

bool Checker::isLibrary(const std::string &rootPath) const {
    // A package that is not a program has no 🏁 block, and exports types with 🌍 as it is of no use otherwise.
    bool exports = false;
    std::set<std::string> visited;
    std::vector<std::string> documents{rootPath};
    while (!documents.empty()) {
        auto path = documents.back();
        documents.pop_back();
        if (!visited.insert(path).second) {
            continue;
        }
        auto tokens = lex(read(path));
        bool start = false;
        eachTopLevelToken(tokens, [&](size_t i) {
            start = start || isTopLevelIdentifier(tokens[i], U'🏁');
            exports = exports || isTopLevelIdentifier(tokens[i], U'🌍');
        });
        if (start) {
            return false;
        }
        auto included = includes(path);
        documents.insert(documents.end(), included.begin(), included.end());
    }
    return exports;
}

Analysis Checker::check(const std::string &rootPath) const {
    Analysis analysis;
    analysis.rootPath = rootPath;

    auto root = fs::path(rootPath);
    std::string packageName = "_";
    if (isInterfaceFile(root)) {
        // The interface of an installed package, e.g. s/🏛, which go-to-definition opens.
        packageName = root.parent_path().filename().string();
    }
    else if (isLibrary(rootPath)) {
        packageName = root.stem().string();
    }
    bool standalone = packageName == "_";

    auto searchPaths = searchPaths_;
    searchPaths.push_back((root.parent_path() / "packages").string());
    if (const char *path = getenv("EMOJICODE_PACKAGES_PATH")) {
        searchPaths.emplace_back(path);
    }
    searchPaths.emplace_back(defaultPackagesDirectory);

    analysis.compiler = std::make_unique<Compiler>(packageName, rootPath, searchPaths,
                                                   std::make_unique<CollectingDelegate>(&analysis.diagnostics,
                                                                                        rootPath));
    for (auto &overlay : overlays_) {
        analysis.compiler->sourceManager().setOverlay(overlay.first, overlay.second);
    }
    analysis.texts = overlays_;
    analysis.index = std::make_unique<Index>();
    analysis.compiler->setAnalysisObserver(analysis.index.get());
    analysis.compiler->add<Compiler::ParsePhase>();
    analysis.compiler->add<Compiler::AnalysisPhase>(standalone);
    try {
        analysis.compiler->compile();
        for (auto &path : analysis.compiler->sourceManager().paths()) {
            analysis.files.insert(canonicalPath(path));
        }
        analysis.index->finish();
        analysis.analysed = analysis.index->analysedFunctionsOf(analysis.compiler->mainPackage());
    }
    catch (std::exception &e) {
        // The compiler is left in an unknown state, so nothing it produced is used.
        analysis.compiler = nullptr;
        analysis.index = nullptr;
        analysis.analysed = false;
        analysis.diagnostics.push_back(Diagnostic{Location{rootPath, 1, 1}, Diagnostic::Severity::Error,
                                                  std::string("The compiler crashed while checking this file: ") +
                                                  e.what(), {}});
    }
    return analysis;
}

}  // namespace EmojicodeLanguageServer
