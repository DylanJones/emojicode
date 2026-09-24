//
//  TokenStream.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 26/04/16.
//  Copyright © 2016 Theo Weidmann. All rights reserved.
//

#include "TokenStream.hpp"
#include "CompilerError.hpp"

namespace EmojicodeCompiler {

Token TokenStream::consumeToken() {
    if (!hasMoreTokens()) {
        throw CompilerError(lexer_.position(), "Unexpected end of program.");
    }
    return advanceLexer();
}

Token TokenStream::consumeToken(TokenType type) {
    if (!hasMoreTokens()) {
        throw CompilerError(lexer_.position(), "Unexpected end of program.");
    }
    if (nextToken().type() != type) {
        throw CompilerError(nextToken().position(), "Expected ", Token::stringNameForType(type),
                            " but instead found ", nextToken().stringName(), "(", utf8(nextToken().value()), ").");
    }
    return advanceLexer();
}

bool TokenStream::consumeTokenIf(char32_t c, TokenType type) {
    if (nextTokenIs(c, type)) {
        advanceLexer();
        return true;
    }
    return false;
}

bool TokenStream::consumeTokenIf(TokenType type) {
    if (nextTokenIs(type)) {
        advanceLexer();
        return true;
    }
    return false;
}

Token TokenStream::advanceLexer() {
    auto temp = std::move(nextToken_);
    ReadToken next = hasAfterNext_ ? std::move(afterNext_) : read();
    hasAfterNext_ = false;
    skippedBlankLine_ = next.skippedBlankLine;
    if (next.exists) {
        index_ = next.index;
        nextToken_ = std::move(next.token);
    }
    else {
        moreTokens_ = false;
    }
    return temp;
}

TokenStream::ReadToken TokenStream::read() {
    ReadToken result;
    while (true) {
        if (!lexer_.continues()) {
            result.exists = false;
            return result;
        }
        result.index = lexer_.index();
        result.token = lexer_.lex();
        if (result.token.type() == TokenType::BlankLine) {
            result.skippedBlankLine = true;
        }
        else if (result.token.type() == TokenType::SinglelineComment ||
                 result.token.type() == TokenType::MultilineComment) {
            result.token.position().file->addComment(std::move(result.token));
        }
        else if (result.token.type() != TokenType::LineBreak) {
            return result;
        }
    }
}

const Token* TokenStream::tokenAfterNext() {
    if (!hasMoreTokens()) {
        return nullptr;
    }
    if (!hasAfterNext_) {
        afterNext_ = read();
        hasAfterNext_ = true;
    }
    return afterNext_.exists ? &afterNext_.token : nullptr;
}

}  // namespace EmojicodeCompiler
