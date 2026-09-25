//
//  Navigation.cpp
//  EmojicodeLanguageServer
//

#include "Navigation.hpp"
#include <algorithm>
#include "AST/ASTInitialization.hpp"
#include "AST/ASTMethod.hpp"
#include "AST/ASTSuper.hpp"
#include "AST/ASTType.hpp"
#include "AST/ASTVariables.hpp"
#include "Functions/Function.hpp"
#include "Functions/FunctionType.hpp"
#include "Functions/Initializer.hpp"
#include "Lex/SourceManager.hpp"
#include "Package/RecordingPackage.hpp"
#include "Prettyprint/PrettyPrinter.hpp"
#include "Types/Class.hpp"
#include "Types/Protocol.hpp"
#include "Types/TypeDefinition.hpp"
#include "Types/ValueType.hpp"

namespace EmojicodeLanguageServer {

using namespace EmojicodeCompiler;

/// Removes leading and trailing whitespace from documentation.
static std::string documentation(const std::u32string &doc) {
    auto begin = doc.find_first_not_of(U" \t\n");
    if (begin == std::u32string::npos) {
        return "";
    }
    auto end = doc.find_last_not_of(U" \t\n");
    return utf8(doc.substr(begin, end - begin + 1));
}

static std::string codeBlock(const std::string &code) {
    auto end = code.find_last_not_of(" \n");
    return "```emojicode\n" + code.substr(0, end == std::string::npos ? 0 : end + 1) + "\n```";
}

static std::string withDocumentation(std::string hover, const std::u32string &doc) {
    auto text = documentation(doc);
    if (!text.empty()) {
        hover += "\n\n---\n\n" + text;
    }
    return hover;
}

/// Removes U+FE0F, which the lexer ignores in names.
static std::u32string withoutVariationSelectors(std::u32string name) {
    name.erase(std::remove(name.begin(), name.end(), U'️'), name.end());
    return name;
}

/// Returns the superclass of @p klass, or nullptr if it has none or it is unknown because of an error.
static Class* analysedSuperclass(Class *klass) {
    auto type = klass->superType();
    if (type == nullptr || !type->wasAnalysed() || type->type().type() != TypeType::Class) {
        return nullptr;
    }
    return type->type().klass();
}

std::vector<std::pair<const InstanceVariableDeclaration *, TypeDefinition *>>
instanceVariables(TypeDefinition *definition) {
    // The chain from the root class down to definition. The compiler reports cyclic inheritance, but the superclass
    // links stay cyclic after the error, so the walk stops at the first class it has seen before.
    std::vector<TypeDefinition *> chain{definition};
    auto klass = dynamic_cast<Class *>(definition);
    while (klass != nullptr && (klass = analysedSuperclass(klass)) != nullptr &&
           std::find(chain.begin(), chain.end(), klass) == chain.end()) {
        chain.push_back(klass);
    }
    std::vector<std::pair<const InstanceVariableDeclaration *, TypeDefinition *>> result;
    for (auto it = chain.rbegin(); it != chain.rend(); it++) {
        auto inherited = result.size();
        for (auto &variable : (*it)->instanceVariables()) {
            // A class's list contains copies of the instance variables of its superclass once it inherited them.
            auto copy = std::find_if(result.begin(), result.begin() + inherited, [&](auto &pair) {
                auto &p = pair.first->position;
                return pair.first->name == variable.name && p.file == variable.position.file &&
                       p.line == variable.position.line && p.character == variable.position.character;
            });
            if (copy == result.begin() + inherited) {
                result.emplace_back(&variable, *it);
            }
        }
    }
    return result;
}

Navigator::Navigator(const Analysis &analysis, std::string path, SourceProvider sources, bool describe)
    : analysis_(analysis), path_(std::move(path)), sources_(std::move(sources)), source_(sources_(path_)),
      describe_(describe) {
    if (analysis_.compiler == nullptr || analysis_.index == nullptr) {
        return;
    }
    collectDeclarations();
    for (auto &variable : analysis_.index->variables()) {
        if (variable.declaration.path == path_) {
            variablesByLine_.emplace(variable.declaration.line, &variable);
        }
    }
}

bool Navigator::isInFile(const SourcePosition &position) const {
    return !position.isUnknown() && canonicalPath(position.file->path()) == path_;
}

std::string Navigator::typeString(const Type &type, const TypeContext &context) const {
    return type.unboxed().toString(context, analysis_.compiler->mainPackage());
}

std::optional<Location> Navigator::nameLocation(const Location &location, const std::u32string &name) const {
    auto &source = sources_(location.path);
    auto start = source.lines.compilerOffset(location.line, location.character);
    auto end = source.lines.compilerLineEnd(location.line);
    auto wanted = withoutVariationSelectors(name);
    auto it = std::lower_bound(source.tokens.begin(), source.tokens.end(), start,
                               [](const TokenSpan &token, size_t offset) { return token.start < offset; });
    for (; it != source.tokens.end() && it->start < end; ++it) {
        if (withoutVariationSelectors(it->value) == wanted) {
            auto position = source.lines.compilerPosition(it->start);
            return Location{location.path, position.first, position.second};
        }
    }
    return location;
}

std::optional<Location> Navigator::nameLocation(const SourcePosition &position, const std::u32string &name) const {
    if (position.isUnknown()) {
        return std::nullopt;
    }
    return nameLocation(Location{canonicalPath(position.file->path()), position.line, position.character}, name);
}

Symbol Navigator::functionSymbol(Function *function) const {
    Symbol symbol{Symbol::Kind::Method};
    if (dynamic_cast<Initializer *>(function) != nullptr) {
        symbol.kind = Symbol::Kind::Initializer;
    }
    else if (isTypeMethod(function)) {
        symbol.kind = Symbol::Kind::TypeMethod;
    }
    if (describe_) {
        auto declaration = PrettyPrinter(analysis_.compiler->mainPackage()).declaration(function);
        std::string hover = codeBlock(declaration);
        if (function->owner() != nullptr) {
            hover += "\n\nin `" + typeString(function->owner()->type(), TypeContext()) + "`";
        }
        symbol.hover = withDocumentation(hover, function->documentation());
    }
    symbol.declaration = nameLocation(function->position(), function->name());
    symbol.isImported = function->package() != analysis_.compiler->mainPackage();
    return symbol;
}

Symbol Navigator::typeSymbol(const Type &type, const TypeContext &context) const {
    Symbol symbol{Symbol::Kind::OtherType};
    auto unboxed = type.unboxed();
    const char *keyword = "";
    switch (unboxed.unboxedType()) {
        case TypeType::Class:
            symbol.kind = Symbol::Kind::Class;
            keyword = "🐇 ";
            break;
        case TypeType::ValueType:
            symbol.kind = Symbol::Kind::ValueType;
            keyword = "🕊 ";
            break;
        case TypeType::Enum:
            symbol.kind = Symbol::Kind::Enum;
            keyword = "🔘 ";
            break;
        case TypeType::Protocol:
            symbol.kind = Symbol::Kind::Protocol;
            keyword = "🐊 ";
            break;
        default:
            break;
    }
    if (describe_) {
        symbol.hover = codeBlock(keyword + typeString(unboxed, context));
    }
    if (symbol.kind != Symbol::Kind::OtherType) {
        auto definition = unboxed.typeDefinition();
        if (describe_) {
            symbol.hover = withDocumentation(symbol.hover, definition->documentation());
        }
        symbol.declaration = nameLocation(definition->position(), definition->name());
        symbol.isImported = definition->package() != analysis_.compiler->mainPackage();
    }
    return symbol;
}

Symbol Navigator::instanceVariableSymbol(const InstanceVariableDeclaration &variable, TypeDefinition *owner) const {
    Symbol symbol{Symbol::Kind::InstanceVariable};
    if (describe_) {
        auto type = variable.type->wasAnalysed() ? typeString(variable.type->type(), TypeContext(owner->type())) : "";
        symbol.hover = codeBlock("🖍🆕 " + utf8(variable.name) + " " + type) + "\n\nin `" +
                       typeString(owner->type(), TypeContext()) + "`";
    }
    symbol.declaration = nameLocation(variable.position, variable.name);
    return symbol;
}

void Navigator::collectDeclarations() {
    auto add = [&](const std::u32string &name, const std::optional<Location> &location, Symbol symbol,
                   std::vector<OutlineEntry> *outline) {
        if (!location || location->path != path_) {
            return;
        }
        outline->push_back(OutlineEntry{name, symbol.kind, *location, {}});
        symbol.isDeclaration = true;
        symbol.declaration = location;
        declarations_.emplace(source_.lines.compilerOffset(location->line, location->character), std::move(symbol));
    };
    auto addFunction = [&](Function *function, std::vector<OutlineEntry> *outline) {
        if (function->isThunk() || !isInFile(function->position())) {
            return;
        }
        add(function->name(), nameLocation(function->position(), function->name()), functionSymbol(function),
            outline);
    };
    auto addTypeDefinition = [&](TypeDefinition *definition) {
        if (!isInFile(definition->position())) {
            return;
        }
        auto size = outline_.size();
        add(definition->name(), nameLocation(definition->position(), definition->name()),
            typeSymbol(definition->type(), TypeContext()), &outline_);
        if (outline_.size() == size) {
            return;  // Declared in another file.
        }
        auto members = &outline_.back().children;
        for (auto &pair : instanceVariables(definition)) {
            if (pair.second == definition && isInFile(pair.first->position)) {
                auto &variable = *pair.first;
                add(variable.name, nameLocation(variable.position, variable.name),
                    instanceVariableSymbol(variable, definition), members);
            }
        }
        for (auto function : definition->inits().list()) addFunction(function, members);
        for (auto function : definition->methods().list()) addFunction(function, members);
        for (auto function : definition->typeMethods().list()) addFunction(function, members);
        std::sort(members->begin(), members->end(), [](const OutlineEntry &a, const OutlineEntry &b) {
            return std::make_pair(a.location.line, a.location.character) <
                   std::make_pair(b.location.line, b.location.character);
        });
    };
    auto package = analysis_.compiler->mainPackage();
    for (auto &type : package->classes()) addTypeDefinition(type.get());
    for (auto &type : package->valueTypes()) addTypeDefinition(type.get());
    for (auto &type : package->protocols()) addTypeDefinition(type.get());
}

/// Returns whether @p token names @p function. Calls that the compiler adds, like the call of the iterator method
/// for 🔂, are at a token that does not.
static bool tokenNames(const TokenSpan &token, Function *function) {
    if (token.type == TokenType::New) {
        return dynamic_cast<Initializer *>(function) != nullptr;
    }
    return withoutVariationSelectors(token.value) == withoutVariationSelectors(function->name());
}

std::optional<Symbol> Navigator::nodeSymbol(const IndexedNode &node, const TokenSpan &token) const {
    if (node.expr == nullptr) {
        return typeSymbol(node.type, node.context);
    }
    auto expr = node.expr.get();
    Function *function = nullptr;
    if (auto method = dynamic_cast<ASTMethodable *>(expr)) {
        function = method->method();
    }
    else if (auto init = dynamic_cast<ASTInitialization *>(expr)) {
        function = init->initializer();
    }
    else if (auto super = dynamic_cast<ASTSuper *>(expr)) {
        function = super->function();
        // The call is at ⤴️, which is followed by the name of the function.
        if (function != nullptr && token.type == TokenType::Super) {
            return functionSymbol(function);
        }
    }
    if (function != nullptr) {
        if (!tokenNames(token, function)) {
            return std::nullopt;
        }
        return functionSymbol(function);
    }
    if (auto variable = dynamic_cast<ASTGetVariable *>(expr)) {
        // Variables the compiler adds, like the iterator of 🔂, are at a token that is not their name.
        if (token.type != TokenType::Variable || token.value != variable->name()) {
            return std::nullopt;
        }
        Symbol symbol{variable->inInstanceScope() ? Symbol::Kind::InstanceVariable : Symbol::Kind::Variable};
        if (describe_) {
            symbol.hover = codeBlock(utf8(variable->name()) + " " + typeString(expr->expressionType(), node.context));
        }
        symbol.declaration = nameLocation(variable->declarationPosition(), variable->name());
        return symbol;
    }
    if (expr->expressionType().type() == TypeType::Invalid) {
        return std::nullopt;
    }
    Symbol symbol{Symbol::Kind::Expression};
    if (describe_) {
        symbol.hover = codeBlock(typeString(expr->expressionType(), node.context));
    }
    return symbol;
}

std::optional<Symbol> Navigator::variableDeclaration(const TokenSpan &token, size_t line, size_t character) const {
    // A variable is declared at the start of the statement that declares it, or at its function for parameters,
    // which is before its name on the same line. The closest such declaration with the name is taken.
    const IndexedVariable *best = nullptr;
    auto range = variablesByLine_.equal_range(line);
    for (auto it = range.first; it != range.second; ++it) {
        auto &variable = *it->second;
        auto &d = variable.declaration;
        if (variable.name == token.value && d.character <= character &&
            (best == nullptr || best->declaration.character < d.character)) {
            best = &variable;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    Symbol symbol{Symbol::Kind::Variable};
    if (describe_) {
        symbol.hover = codeBlock((best->constant ? "" : "🖍 ") + utf8(best->name) + " " +
                                 typeString(best->type, best->context));
    }
    symbol.declaration = Location{path_, line, character};
    symbol.isDeclaration = true;
    symbol.isConstant = best->constant;
    return symbol;
}

std::optional<Symbol> Navigator::symbol(const TokenSpan &token) const {
    if (analysis_.index == nullptr || analysis_.compiler == nullptr) {
        return std::nullopt;
    }
    auto declaration = declarations_.find(token.start);
    if (declaration != declarations_.end()) {
        return declaration->second;
    }

    auto position = source_.lines.compilerPosition(token.start);
    auto line = position.first;
    auto character = position.second;
    std::optional<Symbol> fallback;
    auto nodes = analysis_.index->nodesAt(path_, line, character);
    // A ⤴️ call is at the ⤴️ before the name of the function.
    auto it = std::lower_bound(source_.tokens.begin(), source_.tokens.end(), token.start,
                               [](const TokenSpan &t, size_t offset) { return t.start < offset; });
    if (it != source_.tokens.begin() && it != source_.tokens.end() && it->start == token.start &&
        std::prev(it)->type == TokenType::Super) {
        auto super = source_.lines.compilerPosition(std::prev(it)->start);
        for (auto node : analysis_.index->nodesAt(path_, super.first, super.second)) {
            if (node->expr != nullptr && dynamic_cast<ASTSuper *>(node->expr.get()) != nullptr) {
                nodes.push_back(node);
            }
        }
    }
    for (auto node : nodes) {
        auto symbol = nodeSymbol(*node, token);
        if (symbol && symbol->kind != Symbol::Kind::Expression) {
            return symbol;
        }
        if (symbol && !fallback) {
            fallback = symbol;
        }
    }
    if (token.type == TokenType::Variable) {
        if (auto declaration = variableDeclaration(token, line, character)) {
            return declaration;
        }
    }
    return fallback;
}

std::optional<std::pair<Symbol, const TokenSpan*>> Navigator::symbolAt(size_t offset) const {
    auto token = tokenAt(source_.tokens, offset);
    if (token == nullptr) {
        return std::nullopt;
    }
    if (auto symbol = this->symbol(*token)) {
        return std::make_pair(*symbol, token);
    }
    return std::nullopt;
}

}  // namespace EmojicodeLanguageServer
