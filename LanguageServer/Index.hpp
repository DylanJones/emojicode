//
//  Index.hpp
//  EmojicodeLanguageServer
//

#ifndef Index_hpp
#define Index_hpp

#include "Analysis/AnalysisObserver.hpp"
#include "Checker.hpp"
#include "Types/Type.hpp"
#include "Types/TypeContext.hpp"
#include <map>
#include <string>
#include <vector>

namespace EmojicodeCompiler {
class SourceFile;
}

namespace EmojicodeLanguageServer {

/// An expression or type in the code and the context in which it was analysed.
struct IndexedNode {
    size_t line;
    size_t character;
    /// Exactly one of expr and type is set.
    EmojicodeCompiler::ASTExpr *expr = nullptr;
    EmojicodeCompiler::ASTType *type = nullptr;
    EmojicodeCompiler::TypeContext context;
};

/// A local variable or parameter and the block in which it can be used.
struct IndexedVariable {
    std::u32string name;
    EmojicodeCompiler::Type type;
    bool constant;
    Location declaration;
    /// The block in which the variable is visible, from its 🍇 to its 🍉. If the analysis of the function stopped
    /// at an error, the end is unknown and its line is SIZE_MAX.
    Location scopeBegin;
    Location scopeEnd;
    EmojicodeCompiler::TypeContext context;
};

/// A block and the context of the function it is in.
struct IndexedScope {
    Location begin;
    Location end;
    EmojicodeCompiler::TypeContext context;
};

/// Records the expressions, types and variables the compiler analysed, by the file they are in, so that they can be
/// looked up by their position.
class Index : public EmojicodeCompiler::AnalysisObserver {
public:
    void analysedExpression(EmojicodeCompiler::ASTExpr *expr, EmojicodeCompiler::ExpressionAnalyser *analyser) override;
    void analysedType(EmojicodeCompiler::ASTType *type) override;
    void leavingScope(const EmojicodeCompiler::ASTBlock &block, const EmojicodeCompiler::Scope &scope,
                      EmojicodeCompiler::FunctionAnalyser *analyser) override;
    void analysisFailed(EmojicodeCompiler::FunctionAnalyser *analyser) override;

    /// Returns the nodes whose position is exactly @p line and @p character (as the compiler counts them) in the
    /// file at @p path.
    std::vector<const IndexedNode*> nodesAt(const std::string &path, size_t line, size_t character) const;
    /// Returns all nodes in the file at @p path, ordered by position.
    const std::vector<IndexedNode>& nodes(const std::string &path) const;
    const std::vector<IndexedVariable>& variables() const { return variables_; }
    const std::vector<IndexedScope>& scopes() const { return scopes_; }

    /// Returns the canonical path of @p file, cached as this is needed for every node.
    const std::string& path(EmojicodeCompiler::SourceFile *file);

    /// Sorts the nodes by position. Call once after the analysis.
    void finish();

private:
    void recordScope(const Location &begin, const Location &end, const EmojicodeCompiler::Scope &scope,
                     EmojicodeCompiler::FunctionAnalyser *analyser);

    std::map<std::string, std::vector<IndexedNode>> nodes_;
    std::vector<IndexedVariable> variables_;
    std::vector<IndexedScope> scopes_;
    std::map<EmojicodeCompiler::SourceFile *, std::string> paths_;
};

}  // namespace EmojicodeLanguageServer

#endif /* Index_hpp */
