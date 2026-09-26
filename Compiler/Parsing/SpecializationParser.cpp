//
//  SpecializationParser.cpp
//  EmojicodeCompiler
//

#include "SpecializationParser.hpp"
#include "AST/ASTStatements.hpp"
#include "CompilerError.hpp"
#include "Emojis.h"
#include "FunctionParser.hpp"
#include "Functions/Function.hpp"
#include "Functions/Initializer.hpp"
#include "Lex/Lexer.hpp"
#include "Lex/TokenStream.hpp"

namespace EmojicodeCompiler {

void SpecializationParser::parse(Function *generic, Function *specialization) {
    auto &position = generic->position();
    // Minimal mode, as a normal lexer records the lines and comments of the file again.
    TokenStream stream(Lexer(position.file, true));
    SpecializationParser parser(generic->package(), stream);
    parser.skipToName(position);
    parser.parseFunction(specialization);
}

void SpecializationParser::skipToName(const SourcePosition &position) {
    while (stream_.hasMoreTokens()) {
        auto token = stream_.consumeToken();
        if (token.position().line == position.line && token.position().character == position.character) {
            // An initializer is declared at 🆕, which its name can follow, and a method at the token of its mood,
            // which its name follows, unless it is an operator.
            if (token.type() == TokenType::New) {
                parseInitializerName();
            }
            else if (token.type() != TokenType::Operator) {
                stream_.consumeToken();
            }
            return;
        }
    }
    throw CompilerError(position, "Could not find the source of the generic function to specialize.");
}

void SpecializationParser::skipGenericParameters() {
    if (!stream_.consumeTokenIf(TokenType::Generic)) {
        return;
    }
    size_t depth = 1;
    while (depth > 0) {
        auto token = stream_.consumeToken();
        if (token.type() == TokenType::Generic) {
            depth++;
        }
        else if (token.type() == TokenType::Identifier && token.value()[0] == E_AUBERGINE) {
            depth--;
        }
    }
}

void SpecializationParser::parseFunction(Function *specialization) {
    auto initializer = dynamic_cast<Initializer *>(specialization) != nullptr;
    skipGenericParameters();
    parseParameters(specialization, initializer);
    if (!initializer) {
        parseReturnType(specialization);
    }
    parseErrorType(specialization);
    stream_.consumeToken(TokenType::BlockBegin);
    specialization->setAst(FunctionParser(package_, stream_).parse());
}

}  // namespace EmojicodeCompiler
