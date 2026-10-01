//
//  ErrorSelfDestructing.cpp
//  runtime
//
//  Created by Theo Weidmann on 24.09.18.
//

#include "ErrorSelfDestructing.hpp"
#include "Analysis/FunctionAnalyser.hpp"
#include "Generation/RunTimeHelper.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Scoping/SemanticScoper.hpp"
#include "Types/Class.hpp"
#include "Types/TypeExpectation.hpp"
#include "AST/ASTExpr.hpp"
#include "AST/ASTUnary.hpp"

namespace EmojicodeCompiler {

void ErrorSelfDestructing::analyseInstanceVariables(FunctionAnalyser *analyser, const SourcePosition &p) {
    for (auto &var : analyser->scoper().instanceScope()->map()) {
        Variable &cv = var.second;
        auto incident = PathAnalyserIncident(true, cv.id());
        if (cv.type().isManaged() && analyser->pathAnalyser().hasPotentially(incident)) {
            release_.emplace_back(cv.id(), cv.type());
            if (!analyser->pathAnalyser().hasCertainly(incident)) {
                throw CompilerError(p, "Initialization cannot be aborted: Variable ", utf8(cv.name()),
                                    " must either be initialized on all paths or not at all.");
            }
        }
    }
    class_ = dynamic_cast<Class*>(analyser->function()->owner());
    assert(class_ != nullptr);
}

void ErrorSelfDestructing::buildDestruct(FunctionCodeGenerator *fg) const {
    if (class_ != nullptr) {
        for (auto &release : release_) {
            fg->releaseByReference(fg->instanceVariablePointer(release.first), release.second);
        }
        auto clInf = fg->buildGetClassInfoFromObject(fg->thisValue());
        fg->createIf(fg->builder().CreateICmpEQ(clInf, class_->classInfo()), [&] {
            if (class_->storesGenericArgs()) {
                // The receiver's generic type description may have been dynamically allocated (see
                // TypeDescriptionGenerator); a normal deinitializer would free it, but that does not run for a
                // partially initialized object, so this is the only opportunity to release it before the receiver
                // itself is freed below.
                auto gargs = fg->builder().CreateLoad(fg->genericArgsType(), fg->genericArgsPtr());
                fg->freeOwnedDescription(gargs);
            }
            fg->builder().CreateCall(fg->generator()->runTime().releaseWithoutDeinit(), fg->thisValue());
        });
    }
}

ASTCall* ErrorHandling::handleCall(std::shared_ptr<ASTExpr> *expr) {
    handledCall_ = dynamic_cast<ASTCall *>(expr->get());
    if (handledCall_ != nullptr) {
        handledCall_->setHandledError();
        auto node = std::make_shared<ASTHandledCall>(std::move(*expr));
        handledCallNode_ = node.get();
        *expr = std::move(node);
    }
    return handledCall_;
}

Type ErrorHandling::expectCall(ExpressionAnalyser *analyser, std::shared_ptr<ASTExpr> *expr) {
    if (analyser->analyse(*expr).type() == TypeType::NoReturn) {
        return Type::noReturn();
    }
    return analyser->comply(TypeExpectation(false, false), expr);
}

llvm::Value* ErrorHandling::prepareErrorDestination(FunctionCodeGenerator *fg) const {
    // Errors are always object pointers, even if the error type is a generic variable, which has no LLVM type.
    auto type = fg->typeHelper().pointer();
    auto alloca = fg->createEntryAlloca(type, "error");
    fg->builder().CreateStore(llvm::Constant::getNullValue(type), alloca);
    handledCall_->setErrorPointer(alloca);
    return alloca;
}

void ErrorHandling::generateHandledCall(FunctionCodeGenerator *fg) const {
    handledCallNode_->generateCall(fg);
}

bool ErrorHandling::handledCallProducesTemporaryObject() const {
    return handledCall_->producesTemporaryObject();
}

llvm::Value* ErrorHandling::isError(FunctionCodeGenerator *fg, llvm::Value *errorDestination) const {
    return fg->isErrorSet(errorDestination);
}

}  // namespace EmojicodeCompiler
