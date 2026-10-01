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
#include "Types/ValueType.hpp"
#include "Generation/LLVMTypeHelper.hpp"
#include "Generation/TypeDescriptionGenerator.hpp"
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <stdexcept>

namespace EmojicodeCompiler {

/// Whether @p calleeType is a reference to a mutable variable, which owns its box. The method called on it may mutate
/// its value. Protocols do not tell which of their methods mutate, and the value of a box stored in memory other code
/// owns, like a parameter or this of a method that does not mutate, must not be touched.
static bool isMutableVariable(const Type &calleeType) {
    return calleeType.isReference() && calleeType.isMutable();
}

CallCodeGenerator::CallCodeGenerator(FunctionCodeGenerator *fg, CallType callType) : fg_(fg), callType_(callType) {}
CallCodeGenerator::~CallCodeGenerator() = default;

llvm::Value* CallCodeGenerator::markNeverReturning(llvm::Value *value, Function *function) {
    if (function->neverReturns()) {
        if (auto call = llvm::dyn_cast<llvm::CallInst>(value)) {
            call->setDoesNotReturn();
            call->addFnAttr(llvm::Attribute::Cold);
        }
    }
    return value;
}

/// Whether a value of this type can hold copy-on-write storage that a mutation of the receiver would detach from.
static bool mayAliasReceiver(const Type &type) {
    switch (type.type()) {
        case TypeType::ValueType:
            return type.valueType()->isManaged();
        case TypeType::Optional:
            return mayAliasReceiver(type.optionalType());
        case TypeType::Box:
            return true;
        default:
            return false;
    }
}

llvm::Value *CallCodeGenerator::generate(llvm::Value *callee, const Type &type, const ASTArguments &astArgs,
                                         Function *function, llvm::Value *errorPointer,
                                         const std::vector<llvm::Value *> &supplArgs,
                                         const std::function<void()> &beforeDispatch) {
    evaluateArguments(callee != nullptr, type, astArgs, function, errorPointer, supplArgs);
    return dispatchEvaluated(callee, beforeDispatch);
}

void CallCodeGenerator::evaluateArguments(bool hasCallee, const Type &type, const ASTArguments &astArgs,
                                          Function *function, llvm::Value *errorPointer,
                                          const std::vector<llvm::Value *> &supplArgs) {
    type_ = type;
    astArgs_ = &astArgs;
    function_ = function;
    snapshots_.clear();
    args_ = createArgsVector(nullptr, astArgs, errorPointer, supplArgs);
    // A by-value argument of a value type is borrowed: it can alias the receiver's storage, which the 🖍 method may
    // mutate and so modify the argument. The call gets an owned snapshot of it instead.
    bool receiverOwnsStorage = (type.type() == TypeType::ValueType && callType_ == CallType::StaticDispatch) ||
                               (type.type() == TypeType::Box && callType_ == CallType::DynamicProtocolDispatch);
    if (function->mutating() && hasCallee && receiverOwnsStorage && dynamic_cast<Initializer *>(function) == nullptr) {
        for (size_t i = 0; i < astArgs.args().size(); i++) {
            auto &argType = astArgs.args()[i]->expressionType();
            if (!mayAliasReceiver(argType) || !fg_->isManagedByReference(argType)) continue;
            auto &arg = args_[i + 1];
            auto llvmType = fg_->typeHelper().llvmTypeFor(argType);
            auto slot = fg_->createEntryAlloca(llvmType);
            fg_->builder().CreateStore(arg->getType()->isPointerTy() ? fg_->builder().CreateLoad(llvmType, arg) : arg,
                                       slot);
            if (arg->getType()->isPointerTy()) {
                arg = slot;
            }
            fg_->retain(slot, argType);
            snapshots_.emplace_back(slot, argType);
        }
    }
}

llvm::Value *CallCodeGenerator::dispatchEvaluated(llvm::Value *callee, const std::function<void()> &beforeDispatch) {
    bool erasedReference = callee != nullptr && callee->getType() == fg_->typeHelper().erasedReference();
    if (callType_ != CallType::StaticContextfreeDispatch) {
        // The method is called on a box with the value, which is written back if the method mutates it.
        args_.front() = erasedReference ? fg_->buildErasedReferenceBox(callee, type_) : callee;
    }
    if (beforeDispatch) {
        beforeDispatch();
    }
    auto value = dispatch(function_, type_, *astArgs_, args_);
    for (auto &snapshot : snapshots_) {
        fg_->release(snapshot.first, snapshot.second);
    }
    snapshots_.clear();
    if (erasedReference) {
        fg_->buildErasedReferenceWriteBack(callee, args_.front(), type_, function_->mutating());
    }
    restoreStack(function_);
    return value;
}

void CallCodeGenerator::restoreStack(Function *function) {
    auto returnType = function->returnType();
    if (tdg_ != nullptr && (returnType == nullptr || !LLVMTypeHelper::isErasedReference(returnType->type()))) {
        tdg_->restoreStack();
    }
}

llvm::Value* CallCodeGenerator::dispatch(Function *function, const Type &type, const ASTArguments &astArgs,
                                         const std::vector<llvm::Value *> &args) {
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
            return markNeverReturning(call, function);
        }
        case CallType::DynamicDispatch:
        case CallType::DynamicDispatchOnType:
            assert(type.type() == TypeType::Class);
            return markNeverReturning(createDynamicDispatch(function, args, astArgs.genericArgumentTypes()), function);
        case CallType::DynamicProtocolDispatch: {
            assert(type.type() == TypeType::Box);

            llvm::Value *conformance;
            if (type.boxedFor().type() != TypeType::Protocol) {
                conformance = buildFindProtocolConformance(args, type, type.unboxed());
            }
            else {
                conformance = fg()->builder().CreateLoad(fg()->typeHelper().pointer(),
                                                         fg()->buildGetBoxInfoPtr(args.front()));
            }
            // Only a 🖍 method can mutate the value, as only a 🖍 protocol method can be implemented by one.
            return createDynamicProtocolDispatch(function, args, astArgs.genericArgumentTypes(), conformance,
                                                 isMutableVariable(type) && function->mutating());
        }
        case CallType::None:
            break;
    }
    throw std::domain_error("CallType::None is not a valid call type");
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
                                                             const Type &calleeType, const Type &protocol) {
    // The box can be for another protocol, e.g. a multiprotocol value returned as a generic argument for it.
    auto boxInfo = fg()->builder().CreateLoad(fg()->typeHelper().pointer(), fg()->buildGetBoxInfoPtr(args.front()));
    return fg()->buildFindProtocolConformance(args.front(), fg()->buildGetValueBoxInfo(boxInfo, calleeType),
                                              protocol.protocol()->rtti());
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
    if (callee->getType() == fg()->typeHelper().erasedReference()) {
        auto box = fg()->buildErasedReferenceBox(callee, calleeType);
        auto value = generate(box, calleeType, args, function, errorPointer, multiprotocolN);
        fg()->buildErasedReferenceWriteBack(callee, box, calleeType, function->mutating());
        return value;
    }

    auto argsv = createArgsVector(callee, args, errorPointer, {});

    auto &protocol = calleeType.protocols()[multiprotocolN];
    llvm::Value *conformance = nullptr;
    // The box can be for a multiprotocol of some of the protocols, e.g. a multiprotocol value returned as a generic
    // argument for it, whose table has the conformances in its order.
    if (auto index = FunctionCodeGenerator::multiprotocolIndex(calleeType, protocol.protocol())) {
        auto boxInfo = fg()->builder().CreateLoad(fg()->typeHelper().pointer(),
                                                  fg()->buildGetBoxInfoPtr(argsv.front()));
        conformance = fg()->buildGetBoxConformance(boxInfo, calleeType, *index);
    }
    if (conformance == nullptr) {
        conformance = buildFindProtocolConformance(argsv, calleeType, protocol);
    }
    auto value = createDynamicProtocolDispatch(function, std::move(argsv), args.genericArgumentTypes(), conformance,
                                               isMutableVariable(calleeType) && function->mutating());
    restoreStack(function);
    return value;
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
    auto info = callType_ == CallType::DynamicDispatchOnType ? fg()->buildGetClassInfoFromTypeValue(args.front()) :
        fg()->buildGetClassInfoFromObject(args.front());
    auto tablePtr = fg()->builder().CreateConstInBoundsGEP2_32(fg_->typeHelper().classInfo(), info, 0, 1);
    auto table = fg()->builder().CreateLoad(fg()->typeHelper().pointer(), tablePtr, "table");
    return dispatchFromVirtualTable(function, table, args, genericArgs);
}

