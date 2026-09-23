//
//  ASTVariables_CG.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 03/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTInitialization.hpp"
#include "ASTMemory.hpp"
#include "ASTVariables.hpp"
#include "Generation/RunTimeHelper.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Types/ValueType.hpp"

namespace EmojicodeCompiler {

Value* AccessesAnyVariable::instanceVariablePointer(FunctionCodeGenerator *fg) const {
    assert(inInstanceScope());
    return fg->instanceVariablePointer(id());
}

Value* AccessesAnyVariable::managementValue(FunctionCodeGenerator *fg) const {
    if (inInstanceScope()) {
        llvm::Value *objectPointer = instanceVariablePointer(fg);
        if (!fg->isManagedByReference(variableType_)) {
            objectPointer = fg->builder().CreateLoad(fg->instanceVariableType(id()), objectPointer);
        }
        return objectPointer;
    }

    auto &var = fg->scoper().getVariable(id());
    return fg->isManagedByReference(variableType_) ? var.ptr : fg->builder().CreateLoad(var.type, var.ptr);
}

void AccessesAnyVariable::release(FunctionCodeGenerator *fg) const {
    fg->release(managementValue(fg), variableType_);
}

void AccessesAnyVariable::setTbaaMetadata(FunctionCodeGenerator *fg, llvm::Instruction *storeOrLoad) const {
    if (!inInstanceScope() || !fg->typeHelper().shouldAddTbaa(variableType_)) return;
    auto varTbaa = fg->typeHelper().tbaaNodeFor(variableType_, false);
    auto structTbaa = fg->typeHelper().tbaaNodeFor(fg->calleeType(), true);
    auto accessTag = fg->typeHelper().mdBuilder()->createTBAAAccessTag(structTbaa, varTbaa, id(), 0);
    storeOrLoad->setMetadata(llvm::LLVMContext::MD_tbaa, accessTag);
}

Value* ASTGetVariable::generate(FunctionCodeGenerator *fg) const {
    if (inInstanceScope()) {
        auto ptr = instanceVariablePointer(fg);
        if (reference_) {
            return ptr;
        }
        auto val = fg->builder().CreateLoad(fg->instanceVariableType(id()), ptr);
        setTbaaMetadata(fg, val);
        if (expressionType().isManaged()) {
            fg->retain(fg->isManagedByReference(expressionType()) ? ptr : val, expressionType());
            // We cannot provide the variable address for releasing as another part of the statement could modify the
            // variable and thus might contain a different value when releasing.
            handleResult(fg, val);
        }
        return val;
    }

    auto &localVariable = fg->scoper().getVariable(id());
    if (reference_) {
        return localVariable.ptr;
    }

    auto val = fg->builder().CreateLoad(localVariable.type, localVariable.ptr);
    setTbaaMetadata(fg, val);
    if (!returned_ && !isTemporary() && expressionType().isManaged()) {
        fg->retain(fg->isManagedByReference(expressionType()) ? localVariable.ptr : val, expressionType());
        handleResult(fg, val);  // See above
    }
    return val;
}

void ASTVariableInit::generate(FunctionCodeGenerator *fg) const {
    if (auto init = std::dynamic_pointer_cast<ASTInitialization>(expr_)) {
        if (init->initType() == ASTInitialization::InitType::ValueType) {
            init->setDestination(variablePointer(fg));
            expr_->generate(fg);
            return;
        }
    }

    generateAssignment(fg);
}

llvm::Value* ASTVariableInit::variablePointer(EmojicodeCompiler::FunctionCodeGenerator *fg) const {
    if (declare_) {
        auto type = fg->typeHelper().llvmTypeFor(expr_->expressionType());
        auto varPtr = fg->createEntryAlloca(type, utf8(name()));
        fg->scoper().getVariable(id()) = CGVariable(varPtr, type);
        return varPtr;
    }

    if (inInstanceScope()) {
        return instanceVariablePointer(fg);
    }

    return fg->scoper().getVariable(id()).ptr;
}

void ASTVariableDeclaration::generate(FunctionCodeGenerator *fg) const {
    auto type = fg->typeHelper().llvmTypeFor(type_->type());
    auto alloca = fg->createEntryAlloca(type, utf8(varName_));
    fg->scoper().getVariable(id_) = CGVariable(alloca, type);

    if (type_->type().type() == TypeType::Optional) {
        fg->builder().CreateStore(fg->buildSimpleOptionalWithoutValue(type_->type()), alloca);
    }
}

void ASTVariableAssignment::generateAssignment(FunctionCodeGenerator *fg) const {
    auto val = expr_->generate(fg);
    if (wasInitialized_ && variableType().isManaged()) {
        release(fg);
    }
    auto variablePtr = variablePointer(fg);
    setTbaaMetadata(fg, fg->builder().CreateStore(val, variablePtr));
}

void ASTConstantVariable::generateAssignment(FunctionCodeGenerator *fg) const {
    fg->setVariable(id(), expr_->generate(fg), utf8(name()));
}

Value* ASTIsOnlyReference::generate(FunctionCodeGenerator *fg) const {
    Value *val;
    if (inInstanceScope()) {
        val = fg->builder().CreateLoad(fg->instanceVariableType(id()), instanceVariablePointer(fg));
    }
    else {
        auto &localVariable = fg->scoper().getVariable(id());
        val = fg->builder().CreateLoad(localVariable.type, localVariable.ptr);
    }
    return fg->builder().CreateCall(fg->generator()->runTime().isOnlyReference(), val);
}

}  // namespace EmojicodeCompiler
