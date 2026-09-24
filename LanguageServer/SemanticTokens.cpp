//
//  SemanticTokens.cpp
//  EmojicodeLanguageServer
//

#include "SemanticTokens.hpp"
#include <optional>

namespace EmojicodeLanguageServer {

using EmojicodeCompiler::TokenType;

namespace {

enum Type : uint32_t {
    Class, Enum, Interface, Struct, TypeParameter, Variable, Property, Method, Keyword, Modifier, Comment, String,
    Number, Operator, Decorator,
};

enum Modifier : uint32_t {
    Declaration = 1 << 0,
    Readonly = 1 << 1,
    Static = 1 << 2,
    DefaultLibrary = 1 << 3,
    Documentation = 1 << 4,
};

struct Classification {
    uint32_t type;
    uint32_t modifiers = 0;
};

/// Emoji that the parser gives a meaning by the first code point of an identifier.
std::optional<Classification> contextualKeyword(char32_t c) {
    switch (c) {
        case U'🏁': case U'📦': case U'📜': case U'🔗': case U'🙅': case U'🤷': case U'♻': case U'↪':
            return Classification{Keyword};
        case U'🌍': case U'🔏': case U'✒': case U'🥯': case U'⚠': case U'🔑': case U'🔓': case U'🔒': case U'🔐':
        case U'📻': case U'🍼':
            return Classification{Modifier};
        case U'🍺': case U'🔺': case U'🔲': case U'⚖': case U'🏮': case U'🍬': case U'✴': case U'⬛': case U'🚧':
        case U'🍱':
            return Classification{Operator};
        case U'◼': case U'⚪': case U'🔵': case U'⚫':
            return Classification{TypeParameter, DefaultLibrary};
        default:
            return std::nullopt;
    }
}

std::optional<Classification> classify(const Symbol &symbol) {
    uint32_t modifiers = (symbol.isDeclaration ? Declaration : 0) | (symbol.isConstant ? Readonly : 0) |
                         (symbol.isImported ? DefaultLibrary : 0);
    switch (symbol.kind) {
        case Symbol::Kind::Method:
        case Symbol::Kind::Initializer:
            return Classification{Method, modifiers};
        case Symbol::Kind::TypeMethod:
            return Classification{Method, modifiers | Static};
        case Symbol::Kind::Variable:
            return Classification{Variable, modifiers};
        case Symbol::Kind::InstanceVariable:
            return Classification{Property, modifiers};
        case Symbol::Kind::Class:
            return Classification{Class, modifiers};
        case Symbol::Kind::ValueType:
            return Classification{Struct, modifiers};
        case Symbol::Kind::Enum:
            return Classification{Enum, modifiers};
        case Symbol::Kind::Protocol:
            return Classification{Interface, modifiers};
        case Symbol::Kind::OtherType:
            return Classification{TypeParameter, modifiers};
        case Symbol::Kind::Expression:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Classification> classify(const TokenSpan &token, const Navigator &navigator) {
    switch (token.type) {
        case TokenType::SinglelineComment:
        case TokenType::MultilineComment:
            return Classification{Comment};
        case TokenType::DocumentationComment:
        case TokenType::PackageDocumentationComment:
            return Classification{Comment, Documentation};
        case TokenType::String:
        case TokenType::BeginInterpolation:
        case TokenType::MiddleInterpolation:
        case TokenType::EndInterpolation:
        case TokenType::Symbol:
            return Classification{String};
        case TokenType::Integer:
        case TokenType::Double:
            return Classification{Number};
        case TokenType::Decorator:
            return Classification{Decorator};
        case TokenType::Operator:
        case TokenType::Call:
        case TokenType::RightProductionOperator:
        case TokenType::LeftProductionOperator:
        case TokenType::SelectionOperator:
            return Classification{Operator};
        case TokenType::BooleanTrue:
        case TokenType::BooleanFalse:
        case TokenType::NoValue:
        case TokenType::Return:
        case TokenType::RepeatWhile:
        case TokenType::ForIn:
        case TokenType::Error:
        case TokenType::If:
        case TokenType::ElseIf:
        case TokenType::Else:
        case TokenType::ErrorHandler:
        case TokenType::New:
        case TokenType::This:
        case TokenType::Unsafe:
        case TokenType::Super:
        case TokenType::Generic:
        case TokenType::Class:
        case TokenType::Enumeration:
        case TokenType::ValueType:
        case TokenType::Protocol:
        case TokenType::CollectionLiteral:
            return Classification{Keyword};
        case TokenType::Mutable:
            return Classification{Modifier};
        case TokenType::Identifier:
            if (auto symbol = navigator.symbol(token)) {
                if (auto classification = classify(*symbol)) {
                    return classification;
                }
            }
            if (!token.value.empty()) {
                return contextualKeyword(token.value.front());
            }
            return std::nullopt;
        case TokenType::Variable:
            if (auto symbol = navigator.symbol(token)) {
                if (auto classification = classify(*symbol)) {
                    return classification;
                }
            }
            return Classification{Variable};
        default:
            // Punctuation like 🍇 🍉 ❗️ is left to the client's syntax highlighting.
            return std::nullopt;
    }
}

}  // namespace

const std::vector<const char *> kSemanticTokenTypes = {
    "class", "enum", "interface", "struct", "typeParameter", "variable", "property", "method", "keyword", "modifier",
    "comment", "string", "number", "operator", "decorator",
};

const std::vector<const char *> kSemanticTokenModifiers = {
    "declaration", "readonly", "static", "defaultLibrary", "documentation",
};

std::vector<uint32_t> semanticTokens(const Navigator &navigator, PositionEncoding encoding) {
    auto &source = navigator.source();
    std::vector<uint32_t> data;
    size_t previousLine = 0;
    size_t previousStart = 0;
    auto emit = [&](size_t line, size_t startCharacter, size_t endCharacter, const Classification &c) {
        auto start = source.lines.toClient(line, startCharacter, encoding).character;
        auto end = source.lines.toClient(line, endCharacter, encoding).character;
        if (end <= start) {
            return;
        }
        data.push_back(static_cast<uint32_t>(line - previousLine));
        data.push_back(static_cast<uint32_t>(line == previousLine ? start - previousStart : start));
        data.push_back(static_cast<uint32_t>(end - start));
        data.push_back(c.type);
        data.push_back(c.modifiers);
        previousLine = line;
        previousStart = start;
    };

    for (auto &token : source.tokens) {
        auto classification = classify(token, navigator);
        if (!classification) {
            continue;
        }
        // Tokens cannot span lines, so comments and strings with line breaks are split.
        auto start = source.lines.lineAndCharacter(token.start);
        auto end = source.lines.lineAndCharacter(token.end);
        for (auto line = start.first; line <= end.first; line++) {
            auto from = line == start.first ? start.second : 0;
            auto to = line == end.first ? end.second : source.lines.line(line).size();
            emit(line, from, to, *classification);
        }
    }
    return data;
}

}  // namespace EmojicodeLanguageServer
