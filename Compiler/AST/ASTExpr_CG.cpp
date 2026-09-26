//
//  ASTExpr_CG.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 03/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTExpr.hpp"
#include "ASTTypeExpr.hpp"
#include "Generation/CallCodeGenerator.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Generation/LLVMTypeHelper.hpp"
#include "Types/Class.hpp"

namespace EmojicodeCompiler {

llvm::Value* ASTExpr::handleResult(FunctionCodeGenerator *fg, llvm::Value *result, llvm::Value *vtReference) const {
    if (producesTemporaryObject()) {
        if (fg->isManagedByReference(expressionType())) {
            if (vtReference == nullptr) {
                assert(result != nullptr);
                auto temp = fg->createEntryAlloca(result->getType());
                fg->builder().CreateStore(result, temp);
                fg->addTemporaryObject(temp, expressionType());
            }
            else {
                fg->addTemporaryObject(vtReference, expressionType());
            }
        }
        else {
            assert(result != nullptr);
            fg->addTemporaryObject(result, expressionType());
        }
    }
    return result;
}

bool ASTExpr::producesTemporaryObject() const {
    return isTemporary_ && expressionType().isManaged() && !expressionType().isReference();
}

Value* ASTSizeOf::generate(FunctionCodeGenerator *fg) const {
    if (LLVMTypeHelper::isErased(type_->type())) {  // The size of the type T stands for.
        return fg->buildValueSize(fg->buildTypeDescriptionEntry(type_->type()));
    }
    return fg->sizeOf(fg->typeHelper().llvmTypeFor(type_->type()));
}

Value* ASTCallableCall::generate(FunctionCodeGenerator *fg) const {
    auto callable = callable_->generate(fg);
    auto type = callable_->expressionType();
    if (type.isCCallable()) {
        return generateCCall(fg, callable, type);
    }

    auto returnType = fg->typeHelper().llvmTypeFor(type.returnType());
    std::vector<llvm::Type *> argTypes { fg->typeHelper().pointer() };
    std::transform(type.parameters(), type.parametersEnd(), std::back_inserter(argTypes), [fg](auto &arg) {
        return fg->typeHelper().llvmTypeFor(arg);
    });
    if (isErrorProne()) {
        argTypes.emplace_back(fg->typeHelper().pointer());
    }
    auto functionType = llvm::FunctionType::get(returnType, argTypes, false);

    auto function = fg->builder().CreateExtractValue(callable, 0);
    std::vector<llvm::Value *> args{ fg->builder().CreateExtractValue(callable, 1) };
    for (auto &arg : args_.args()) {
        args.emplace_back(arg->generate(fg));
    }
    if (isErrorProne()) {
        args.emplace_back(errorPointer());
    }
    return handleResult(fg, fg->builder().CreateCall(functionType, function, args));
}

Value* ASTCallableCall::generateCCall(FunctionCodeGenerator *fg, llvm::Value *function, const Type &type) const {
    std::vector<llvm::Type *> argTypes;
    std::vector<llvm::Value *> args;
    for (size_t i = 0; i < type.parametersCount(); i++) {
        argTypes.emplace_back(fg->typeHelper().llvmTypeFor(type.parameters()[i]));
        args.emplace_back(args_.args()[i]->generate(fg));
    }
    auto functionType = llvm::FunctionType::get(fg->typeHelper().llvmTypeFor(type.returnType()), argTypes, false);
    auto call = fg->builder().CreateCall(functionType, function, args);
    auto triple = llvm::Triple(fg->generator()->module()->getTargetTriple());
    for (size_t i = 0; i < type.parametersCount(); i++) {
        auto attribute = cExtensionAttribute(type.parameters()[i], triple);
        if (attribute != llvm::Attribute::None) {
            call->addParamAttr(i, attribute);
        }
    }
    auto attribute = cExtensionAttribute(type.returnType(), triple);
    if (attribute != llvm::Attribute::None) {
        call->addRetAttr(attribute);
    }
    return handleResult(fg, call);
}

}  // namespace EmojicodeCompiler
