//
//  Checker.cpp
//  EmojicodeLanguageServer
//

#include "Checker.hpp"
#include "Index.hpp"
#include "CompilerError.hpp"
#include "Lex/SourceManager.hpp"
#include "Positions.hpp"
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

/// Records that the phases before it completed without errors.
class MarkPhase : public Compiler::Phase {
public:
    explicit MarkPhase(bool *reached) : reached_(reached) {}
    void perform(Compiler *compiler) override { *reached_ = true; }

private:
    bool *reached_;
};

bool isSourceFile(const fs::path &path) {
    auto name = path.filename().string();
    return endsWith(name, ".emojic") || endsWith(name, ".🍇");
}

}  // namespace

Analysis::Analysis() = default;
Analysis::Analysis(Analysis &&) = default;
Analysis& Analysis::operator=(Analysis &&) = default;
Analysis::~Analysis() = default;

std::u32string Checker::read(const std::string &path) const {
    auto overlay = overlays_.find(path);
    if (overlay != overlays_.end()) {
        return overlay->second;
    }
    std::ifstream stream(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    try {
        return utf32(content);
    }
    catch (std::range_error &) {
        return U"";
    }
}

std::string Checker::includer(const std::string &path) const {
    // 📜 paths are relative to the including file, which is therefore in the same directory or above.
    auto directory = fs::path(path).parent_path();
    for (int level = 0; level < 3 && !directory.empty(); level++, directory = directory.parent_path()) {
        std::error_code error;
        for (auto &entry : fs::directory_iterator(directory, error)) {
            if (!entry.is_regular_file() || !isSourceFile(entry.path()) || entry.path() == fs::path(path)) {
                continue;
            }
            auto content = read(entry.path().string());
            for (size_t i = content.find(U'📜'); i != std::u32string::npos; i = content.find(U'📜', i + 1)) {
                auto begin = content.find(U'🔤', i);
                auto end = begin == std::u32string::npos ? begin : content.find(U'🔤', begin + 1);
                if (end == std::u32string::npos) {
                    break;
                }
                // Only whitespace may come between 📜 and the string.
                if (content.find_first_not_of(U" \t️", i + 1) != begin) {
                    continue;
                }
                auto included = directory / utf8(content.substr(begin + 1, end - begin - 1));
                if (canonicalPath(included.string()) == path) {
                    return entry.path().string();
                }
            }
        }
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

Analysis Checker::check(const std::string &rootPath) const {
    Analysis analysis;
    analysis.rootPath = rootPath;

    // A package's main file is named like its directory, e.g. s/s.🍇. Other files are standalone programs.
    auto root = fs::path(rootPath);
    auto packageName = root.stem().string() == root.parent_path().filename().string() ? root.stem().string() : "_";
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
    analysis.index = std::make_unique<Index>();
    analysis.compiler->setAnalysisObserver(analysis.index.get());
    analysis.compiler->add<Compiler::ParsePhase>();
    // The compiler stops after the first phase with errors, so this marks whether the package parsed.
    analysis.compiler->add<MarkPhase>(&analysis.analysed);
    analysis.compiler->add<Compiler::AnalysisPhase>(standalone);
    try {
        analysis.compiler->compile();
        analysis.index->finish();
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