llvm::Value *CallCodeGenerator::createDynamicProtocolDispatch(Function *function, std::vector<llvm::Value *> args,
                                                              const std::vector<Type> &genericArgs,
                                                              llvm::Value *conformance, bool uniqueBox) {
    args.front() = getProtocolCallee(args, conformance, uniqueBox);

    auto tablePtr = fg()->builder().CreateConstGEP2_32(fg()->typeHelper().protocolConformance(), conformance, 0, 1);
    auto table = fg()->builder().CreateLoad(fg()->typeHelper().pointer(), tablePtr, "table");
    return dispatchFromVirtualTable(function, table, args, genericArgs);
}

llvm::Value *CallCodeGenerator::getProtocolCallee(std::vector<Value *> &args, llvm::Value *conformance,
                                                  bool uniqueBox) const {
    auto shouldLoadPtr = fg()->builder().CreateConstGEP2_32(fg()->typeHelper().protocolConformance(),
                                                            conformance, 0, 0);
    auto shouldLoad = fg()->builder().CreateLoad(llvm::Type::getInt1Ty(fg()->ctx()), shouldLoadPtr, "shouldLoad");
    return fg()->createIfElsePhi(shouldLoad, [this, &args, conformance, uniqueBox]() {
        if (uniqueBox) {
            fg()->makeBoxValueUnique(conformance, args.front());
        }
        return fg()->builder().CreateLoad(fg()->typeHelper().pointer(), fg()->buildGetBoxValuePtr(args.front()));
    }, [this, &args]() {
        return fg()->buildGetBoxValuePtr(args.front());
    });
}

}  // namespace EmojicodeCompiler
