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
                fg->createIf(fg->isErrorSet(errorPointer()), [&] { fg->freeOwnedDescription(gargs); });
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

    // The receiver is allocated before its arguments are evaluated, but only the initializer call below, which is
    // never reached if evaluating an argument reraises, transfers ownership of it (and of any dynamically allocated
    // generic argument descriptions). Register both as pending (see FunctionCodeGenerator::addPendingReceiver()) so
    // they are released, without deinitializing the receiver’s uninitialized fields, if that happens, while still
    // surviving checkpoints argument evaluation may hit that must not assume this call has been reached (e.g.
    // short-circuiting 🤝/👐). Disarm the registrations once the initializer call is reached.
    //
    // createPendingPointerAlloca() initializes the tracking variable to null in the entry block itself: this
    // construction may sit inside a short-circuiting operand that never runs, in which case a later checkpoint
    // reads the variable without the store below, immediately after it, ever having executed.
    llvm::Value *pendingObjVar = nullptr;
    if (!stackInit) {
        pendingObjVar = fg->createPendingPointerAlloca();
        fg->builder().CreateStore(obj, pendingObjVar);
        fg->addPendingReceiver(pendingObjVar);
    }
    llvm::Value *pendingGArgsVar = nullptr;
    if (gArgsDescs != nullptr && !llvm::isa<llvm::Constant>(gArgsDescs)) {
        pendingGArgsVar = fg->createPendingPointerAlloca();
        fg->builder().CreateStore(fg->builder().CreateExtractValue(gArgsDescs, { 0 }), pendingGArgsVar);
        fg->addPendingRawAllocation(pendingGArgsVar);
    }

    auto suppl = gArgsDescs != nullptr ? std::vector<llvm::Value*> { gArgsDescs } : std::vector<llvm::Value*>();
    auto result = CallCodeGenerator(fg, CallType::StaticDispatch).generate(obj, type, args, function, errorPointer,
                                                                           suppl);

    if (pendingObjVar != nullptr) {
        fg->builder().CreateStore(llvm::ConstantPointerNull::get(fg->typeHelper().pointer()), pendingObjVar);
    }
    if (pendingGArgsVar != nullptr) {
        fg->builder().CreateStore(llvm::ConstantPointerNull::get(fg->typeHelper().pointer()), pendingGArgsVar);
    }
    if (!stackInit && errorPointer != nullptr && function->owner() != type.klass()) {
        // An inherited initializer (the class does not declare its own and directly uses a superclass's) releases
        // the fields it initialized itself, but only releases the receiver if the object's runtime class info is
        // its own owner's, which lets a subclass's explicit superinitializer call keep ownership of that release.
        // That never matches here, as obj's class info is the class actually being instantiated: release obj here.
        fg->createIf(fg->isErrorSet(errorPointer), [&] {
            fg->builder().CreateCall(fg->generator()->runTime().releaseWithoutDeinit(), obj);
        });
    }
    return result;
}

Value* ASTInitialization::generateMemoryAllocation(FunctionCodeGenerator *fg) const {
    auto size = fg->builder().CreateAdd(args_.args()[0]->generate(fg),
                                        fg->sizeOf(fg->typeHelper().pointer()));
    return fg->builder().CreateCall(fg->generator()->runTime().alloc(), size, "alloc");
}

}  // namespace EmojicodeCompiler
