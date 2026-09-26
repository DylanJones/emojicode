//
//  ASTMethod.hpp
//  Emojicode
//
//  Created by Theo Weidmann on 05/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#ifndef ASTMethod_hpp
#define ASTMethod_hpp

#include "ASTExpr.hpp"
#include "Functions/CallType.h"
#include <utility>
#include <map>

namespace EmojicodeCompiler {

class FunctionAnalyser;
class Compiler;

class ASTMethodable : public ASTCall {
public:
    /// The method that is called, or nullptr if the call is built in or was not analysed.
    Function* method() const { return method_; }
    /// The type on which the method is called.
    const Type& calleeType() const { return calleeType_; }

protected:
    explicit ASTMethodable(const SourcePosition &p) : ASTCall(p), args_(p) {}
    ASTMethodable(const SourcePosition &p, ASTArguments args) : ASTCall(p), args_(std::move(args)) {}

    Type analyseMethodCall(ExpressionAnalyser *analyser, const std::u32string &name,
                           std::shared_ptr<ASTExpr> &callee);
    /// Analyses this node as method call.
    /// @param otype Result of analysing `callee` with TypeExpectation `TypeExpectation()`.
    Type analyseMethodCall(ExpressionAnalyser *analyser, const std::u32string &name, std::shared_ptr<ASTExpr> &callee,
                           const Type &otype);

    enum class BuiltInType {
        None,
        DoubleMultiply, DoubleAdd, DoubleSubstract, DoubleDivide, DoubleGreater, DoubleGreaterOrEqual,
        DoubleLess, DoubleLessOrEqual, DoubleRemainder, DoubleEqual, DoubleInverse, Power, Log2, Log10, Ln, Ceil, Floor,
        Round, DoubleAbs, DoubleToInteger,
        IntegerMultiply, IntegerAdd, IntegerSubstract, IntegerDivide, IntegerGreater, IntegerGreaterOrEqual,
        IntegerLess, IntegerLessOrEqual, IntegerLeftShift, IntegerRightShift, IntegerOr, IntegerAnd, IntegerXor,
        IntegerRemainder, IntegerToDouble, IntegerNot, IntegerInverse, IntegerToByte, ByteToInteger,
        BooleanAnd, BooleanOr, BooleanNegate,
        Equal, Store, Load, Release, MemoryMove, MemorySet, IsNoValueLeft, IsNoValueRight, Multiprotocol,
        UnsignedDivide, UnsignedRemainder, UnsignedGreater, UnsignedGreaterOrEqual, UnsignedLess,
        UnsignedLessOrEqual, SignedRightShift,
        /// Converts a value of a C type to the return type of the method.
        CConvert,
        /// 📍: The value at the address.
        CPointerLoad,
        /// 📍: Stores the argument at the address.
        CPointerStore,
        /// 📍: The address advanced by the argument times the size of the pointee.
        CPointerAdvance,
        /// The value itself, reinterpreted as the return type. Used for pointer casts and to take over the reference
        /// to an object from a 🕳.
        CReinterpret,
        /// 🕳: The object the pointer points to, retained.
        CBorrowObject,
    };

    BuiltInType builtIn_ = BuiltInType::None;
    ASTArguments args_;
    CallType callType_ = CallType::None;
    Type calleeType_ = Type::noReturn();
    size_t multiprotocolN_ = 0;
    Function *method_ = nullptr;
    Type castTo_ = Type::noReturn();

    bool isErrorProne() const override;
    const Type& errorType() const override;

private:
    static std::map<std::pair<TypeDefinition*, char32_t>, BuiltInType> kBuiltIns;
    static void prepareBuiltIns(Compiler *c);

    bool builtIn(ExpressionAnalyser *analyser, const Type &type, const std::u32string &name);
    /// Recognizes the built-in methods of types with a C representation declared in source.
    bool builtInC(ExpressionAnalyser *analyser, const Type &type, const std::u32string &name);

    Type analyseMultiProtocolCall(ExpressionAnalyser *analyser, const std::u32string &name,
                                  const std::shared_ptr<ASTExpr> &callee);

    void checkMutation(ExpressionAnalyser *analyser, const std::shared_ptr<ASTExpr> &callee) const;
    void determineCallType(const ExpressionAnalyser *analyser);
    void determineCalleeType(ExpressionAnalyser *analyser, const std::u32string &name,
                             std::shared_ptr<ASTExpr> &callee, const Type &otype);
    Type analyseTypeMethodCall(ExpressionAnalyser *analyser, const std::u32string &name,
                               std::shared_ptr<ASTExpr> &callee);
};

class ASTMethod final : public ASTMethodable {
public:
    ASTMethod(std::u32string name, std::shared_ptr<ASTExpr> callee, const ASTArguments &args, const SourcePosition &p)
    : ASTMethodable(p, args), name_(std::move(name)), callee_(std::move(callee)) {}
    Type analyse(ExpressionAnalyser *analyser) override;
    void toCode(PrettyStream &pretty) const override;
    Value* generate(FunctionCodeGenerator *fg) const override;
    void analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) override;
    void mutateReference(ExpressionAnalyser *analyser) final;

    const std::u32string& name() const { return name_; }

private:
    std::u32string name_;
    std::shared_ptr<ASTExpr> callee_;

    llvm::Value* buildMemoryAddress(FunctionCodeGenerator *fg, llvm::Value *memory, llvm::Value *offset,
                                    const Type &type) const;
    llvm::Value* buildAddOffsetAddress(FunctionCodeGenerator *fg, llvm::Value *memory, llvm::Value *offset) const;
};
    
}  // namespace EmojicodeCompiler

#endif /* ASTMethod_hpp */
