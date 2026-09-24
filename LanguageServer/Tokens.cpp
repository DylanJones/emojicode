//
//  Tokens.cpp
//  EmojicodeLanguageServer
//

#include "Tokens.hpp"
#include "CompilerError.hpp"
#include "Lex/Lexer.hpp"
#include "Lex/SourceManager.hpp"
#include <algorithm>

namespace EmojicodeLanguageServer {

using EmojicodeCompiler::TokenType;

/// Whether the lexer skips @p c between tokens. U+FE0F is whitespace to the lexer, but it belongs to the emoji
/// before it, so it is kept in the token.
static bool isSkipped(char32_t c) {
    return c != 0xFE0F && (c == ' ' || (0x9 <= c && c <= 0xD) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
                           (0x2000 <= c && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
                           c == 0x205F || c == 0x3000);
}

std::vector<TokenSpan> lex(const std::u32string &text) {
    std::vector<TokenSpan> tokens;
    EmojicodeCompiler::SourceFile file(text, "");
    try {
        EmojicodeCompiler::Lexer lexer(&file, false);
        while (lexer.continues()) {
            auto start = lexer.index();
            auto token = lexer.lex();
            if (!tokens.empty()) {
                tokens.back().end = start;
            }
            tokens.push_back(TokenSpan{start, text.size(), token.type(), token.value()});
        }
    }
    catch (EmojicodeCompiler::CompilerError &) {}

    // Each token was ended where the next one starts, so the whitespace in between is removed.
    for (auto &token : tokens) {
        while (token.end > token.start + 1 && isSkipped(text[token.end - 1])) {
            token.end--;
        }
    }
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const TokenSpan &token) {
        return token.type == TokenType::LineBreak || token.type == TokenType::BlankLine;
    }), tokens.end());
    return tokens;
}

const TokenSpan* tokenStartingAt(const std::vector<TokenSpan> &tokens, size_t offset) {
    auto it = std::lower_bound(tokens.begin(), tokens.end(), offset, [](const TokenSpan &token, size_t offset) {
        return token.start < offset;
    });
    return it != tokens.end() && it->start == offset ? &*it : nullptr;
}

const TokenSpan* tokenAt(const std::vector<TokenSpan> &tokens, size_t offset) {
    auto it = std::upper_bound(tokens.begin(), tokens.end(), offset, [](size_t offset, const TokenSpan &token) {
        return offset < token.start;
    });
    if (it == tokens.begin()) {
        return nullptr;
    }
    --it;
    return offset <= it->end ? &*it : nullptr;
}

}  // namespace EmojicodeLanguageServer
