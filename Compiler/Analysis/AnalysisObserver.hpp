//
//  AnalysisObserver.hpp
//  EmojicodeCompiler
//

#ifndef AnalysisObserver_hpp
#define AnalysisObserver_hpp

namespace EmojicodeCompiler {

class ASTBlock;
class ASTExpr;
class ASTType;
class ExpressionAnalyser;
class FunctionAnalyser;
class Scope;

/// An AnalysisObserver is told about what the semantic analysis finds out, e.g. by the language server, which
/// records the types of expressions to show them to the user. Set one with Compiler::setAnalysisObserver.
///
/// The nodes passed to the observer are owned by the functions of the packages and live as long as the Compiler.
class AnalysisObserver {
public:
    /// Called after @p expr was analysed and its expressionType() was set.
    virtual void analysedExpression(ASTExpr *expr, ExpressionAnalyser *analyser) = 0;
    /// Called after @p type was analysed and its type() was set.
    virtual void analysedType(ASTType *type) = 0;
    /// Called when the scope of @p block is left, with the variables declared in it.
    virtual void leavingScope(const ASTBlock &block, const Scope &scope, FunctionAnalyser *analyser) = 0;
    /// Called when the analysis of a function stopped because of an error. The scopes that were not left yet are
    /// still available from the analyser's scoper().
    virtual void analysisFailed(FunctionAnalyser *analyser) = 0;

    virtual ~AnalysisObserver() = default;
};

}  // namespace EmojicodeCompiler

#endif /* AnalysisObserver_hpp */
