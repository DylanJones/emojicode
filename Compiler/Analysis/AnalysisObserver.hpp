//
//  AnalysisObserver.hpp
//  EmojicodeCompiler
//

#ifndef AnalysisObserver_hpp
#define AnalysisObserver_hpp

#include <memory>

namespace EmojicodeCompiler {

class ASTBlock;
class ASTExpr;
class ASTType;
class ExpressionAnalyser;
class FunctionAnalyser;
class Package;
class Scope;

/// An AnalysisObserver is told about what the semantic analysis finds out, e.g. by the language server, which
/// records the types of expressions to show them to the user. Set one with Compiler::setAnalysisObserver.
///
class AnalysisObserver {
public:
    /// Called after @p expr was analysed and its expressionType() was set. The analysis may replace the node in the
    /// tree afterwards, e.g. to box its value, so an observer that keeps it must keep the pointer.
    virtual void analysedExpression(const std::shared_ptr<ASTExpr> &expr, ExpressionAnalyser *analyser) = 0;
    /// Called after @p type was analysed and its type() was set. The node may be temporary, so an observer must not
    /// keep it.
    virtual void analysedType(ASTType *type) = 0;
    /// Called when the scope of @p block is left, with the variables declared in it.
    virtual void leavingScope(const ASTBlock &block, const Scope &scope, FunctionAnalyser *analyser) = 0;
    /// Called when the analysis of a function or closure stopped because of an error. The scopes that were not left
    /// yet are still available from the analyser's scoper(). For an error in a closure, this is called for the
    /// closure and then for each function or closure around it.
    virtual void analysisFailed(FunctionAnalyser *analyser) = 0;
    /// Called when the declarations of @p package were analysed, just before the bodies of its functions are. An
    /// error in a declaration stops the analysis of the package before this.
    virtual void analysingFunctions(Package *package) = 0;

    virtual ~AnalysisObserver() = default;
};

}  // namespace EmojicodeCompiler

#endif /* AnalysisObserver_hpp */
