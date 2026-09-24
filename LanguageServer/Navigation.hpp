//
//  Navigation.hpp
//  EmojicodeLanguageServer
//

#ifndef Navigation_hpp
#define Navigation_hpp

#include "Checker.hpp"
#include "Index.hpp"
#include "Positions.hpp"
#include "Tokens.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace EmojicodeCompiler {
class Function;
class TypeDefinition;
struct InstanceVariableDeclaration;
}

namespace EmojicodeLanguageServer {

/// What a token in the code refers to.
struct Symbol {
    enum class Kind {
        /// An expression that is none of the kinds below, e.g. a literal.
        Expression,
        Method,
        TypeMethod,
        Initializer,
        Variable,
        InstanceVariable,
        Class,
        ValueType,
        Enum,
        Protocol,
        /// A type that is none of the above, e.g. a generic type variable.
        OtherType,
    };

    Kind kind;
    /// Markdown describing the symbol.
    std::string hover;
    /// Where the symbol was declared, if known.
    std::optional<Location> declaration;
    /// Whether this token is the declaration itself.
    bool isDeclaration = false;
    /// Whether the symbol is a constant variable.
    bool isConstant = false;
    /// Whether the symbol is defined in another package.
    bool isImported = false;
};

/// Returns the instance variables of @p definition, including inherited ones, each with the type that declares it.
/// Its type must be printed in the context of that type.
std::vector<std::pair<const EmojicodeCompiler::InstanceVariableDeclaration *, EmojicodeCompiler::TypeDefinition *>>
instanceVariables(EmojicodeCompiler::TypeDefinition *definition);

/// A declaration in the outline of a file.
struct OutlineEntry {
    std::u32string name;
    Symbol::Kind kind;
    Location location;
    std::vector<OutlineEntry> children;
};

/// Returns the text of the file at a canonical path.
using SourceProvider = std::function<const SourceText& (const std::string &path)>;

/// Finds out what the tokens of a file refer to, using the analysis of the package that contains the file.
class Navigator {
public:
    /// @param path The canonical path of the file.
    Navigator(const Analysis &analysis, std::string path, SourceProvider sources);

    /// Returns what the token at the code point @p offset of the file refers to, and the token.
    std::optional<std::pair<Symbol, const TokenSpan*>> symbolAt(size_t offset) const;
    /// Returns what @p token, a token of the file, refers to.
    std::optional<Symbol> symbol(const TokenSpan &token) const;

    const SourceText& source() const { return source_; }
    /// The types declared in the file with their members.
    const std::vector<OutlineEntry>& outline() const { return outline_; }

private:
    std::optional<Symbol> nodeSymbol(const IndexedNode &node, const TokenSpan &token) const;
    std::optional<Symbol> variableDeclaration(const TokenSpan &token, size_t line, size_t character) const;
    Symbol functionSymbol(EmojicodeCompiler::Function *function) const;
    Symbol typeSymbol(const EmojicodeCompiler::Type &type, const EmojicodeCompiler::TypeContext &context) const;
    Symbol instanceVariableSymbol(const EmojicodeCompiler::InstanceVariableDeclaration &variable,
                                  EmojicodeCompiler::TypeDefinition *owner) const;

    /// Returns the location of the token @p name at or after @p position on its line. Declarations are at the
    /// start of the code that declares them, e.g. a method at its ❗️, so this finds the name itself.
    std::optional<Location> nameLocation(const EmojicodeCompiler::SourcePosition &position,
                                         const std::u32string &name) const;
    std::optional<Location> nameLocation(const Location &location, const std::u32string &name) const;
    /// Records the declarations of the types, methods and instance variables of the analysed package.
    void collectDeclarations();

    std::string typeString(const EmojicodeCompiler::Type &type, const EmojicodeCompiler::TypeContext &context) const;

    const Analysis &analysis_;
    std::string path_;
    SourceProvider sources_;
    const SourceText &source_;
    /// The symbols declared in this file by the code point offset of their name.
    std::map<size_t, Symbol> declarations_;
    std::vector<OutlineEntry> outline_;
};

}  // namespace EmojicodeLanguageServer

#endif /* Navigation_hpp */
