//
//  CallCodeGenerator.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 05/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "CallCodeGenerator.hpp"
#include "CTrampolineGenerator.hpp"
#include "AST/ASTExpr.hpp"
#include "FunctionCodeGenerator.hpp"
#include "Functions/Initializer.hpp"
#include "Types/Protocol.hpp"
#include "Types/TypeDefinition.hpp"
#include "Generation/TypeDescriptionGenerator.hpp"
#include <llvm/Support/raw_ostream.h>
#include <stdexcept>

namespace EmojicodeCompiler {

CallCodeGenerator::CallCodeGenerator(FunctionCodeGenerator *fg, CallType callType) : fg_(fg), callType_(callType) {}
CallCodeGenerator::~CallCodeGenerator() = default;

llvm::Value *CallCodeGenerator::generate(llvm::Value *callee, const Type &type, const ASTArguments &astArgs,
                                         Function *function, llvm::Value *errorPointer,
                                         const std::vector<llvm::Value *> &supplArgs) {
    auto args = createArgsVector(callee, astArgs, errorPointer, supplArgs);

    assert(function != nullptr);
    switch (callType_) {
        case CallType::StaticContextfreeDispatch:
        case CallType::StaticDispatch: {
            auto llvmFn = function->reificationFor(astArgs.genericArgumentTypes()).function;
            if (needsCTrampoline(function)) {
                return generateCTrampolineCall(function, llvmFn, args);
            }
            auto call = fg_->builder().CreateCall(llvmFn, args);
            if (function->isC()) {
                call->setAttributes(llvmFn->getAttributes());
            }
            return call;
        }
        case CallType::DynamicDispatch:
        case CallType::DynamicDispatchOnType:
            assert(type.type() == TypeType::Class);
            return createDynamicDispatch(function, args, astArgs.genericArgumentTypes());
        case CallType::DynamicProtocolDispatch: {
            assert(type.type() == TypeType::Box);

            llvm::Value *conformance;
            if (type.boxedFor().type() != TypeType::Protocol) {
                conformance = buildFindProtocolConformance(args, type.unboxed());
            }
            else {
                conformance = fg()->builder().CreateLoad(fg()->typeHelper().pointer(),
                                                         fg()->buildGetBoxInfoPtr(args.front()));
            }
            return createDynamicProtocolDispatch(function, args, astArgs.genericArgumentTypes(), conformance);
        }
        case CallType::None:
            throw std::domain_error("CallType::None is not a valid call type");
    }
    if (tdg_ != nullptr) {
        tdg_->restoreStack();
    }
}

llvm::Value* CallCodeGenerator::generateCTrampolineCall(Function *function, llvm::Function *trampoline,
                                                       std::vector<llvm::Value *> args) {
    for (size_t i = 0; i < args.size(); i++) {
        if (isCStructValue(function->parameters()[i].type->type())) {
            auto slot = fg_->createEntryAlloca(args[i]->getType());
            fg_->builder().CreateStore(args[i], slot);
            args[i] = slot;
        }
    }
    auto &returnType = function->returnType()->type();
    llvm::Value *result = nullptr;
    if (isCStructValue(returnType)) {
        result = fg_->createEntryAlloca(fg_->typeHelper().llvmTypeFor(returnType));
        args.emplace_back(result);
    }
    auto call = fg_->builder().CreateCall(trampoline, args);
    call->setAttributes(trampoline->getAttributes());
    if (result != nullptr) {
        return fg_->builder().CreateLoad(fg_->typeHelper().llvmTypeFor(returnType), result);
    }
    return call;
}

llvm::Value* CallCodeGenerator::buildFindProtocolConformance(const std::vector<llvm::Value *> &args,
                                                             const Type &protocol) {
    auto boxInfo = fg()->builder().CreateLoad(fg()->typeHelper().pointer(), fg()->buildGetBoxInfoPtr(args.front()));
    return fg()->buildFindProtocolConformance(args.front(), boxInfo, protocol.protocol()->rtti());
}

std::vector<Value *> CallCodeGenerator::createArgsVector(llvm::Value *callee, const ASTArguments &args,
                                                         llvm::Value *errorPointer,
                                                         const std::vector<llvm::Value *> &supplArgs) {
    std::vector<Value *> argsVector;
    if (callType_ != CallType::StaticContextfreeDispatch) {
        argsVector.emplace_back(callee);
    }
    for (auto &arg : args.args()) {
        argsVector.emplace_back(arg->generate(fg_));
    }
    argsVector.insert(argsVector.end(), supplArgs.begin(), supplArgs.end());
    if (!args.genericArguments().empty()) {
        tdg_ = std::make_unique<TypeDescriptionGenerator>(fg_, TypeDescriptionUser::Function);
        argsVector.emplace_back(tdg_->generate(args.genericArguments()));
    }
    if (errorPointer != nullptr) {
        argsVector.emplace_back(errorPointer);
    }
    return argsVector;
}

llvm::Value *MultiprotocolCallCodeGenerator::generate(llvm::Value *callee, const Type &calleeType,
                                                      const ASTArguments &args, Function* function,
                                                      llvm::Value *errorPointer, size_t multiprotocolN) {
    assert(calleeType.type() == TypeType::Box);
    assert(function != nullptr);

    auto argsv = createArgsVector(callee, args, errorPointer, {});

    llvm::Value *conformance;
    if (calleeType.boxedFor().type() != TypeType::MultiProtocol) {
        conformance = buildFindProtocolConformance(argsv, calleeType.protocols()[multiprotocolN]);
    }
    else {
        auto mpt = fg()->typeHelper().multiprotocolConformance(calleeType);
        auto mpl = fg()->builder().CreateLoad(fg()->typeHelper().pointer(), fg()->buildGetBoxInfoPtr(argsv.front()));

        conformance = fg()->builder().CreateLoad(fg()->typeHelper().pointer(),
                                                 fg()->builder().CreateConstGEP2_32(mpt, mpl, 0, multiprotocolN));
    }
    return createDynamicProtocolDispatch(function, std::move(argsv), args.genericArgumentTypes(), conformance);
}

llvm::Value *CallCodeGenerator::dispatchFromVirtualTable(Function *function, llvm::Value *virtualTable,
                                                         const std::vector<llvm::Value *> &args,
                                                         const std::vector<Type> &genericArguments) {
    auto reification = function->reificationFor(genericArguments);
    auto id = fg()->int32(reification.vti());
    auto ptr = fg()->typeHelper().pointer();
    auto dispatchedFunc = fg()->builder().CreateLoad(ptr, fg()->builder().CreateInBoundsGEP(ptr, virtualTable, id),
                                                     "dispatchFunc");

    std::vector<llvm::Type *> argTypes = reification.functionType()->params();
    if (callType_ == CallType::DynamicProtocolDispatch) {
        argTypes.front() = ptr;
    }
    else if (callType_ == CallType::DynamicDispatch) {
        argTypes.front() = args.front()->getType();
    }
    else if (callType_ == CallType::DynamicDispatchOnType) {
        assert(argTypes.front() == args.front()->getType());
    }

    auto funcType = llvm::FunctionType::get(reification.functionType()->getReturnType(), argTypes, false);
    return fg_->builder().CreateCall(funcType, dispatchedFunc, args);
}

llvm::Value *CallCodeGenerator::createDynamicDispatch(Function *function, const std::vector<llvm::Value *> &args,
                                                      const std::vector<Type> &genericArgs) {
    auto info = callType_ == CallType::DynamicDispatchOnType ? args.front() : fg()->buildGetClassInfoFromObject(args.front());
    auto tablePtr = fg()->builder().CreateConstInBoundsGEP2_32(fg_->typeHelper().classInfo(), info, 0, 1);
    auto table = fg()->builder().CreateLoad(fg()->typeHelper().pointer(), tablePtr, "table");
    return dispatchFromVirtualTable(function, table, args, genericArgs);
}

llvm::Value *CallCodeGenerator::createDynamicProtocolDispatch(Function *function, std::vector<llvm::Value *> args,
                                                              const std::vector<Type> &genericArgs,
                                                              llvm::Value *conformance) {
    args.front() = getProtocolCallee(args, conformance);

    auto tablePtr = fg()->builder().CreateConstGEP2_32(fg()->typeHelper().protocolConformance(), conformance, 0, 1);
    auto table = fg()->builder().CreateLoad(fg()->typeHelper().pointer(), tablePtr, "table");
    return dispatchFromVirtualTable(function, table, args, genericArgs);
}

llvm::Value *CallCodeGenerator::getProtocolCallee(std::vector<Value *> &args, llvm::Value *conformance) const {
    auto shouldLoadPtr = fg()->builder().CreateConstGEP2_32(fg()->typeHelper().protocolConformance(),
                                                            conformance, 0, 0);
    auto shouldLoad = fg()->builder().CreateLoad(llvm::Type::getInt1Ty(fg()->ctx()), shouldLoadPtr, "shouldLoad");
    return fg()->createIfElsePhi(shouldLoad, [this, &args]() {
        return fg()->builder().CreateLoad(fg()->typeHelper().pointer(), fg()->buildGetBoxValuePtr(args.front()));
    }, [this, &args]() {
        return fg()->buildGetBoxValuePtr(args.front());
    });
}

}  // namespace EmojicodeCompiler
