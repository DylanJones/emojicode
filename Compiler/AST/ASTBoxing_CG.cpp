//
//  ASTBoxing_CG.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 03/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTBoxing.hpp"
#include "ASTInitialization.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Generation/ProtocolsTableGenerator.hpp"
#include "Generation/RunTimeHelper.hpp"
#include "Types/Protocol.hpp"

namespace EmojicodeCompiler {

Value* ASTUpcast::generate(FunctionCodeGenerator *fg) const {
    return fg->builder().CreateBitCast(expr_->generate(fg), fg->typeHelper().llvmTypeFor(toType_));
}

ASTBoxing::ASTBoxing(std::shared_ptr<ASTExpr> expr, const SourcePosition &p, const Type &exprType)
    : ASTUnaryMFForwarding(std::move(expr), p) {
    setExpressionType(exprType);
    if (auto init = std::dynamic_pointer_cast<ASTInitialization>(expr_)) {
        init_ = init->initType() == ASTInitialization::InitType::ValueType;
    }
}

Value* ASTRebox::generate(FunctionCodeGenerator *fg) const {
    if (expressionType().boxedFor().type() == TypeType::Something) {
        auto box = expr_->generate(fg);
        auto pct = fg->typeHelper().protocolConformance();
        auto pc = fg->builder().CreateExtractValue(box, 0);
        auto bi = fg->builder().CreateLoad(fg->typeHelper().pointer(),
                                           fg->builder().CreateConstInBoundsGEP2_32(pct, pc, 0, 2));
        return fg->builder().CreateInsertValue(box, bi, 0);
    }

    auto box = getAllocaTheBox(fg);
    auto protocolRtti = expressionType().boxedFor().protocol()->rtti();
    auto boxInfo = fg->builder().CreateLoad(fg->typeHelper().pointer(), fg->buildGetBoxInfoPtr(box));
    auto conformance = fg->buildFindProtocolConformance(box, boxInfo, protocolRtti);
    fg->builder().CreateStore(conformance, fg->buildGetBoxInfoPtr(box));
    return fg->builder().CreateLoad(fg->typeHelper().box(), box);
}

Value* ASTBoxing::getBoxValuePtr(Value *box, FunctionCodeGenerator *fg) const {
    return fg->buildGetBoxValuePtr(box);
}

Value* ASTBoxing::getSimpleOptional(Value *value, FunctionCodeGenerator *fg) const {
    return fg->buildSimpleOptionalWithValue(value, expressionType());
}

Value* ASTBoxing::getSimpleOptionalWithoutValue(FunctionCodeGenerator *fg) const {
    return fg->buildSimpleOptionalWithoutValue(expressionType());
}

Value* ASTBoxing::getAllocaTheBox(FunctionCodeGenerator *fg) const {
    auto box = fg->createEntryAlloca(fg->typeHelper().box());
    fg->builder().CreateStore(expr_->generate(fg), box);
    return box;
}

Value* ASTBoxing::getGetValueFromBox(Value *box, FunctionCodeGenerator *fg) const {
    auto containedType = expr_->expressionType().unboxed().unoptionalized();
    auto type = fg->typeHelper().llvmTypeFor(containedType);
    if (fg->typeHelper().isRemote(containedType)) {
        auto ptrPtr = fg->buildGetBoxValuePtr(box);
        return fg->builder().CreateLoad(type, fg->builder().CreateLoad(fg->typeHelper().pointer(), ptrPtr));
    }
    return fg->builder().CreateLoad(type, getBoxValuePtr(box, fg));
}

void ASTBoxing::releaseRemoteAllocationIfTaken(Value *box, FunctionCodeGenerator *fg) const {
    if (isTemporary() || !fg->typeHelper().isRemote(expr_->expressionType().unboxed().unoptionalized())) {
        return;
    }
    auto ptr = fg->typeHelper().pointer();
    auto remote = fg->builder().CreateLoad(ptr, fg->buildGetBoxValuePtrAfter(box, ptr, ptr));
    fg->builder().CreateCall(fg->generator()->runTime().releaseWithoutDeinit(), remote);
}

void ASTBoxing::valueTypeInit(FunctionCodeGenerator *fg, Value *destination) const {
    auto init = std::dynamic_pointer_cast<ASTInitialization>(expr_);
    init->setDestination(destination);
    expr_->generate(fg);
}

Value* ASTBoxToSimple::generate(FunctionCodeGenerator *fg) const {
    auto box = getAllocaTheBox(fg);
    auto value = getGetValueFromBox(box, fg);
    releaseRemoteAllocationIfTaken(box, fg);
    return value;
}

Value* ASTBoxToSimpleOptional::generate(FunctionCodeGenerator *fg) const {
    auto box = getAllocaTheBox(fg);

    auto hasNoValue = fg->buildHasNoValueBoxPtr(box);
    return fg->createIfElsePhi(hasNoValue, [this, fg]() {
        return getSimpleOptionalWithoutValue(fg);
    }, [this, box, fg]() {
        auto value = getGetValueFromBox(box, fg);
        releaseRemoteAllocationIfTaken(box, fg);
        return getSimpleOptional(value, fg);
    });
}

Value* ASTSimpleToSimpleOptional::generate(FunctionCodeGenerator *fg) const {
    return getSimpleOptional(expr_->generate(fg), fg);
}

Value* ASTSimpleToBox::generate(FunctionCodeGenerator *fg) const {
    auto box = fg->createEntryAlloca(fg->typeHelper().box());
    remoteObject_ = nullptr;
    if (isValueTypeInit()) {
        setBoxInfo(box, fg);
        valueTypeInit(fg, buildStoreAddress(box, fg));
    }
    else {
        getPutValueIntoBox(box, expr_->generate(fg), fg);
    }
    // The value is released as a temporary, but a heap object storing it is not. It is released after the value,
    // which is in it.
    if (remoteObject_ != nullptr) {
        if (auto objectVariable = temporaryRemoteObjectVariable(fg)) {
            fg->builder().CreateStore(remoteObject_, objectVariable);
            fg->addTemporaryRemoteObject(objectVariable);
        }
    }
    return fg->builder().CreateLoad(fg->typeHelper().box(), box);
}

Value* ASTSimpleOptionalToBox::generate(FunctionCodeGenerator *fg) const {
    auto value = expr_->generate(fg);
    auto hasNoValue = fg->buildOptionalHasNoValue(value, expr_->expressionType());
    // An object is only allocated if there is a value.
    auto objectVariable = temporaryRemoteObjectVariable(fg);
    if (objectVariable != nullptr) {
        fg->builder().CreateStore(llvm::ConstantPointerNull::get(fg->typeHelper().pointer()), objectVariable);
    }

    auto result = fg->createIfElsePhi(hasNoValue, [&] {
        return fg->buildBoxWithoutValue();
    }, [&] {
        auto box = fg->createEntryAlloca(fg->typeHelper().box());
        getPutValueIntoBox(box, fg->buildGetOptionalValue(value, expr_->expressionType()), fg);
        if (objectVariable != nullptr) {
            fg->builder().CreateStore(remoteObject_, objectVariable);
        }
        return fg->builder().CreateLoad(fg->typeHelper().box(), box);
    });
    if (objectVariable != nullptr) {
        fg->addTemporaryRemoteObject(objectVariable);
    }
    return result;
}

Value* ASTToBox::temporaryRemoteObjectVariable(FunctionCodeGenerator *fg) const {
    auto containedType = expr_->expressionType().unboxed().unoptionalized();
    if (!fg->typeHelper().isRemote(containedType) || allocatesOnStack() || !producesTemporaryObject()) {
        return nullptr;
    }
    return fg->createEntryAlloca(fg->typeHelper().pointer());
}

Value* ASTToBox::buildStoreAddress(Value *box, FunctionCodeGenerator *fg) const {
    auto containedType = expr_->expressionType().unboxed().unoptionalized();
    if (fg->typeHelper().isRemote(containedType)) {
        auto mngType = fg->typeHelper().managable(fg->typeHelper().llvmTypeFor(containedType));
        remoteObject_ = allocate(fg, mngType);
        return fg->buildSetRemoteBoxObject(box, mngType, remoteObject_);
    }
    return getBoxValuePtr(box, fg);
}

void ASTToBox::getPutValueIntoBox(Value *box, Value *value, FunctionCodeGenerator *fg) const {
    setBoxInfo(box, fg);
    fg->builder().CreateStore(value, buildStoreAddress(box, fg));
}

void ASTToBox::setBoxInfo(Value *box, FunctionCodeGenerator *fg) const {
    auto boxedFor = expressionType().boxedFor();
    if (boxedFor.type() == TypeType::Protocol || boxedFor.type() == TypeType::MultiProtocol) {
        llvm::Value *table;
        if (boxedFor.type() == TypeType::MultiProtocol) {
            table = ProtocolsTableGenerator(fg->generator()).multiprotocol(boxedFor, expr_->expressionType());
        }
        else {
            table = expr_->expressionType().typeDefinition()->protocolTableFor(boxedFor);
        }
        fg->builder().CreateStore(table, fg->buildGetBoxInfoPtr(box));
        return;
    }
    auto boxInfo = fg->boxInfoFor(expr_->expressionType().unoptionalized());
    fg->builder().CreateStore(boxInfo, fg->buildGetBoxInfoPtr(box));
}

Value* ASTStoreTemporarily::generate(FunctionCodeGenerator *fg) const {
    auto store = fg->createEntryAlloca(fg->typeHelper().llvmTypeFor(expr_->expressionType()), "temp");
    if (isValueTypeInit()) {
        valueTypeInit(fg, store);
    }
    else {
        fg->builder().CreateStore(expr_->generate(fg), store);
    }
    return store;
}

Value* ASTBoxReferenceToReference::generate(FunctionCodeGenerator *fg) const {
    auto containedType = expr_->expressionType().unboxed().unoptionalized();
    if (fg->typeHelper().isRemote(containedType)) {
        auto box = expr_->generate(fg);
        if (mutated_) {  // A mutation must not change copies of the box, which share the object storing the value.
            fg->makeRemoteBoxValueUnique(box, containedType);
        }
        return fg->builder().CreateLoad(fg->typeHelper().pointer(), fg->buildGetBoxValuePtr(box));
    }
    return fg->buildGetBoxValuePtr(expr_->generate(fg));
}

void ASTBoxReferenceToReference::mutateReference(ExpressionAnalyser *analyser) {
    mutated_ = true;
    expr_->mutateReference(analyser);
}

Value* ASTDereference::generate(FunctionCodeGenerator *fg) const {
    auto ptr = expr_->generate(fg);
    auto val = fg->builder().CreateLoad(fg->typeHelper().llvmTypeFor(expressionType()), ptr);
    if (expressionType().isManaged()) {
        fg->retain(fg->isManagedByReference(expressionType()) ? ptr : val, expressionType());
    }
    return handleResult(fg, val, ptr);
}

void ASTDereference::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) {
    analyser->take(expr_.get());
}

Value* ASTBoxReferenceToSimple::generate(FunctionCodeGenerator *fg) const {
    auto box = expr_->generate(fg);
    auto containedType = expr_->expressionType().unboxed().unoptionalized();
    llvm::Value *valuePtr;

    if (fg->typeHelper().isRemote(containedType)) {
        valuePtr = fg->builder().CreateLoad(fg->typeHelper().pointer(), fg->buildGetBoxValuePtr(box));
    }
    else {
        valuePtr = getBoxValuePtr(box, fg);
    }

    auto val = fg->builder().CreateLoad(fg->typeHelper().llvmTypeFor(containedType), valuePtr);
    if (expressionType().isManaged()) {
        fg->retain(fg->isManagedByReference(expressionType()) ? valuePtr : val, expressionType());
    }
    return handleResult(fg, val, valuePtr);
}

}  // namespace EmojicodeCompiler
