//
//  Tokens.hpp
//  EmojicodeLanguageServer
//

#ifndef Tokens_hpp
#define Tokens_hpp

#include "Lex/Token.hpp"
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
};

/// Lexes @p text with the compiler's lexer. If the text contains an invalid token, the tokens before it are
/// returned. Line breaks are left out.
std::vector<TokenSpan> lex(const std::u32string &text);

/// Returns the token that starts at @p offset, or nullptr.
const TokenSpan* tokenStartingAt(const std::vector<TokenSpan> &tokens, size_t offset);
/// Returns the token that contains @p offset, or the one that ends right at it, or nullptr.
const TokenSpan* tokenAt(const std::vector<TokenSpan> &tokens, size_t offset);

}  // namespace EmojicodeLanguageServer

#endif /* Tokens_hpp */
