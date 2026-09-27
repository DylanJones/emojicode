//
//  ErrorSelfDestructing.hpp
//  runtime
//
//  Created by Theo Weidmann on 24.09.18.
//

#ifndef ErrorSelfDestructing_hpp
#define ErrorSelfDestructing_hpp

#include "Types/Type.hpp"
#include <memory>
#include <vector>

namespace llvm {
class Value;
}  // namespace llvm

namespace EmojicodeCompiler {

class FunctionAnalyser;
class FunctionCodeGenerator;
class ASTExpr;
class ASTCall;
class ASTHandledCall;
struct SourcePosition;

/// This class encapsulates the logic of deinitializing an object when initialization is aborted by raising an error.
///
/// An object whose initialization is aborted by an error cannot be deinitialized by the deinitializer as it might
/// not be fully initialiized. Instead all instance variables that were indeed initialized must be released.
/// This is what this class does.
class ErrorSelfDestructing {
protected:
    /// Must be called during semantic analysis to determine which variables were already initialized.
    void analyseInstanceVariables(FunctionAnalyser *analyser, const SourcePosition &p);
    /// Builds the IR to release all instance variables that were initialized when analyseInstanceVariables() was
    /// called.
    void buildDestruct(FunctionCodeGenerator *fg) const;

private:
    std::vector<std::pair<size_t, Type>> release_;
    Class *class_ = nullptr;
};

class ErrorHandling {
protected:
    /// If `*expr` is a call, marks its error as handled and wraps it in an ASTHandledCall.
    /// @returns The call or nullptr if `*expr` is not a call.
    /// @pre This function must be called before `*expr` is analysed, as the analysis may wrap it in boxing nodes.
    ASTCall* handleCall(std::shared_ptr<ASTExpr> *expr);

    /// @pre This function must be called before generateHandledCall().
    llvm::Value* prepareErrorDestination(FunctionCodeGenerator *fg) const;
    /// Generates the handled call. The expression that was passed to handleCall() must be generated after the error
    /// was checked, and only if there was none, as the nodes the analysis wrapped around the call operate on its value.
    void generateHandledCall(FunctionCodeGenerator *fg) const;
    /// Whether generateHandledCall() registered the value of the call as a temporary object, which is then the last
    /// temporary object. The value of a call that raised an error is undefined and must not be released.
    ///
    /// Only the call decides this: the nodes the analysis wrapped around the call, e.g. to unbox a value of an
    /// unmanaged type, may not produce a temporary object when the call does, and vice versa.
    bool handledCallProducesTemporaryObject() const;

    llvm::Value* isError(FunctionCodeGenerator *fg, llvm::Value *errorDestination) const;

    ASTCall *handledCall_ = nullptr;

private:
    ASTHandledCall *handledCallNode_ = nullptr;
};

}  // namespace EmojicodeCompiler

#endif /* ErrorSelfDestructing_hpp */
