//
//  ASTUnary.hpp
//  Emojicode
//
//  Created by Theo Weidmann on 04/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#ifndef ASTUnary_hpp
#define ASTUnary_hpp

#include <utility>
#include "ASTExpr.hpp"
#include "ErrorSelfDestructing.hpp"
#include "Releasing.hpp"
#include "Scoping/SemanticScopeStats.hpp"

namespace EmojicodeCompiler {

/// Expressions that operate on the value produced by another expression inherit from this class.
class ASTUnary : public ASTExpr {
public:
    ASTUnary(std::shared_ptr<ASTExpr> value, const SourcePosition &p) : ASTExpr(p), expr_(std::move(value)) {}

protected:
    std::shared_ptr<ASTExpr> expr_;
};

/// Unary expressions that do not themselves affect the flow category or value category of ::expr_ should inherit from
/// this class.
///
/// When analysing the flow category, this class simply analyses ::expr_ with the same category. If the value of an
/// expression defined by subclass of this class is taken, ::expr_ is taken.
///
/// @note Expressions inherting from this class must not pass their result to ::handleResult. This is because if the
/// resulting value of this expression is temporary, it will be released by ::expr_ as this expression has not taken the
/// value then.
/// @see MFFunctionAnalyser
class ASTUnaryMFForwarding : public ASTUnary {
public:
    using ASTUnary::ASTUnary;
    void analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) override;

protected:
    void unsetIsTemporaryPost() final { expr_->unsetIsTemporary(); }
};

/// The call whose error is handled by an ErrorHandling node, which wraps the call in this node before it is analysed.
///
/// The analysis may wrap the value of the call in nodes that box or unbox it, e.g. the value of a call to a method
/// that returns its generic argument and is not specialized. These nodes must not operate on the value of a call that
/// raised an error, which is undefined. The ErrorHandling node therefore generates the call with generateCall() and
/// the expression containing this node only if there was no error, whereupon this node returns the value of the call.
class ASTHandledCall final : public ASTUnaryMFForwarding {
public:
    explicit ASTHandledCall(std::shared_ptr<ASTExpr> call) : ASTUnaryMFForwarding(std::move(call), SourcePosition()) {}
    Type analyse(ExpressionAnalyser *analyser) override;
    Type comply(ExpressionAnalyser *analyser, const TypeExpectation &expectation) override;
    Value* generate(FunctionCodeGenerator *fg) const override;
    void generateCall(FunctionCodeGenerator *fg) const { value_ = expr_->generate(fg); }

    void toCode(PrettyStream &pretty) const override;

private:
    mutable Value *value_ = nullptr;
};

class ASTUnwrap final : public ASTUnaryMFForwarding, public ErrorHandling {
    using ASTUnaryMFForwarding::ASTUnaryMFForwarding;
public:
    Type analyse(ExpressionAnalyser *analyser) override;
    Value* generate(FunctionCodeGenerator *fg) const override;

    void toCode(PrettyStream &pretty) const override;

private:
    bool error_ = false;
    Function *method_;
    Value* generateErrorUnwrap(FunctionCodeGenerator *fg) const;
};

class ASTReraise final : public ASTUnaryMFForwarding, public ErrorHandling, public Releasing,
                          private ErrorSelfDestructing {
    using ASTUnaryMFForwarding::ASTUnaryMFForwarding;
public:
    Type analyse(ExpressionAnalyser *analyser) override;
    Value* generate(FunctionCodeGenerator *fg) const override;

    void toCode(PrettyStream &pretty) const override;

    void analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) override;

private:
    SemanticScopeStats stats_;
};

class ASTSelection final : public ASTUnaryMFForwarding {
public:
    ASTSelection(std::shared_ptr<ASTExpr> value, std::shared_ptr<ASTTypeExpr> type, const SourcePosition &p)
        : ASTUnaryMFForwarding(std::move(value), p), typeExpr_(std::move(type)) {}
    Type analyse(ExpressionAnalyser *analyser) override;
    Value* generate(FunctionCodeGenerator *fg) const override { return expr_->generate(fg); }
    Type comply(ExpressionAnalyser *analyser, const TypeExpectation &expectation) override;
    void toCode(PrettyStream &pretty) const override;

private:
    std::shared_ptr<ASTTypeExpr> typeExpr_;
};

}  // namespace EmojicodeCompiler

#endif /* ASTUnary_hpp */
