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

/// Returns the index after the 🔤 that ends the string that the lexer rejected at @p start, or text.size() if there
/// is none.
static size_t stringEnd(const std::u32string &text, size_t start) {
    for (auto i = start + 1; i < text.size(); i++) {
        if (text[i] == U'❌') {
            i++;
        }
        else if (text[i] == U'🔤') {
            return i + 1;
        }
    }
    return text.size();
}

std::vector<TokenSpan> lex(const std::u32string &text) {
    std::vector<TokenSpan> tokens;
    // The lexer is started again after a string it rejected, with the text after the string.
    size_t offset = 0;
    while (offset < text.size()) {
        EmojicodeCompiler::SourceFile file(text.substr(offset), "");
        size_t start = offset;
        try {
            EmojicodeCompiler::Lexer lexer(&file, false);
            while (lexer.continues()) {
                start = offset + lexer.index();
                auto token = lexer.lex();
                if (!tokens.empty()) {
                    tokens.back().end = start;
                }
                tokens.push_back(TokenSpan{start, text.size(), token.type(), token.value()});
            }
            break;
        }
        catch (EmojicodeCompiler::CompilerError &) {
            // The token that failed starts at start, so the one before ends there.
            if (!tokens.empty()) {
                tokens.back().end = std::min(tokens.back().end, start);
            }
            if (start >= text.size()) {
                break;
            }
            auto first = text[start];
            if (first == U'🔤' || first == U'🧲') {
                auto end = stringEnd(text, start);
                tokens.push_back(TokenSpan{start, end, TokenType::String, U"", end == text.size()});
                offset = end;
            }
            else {
                // An unterminated comment, e.g. while it is being typed, extends to the end of the text.
                if (first == U'💭' || first == U'📗' || first == U'📘') {
                    tokens.push_back(TokenSpan{start, text.size(), TokenType::MultilineComment, U"", true});
                }
                break;
            }
        }
    }

    // Each token was ended where the next one starts, so the whitespace in between is removed.
    for (auto &token : tokens) {
        while (!token.unterminated && token.end > token.start + 1 && isSkipped(text[token.end - 1])) {
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
