//
//  _CG.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 03/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "Generation/FunctionCodeGenerator.hpp"
#include "ASTInitialization.hpp"
#include "ASTTypeExpr.hpp"
#include "Functions/CallType.h"
#include "Generation/CallCodeGenerator.hpp"
#include "Generation/TypeDescriptionGenerator.hpp"
#include "Generation/RunTimeHelper.hpp"
#include "Types/Class.hpp"
#include "Types/Enum.hpp"

namespace EmojicodeCompiler {

Value* ASTInitialization::generate(FunctionCodeGenerator *fg) const {
    switch (initType_) {
        case InitType::Class:
        case InitType::ClassStack:
            return generateClassInit(fg);
        case InitType::Enum:
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(fg->ctx()),
                                          typeExpr_->expressionType().enumeration()->getValueFor(name_).second);
        case InitType::ValueType:
            return generateInitValueType(fg);
        case InitType::MemoryAllocation:
            return generateMemoryAllocation(fg);
        case InitType::CConversion:
        case InitType::CRetainObject:
            return generateCInit(fg);
    }
}

Value* ASTInitialization::generateCInit(FunctionCodeGenerator *fg) const {
    auto &arg = args_.args().front();
    auto value = arg->generate(fg);
    Value *result;
    if (initType_ == InitType::CRetainObject) {
        fg->retain(value, arg->expressionType());
        result = value;
    }
    else {
        result = fg->buildCConversion(value, arg->expressionType(), typeExpr_->expressionType());
    }
    if (vtDestination_ != nullptr) {
        fg->builder().CreateStore(result, vtDestination_);
        return nullptr;
    }
    return result;
}

Value* ASTInitialization::genericArgs(FunctionCodeGenerator *fg) const {
    auto user = initType_ == InitType::ValueType ? TypeDescriptionUser::ValueTypeOrValue : TypeDescriptionUser::Class;
    return TypeDescriptionGenerator(fg, user).generate(typeExpr_->expressionType().selfResolvedGenericArgs());
}

Value* ASTInitialization::generateClassInit(FunctionCodeGenerator *fg) const {
    llvm::Value *obj;
    if (typeExpr_->expressionType().isExact()) {
        auto storesGenericArgs = typeExpr_->expressionType().klass()->storesGenericArgs();
        if (typeExpr_->expressionType().klass()->foreign()) {
            assert(!storesGenericArgs);
            obj = CallCodeGenerator(fg, CallType::StaticContextfreeDispatch)
                .generate(nullptr, typeExpr_->expressionType(), args_, initializer_, errorPointer());
        }
        else {
            auto gargs = storesGenericArgs ? genericArgs(fg) : nullptr;
            obj = initObject(fg, args_, initializer_, typeExpr_->expressionType(), errorPointer(),
                             initType_ == InitType::ClassStack, gargs);
            // A purely inherited initializer (one the instantiated class does not itself own) stamps the receiver
            // with the instantiated class's info, which never matches the initializer's own owning class. Its
            // ErrorSelfDestructing therefore never runs, so there is no callee frame that owns this description on
            // failure; this call site is the only remaining owner.
            if (storesGenericArgs && isErrorProne() &&
                initializer_->owner() != typeExpr_->expressionType().klass()) {
                auto null = llvm::ConstantPointerNull::get(fg->typeHelper().pointer());
                auto isError = fg->builder().CreateICmpNE(
                    null, fg->builder().CreateLoad(fg->typeHelper().pointer(), errorPointer()));
                fg->createIf(isError, [&] {
                    fg->createIf(fg->builder().CreateIsNull(fg->builder().CreateExtractValue(gargs, { 1 })), [&] {
                        fg->builder().CreateCall(fg->generator()->runTime().free(),
                                                 { fg->builder().CreateExtractValue(gargs, { 0 }) });
                    });
                });
            }
        }
    }
    else {
        // initializer_ is the type method that calls the required initializer on the class the type value stands for,
        // which takes the generic arguments of the class from the type value.
        obj = CallCodeGenerator(fg, CallType::DynamicDispatchOnType)
            .generate(typeExpr_->generate(fg), typeExpr_->expressionType(), args_, initializer_, errorPointer());
    }
    handleResult(fg, obj);
    return obj;
}

Value* ASTInitialization::generateInitValueType(FunctionCodeGenerator *fg) const {
    auto destination = vtDestination_;
    if (vtDestination_ == nullptr) {
        destination = fg->createEntryAlloca(fg->typeHelper().llvmTypeFor(typeExpr_->expressionType()));
    }

    auto storesGenericArgs = typeExpr_->expressionType().valueType()->storesGenericArgs();
    auto suppl = storesGenericArgs ? std::vector<llvm::Value*> { genericArgs(fg) } : std::vector<llvm::Value*>();
    CallCodeGenerator(fg, CallType::StaticDispatch)
            .generate(destination, typeExpr_->expressionType(), args_, initializer_, errorPointer(), suppl);
    handleResult(fg, nullptr, destination);
    if (vtDestination_ != nullptr) {
        return nullptr;
    }
    return fg->builder().CreateLoad(fg->typeHelper().llvmTypeFor(typeExpr_->expressionType()), destination);
}

Value* ASTInitialization::initObject(FunctionCodeGenerator *fg, const ASTArguments &args, Function *function,
                                     const Type &type, llvm::Value *errorPointer, bool stackInit,
                                     llvm::Value *gArgsDescs) {
    auto llvmType = fg->typeHelper().llvmTypeForTypeDefinition(type);
    auto obj = stackInit ? fg->stackAlloc(llvmType) : fg->alloc(llvmType);
    fg->builder().CreateStore(type.klass()->classInfo(), fg->buildGetClassInfoPtrFromObject(obj));
    auto suppl = gArgsDescs != nullptr ? std::vector<llvm::Value*> { gArgsDescs } : std::vector<llvm::Value*>();
    return CallCodeGenerator(fg, CallType::StaticDispatch).generate(obj, type, args, function, errorPointer, suppl);
}

Value* ASTInitialization::generateMemoryAllocation(FunctionCodeGenerator *fg) const {
    auto size = fg->builder().CreateAdd(args_.args()[0]->generate(fg),
                                        fg->sizeOf(fg->typeHelper().pointer()));
    return fg->builder().CreateCall(fg->generator()->runTime().alloc(), size, "alloc");
}

}  // namespace EmojicodeCompiler
