//
//  Tokens.hpp
//  EmojicodeLanguageServer
//

#ifndef Tokens_hpp
#define Tokens_hpp

#include "Lex/Token.hpp"
#include "Positions.hpp"
#include <string>
#include <vector>

namespace EmojicodeLanguageServer {

/// A token and where it is in the text, as code point indices.
struct TokenSpan {
    size_t start;
    /// The index after the last code point of the token.
    size_t end;
    EmojicodeCompiler::TokenType type;
    std::u32string value;
    /// Whether the token is a string or comment that is not closed, e.g. while it is typed. It runs to the end of the
    /// text.
    bool unterminated = false;
};

/// Whether the lexer skips @p c between tokens. U+FE0F is whitespace to the lexer, but it belongs to the emoji
/// before it, so it is kept in the token.
bool isSkipped(char32_t c);

/// Lexes @p text with the compiler's lexer. A string that the lexer rejects, e.g. because of an invalid escape
/// sequence, is a string token without a value. If the text contains another invalid token, the tokens before it
/// are returned. Line breaks are left out.
std::vector<TokenSpan> lex(const std::u32string &text);

/// The text of a file with its lines and tokens.
struct SourceText {
    explicit SourceText(std::u32string text) : text(std::move(text)), lines(this->text), tokens(lex(this->text)) {}
    SourceText(const SourceText &) = delete;

    const std::u32string text;
    const LineIndex lines;
    const std::vector<TokenSpan> tokens;
};

/// Returns the token that starts at @p offset, or nullptr.
const TokenSpan* tokenStartingAt(const std::vector<TokenSpan> &tokens, size_t offset);
/// Returns the token that contains @p offset, or the one that ends right at it, or nullptr.
const TokenSpan* tokenAt(const std::vector<TokenSpan> &tokens, size_t offset);

}  // namespace EmojicodeLanguageServer

#endif /* Tokens_hpp */
