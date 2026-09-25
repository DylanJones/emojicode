//
//  Checker.hpp
//  EmojicodeLanguageServer
//

#ifndef Checker_hpp
#define Checker_hpp

#include "Compiler.hpp"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace EmojicodeLanguageServer {

/// A place in a file as the compiler reports it: line and character start at 1, character counts code points.
struct Location {
    std::string path;
    size_t line = 1;
    size_t character = 1;
};

struct Diagnostic {
    enum class Severity { Error = 1, Warning = 2 };

    Location location;
    Severity severity;
    std::string message;
    std::vector<std::pair<Location, std::string>> notes;
};

class Index;

/// Everything known about a package after it was checked.
struct Analysis {
    Analysis();
    Analysis(Analysis &&);
    Analysis& operator=(Analysis &&);
    ~Analysis();

    std::string rootPath;
    /// What the analysis found out, by position. Declared before the compiler, which refers to it, so that it is
    /// destroyed after it.
    std::unique_ptr<Index> index;
    /// The compiler that checked the package. It owns the packages, types, functions and their ASTs. nullptr if the
    /// compiler crashed.
    std::unique_ptr<EmojicodeCompiler::Compiler> compiler;
    std::vector<Diagnostic> diagnostics;
    /// The canonical paths of the files that the compiler read, i.e. the files of the package and its imports.
    std::set<std::string> files;
    /// The text of the open files, by canonical path, when they were checked.
    std::map<std::string, std::u32string> texts;
    /// Whether the bodies of the package's functions were analysed, i.e. whether it had no syntax errors and no errors
    /// in its declarations. Analysis continues after errors in a function, so the other functions are analysed even
    /// if there are errors.
    bool analysed = false;
};

/// Checks packages with the compiler.
class Checker {
public:
    /// @param searchPaths Where to search for packages, before the defaults. See Compiler::searchPackage.
    /// @param overlays The content of files that are open in the editor, by canonical path.
    Checker(std::vector<std::string> searchPaths, const std::map<std::string, std::u32string> &overlays)
        : searchPaths_(std::move(searchPaths)), overlays_(overlays) {}

    /// Returns the file that the compiler must be given to check the file at @p path: the file that includes it
    /// with 📜, or the file that includes that one, and so on.
    std::string rootFile(const std::string &path) const;

    /// Returns the content of the file at @p path, from the overlays if it is open. Returns an empty string if the
    /// file cannot be read.
    std::u32string read(const std::string &path) const;

    /// Parses and analyses the package whose main file is @p rootPath.
    Analysis check(const std::string &rootPath) const;

    /// Returns the canonical paths of the files that the file at @p path includes with 📜.
    std::vector<std::string> includes(const std::string &path) const;

private:
    /// The most directory entries that are looked at to find the file that includes a file.
    static constexpr size_t kMaxIncluderSearchEntries = 5000;

    /// Returns a file that includes the file at the canonical @p path, or an empty string.
    std::string includer(const std::string &path) const;
    /// Whether the file at @p rootPath is the main file of a package that is not a program.
    bool isLibrary(const std::string &rootPath) const;

    std::vector<std::string> searchPaths_;
    const std::map<std::string, std::u32string> &overlays_;
};

}  // namespace EmojicodeLanguageServer

#endif /* Checker_hpp */
