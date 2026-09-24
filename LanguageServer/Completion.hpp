//
//  Completion.hpp
//  EmojicodeLanguageServer
//

#ifndef Completion_hpp
#define Completion_hpp

#include "Checker.hpp"
#include "Tokens.hpp"
#include <string>
#include <vector>

namespace EmojicodeLanguageServer {

struct CompletionItem {
    std::string label;
    /// The LSP CompletionItemKind.
    int kind;
    std::string detail;
    /// Markdown.
    std::string documentation;
    std::string insertText;
    bool isSnippet = false;
    /// Items are sorted by rank, then by quality, then by label.
    int rank = 0;
    int quality = 0;
};

/// Completes code. As Emojicode is written with emoji, the word typed before the cursor is not the start of what is
/// inserted, but a description of it: a variable name, a word in the name of an emoji ("grapes" for 🍇), a word in
/// the documentation of a method or type, or a keyword like "class" or "if".
class Completer {
public:
    /// @param analysis The analysis of the file's package, or nullptr if there is none.
    Completer(const Analysis *analysis, std::string path, const SourceText &source, bool snippets)
        : analysis_(analysis), path_(std::move(path)), source_(source), snippets_(snippets) {}

    /// Returns the start of the word before @p offset, which completion replaces, or @p offset if there is none.
    size_t wordStart(size_t offset) const;
    /// Returns whether completion makes sense at @p offset, i.e. it is not in a string or comment.
    bool canComplete(size_t offset) const;

    /// Returns the best items for the word from @p start to @p offset.
    std::vector<CompletionItem> complete(size_t start, size_t offset, size_t limit) const;

private:
    void addVariables(size_t offset, const std::string &word, std::vector<CompletionItem> *items) const;
    void addKeywords(const std::string &word, std::vector<CompletionItem> *items) const;
    void addTypesAndMethods(const std::string &word, std::vector<CompletionItem> *items) const;
    void addEmoji(const std::string &word, std::vector<CompletionItem> *items) const;

    const Analysis *analysis_;
    std::string path_;
    const SourceText &source_;
    bool snippets_;
};

}  // namespace EmojicodeLanguageServer

#endif /* Completion_hpp */
