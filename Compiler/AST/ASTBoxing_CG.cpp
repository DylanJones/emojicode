//
//  ASTBoxing_CG.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 03/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTBoxing.hpp"
#include <algorithm>
#include "ASTInitialization.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Generation/LLVMTypeHelper.hpp"
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
    auto box = expr_->generate(fg);
    if (expressionType().unboxedType() != TypeType::Optional) {
        return rebox(box, fg);
    }
    // A box without a value has neither box info nor conformance to rebox.
    return fg->createIfElsePhi(fg->buildHasNoValueBox(box), [box]() { return box; },
                               [this, box, fg]() { return rebox(box, fg); });
}

Value* ASTRebox::rebox(Value *box, FunctionCodeGenerator *fg) const {
    auto &from = expr_->expressionType();
    auto &target = expressionType().boxedFor();
    if (target.type() != TypeType::Something && target.type() != TypeType::Protocol &&
        target.type() != TypeType::MultiProtocol) {
        // A box for a class, 🔵 or a callable (e.g. a generic variable with such a constraint) carries the value box
        // info itself; the analyser has already established that the value is of a subtype.
        return box;
    }
    auto boxInfo = fg->builder().CreateExtractValue(box, 0);
    if (expressionType().boxedFor().type() == TypeType::Something) {
        return fg->builder().CreateInsertValue(box, fg->buildGetValueBoxInfo(boxInfo, from), 0);
    }

    Value *boxPtr = nullptr, *valueBoxInfo = nullptr;
    auto conformanceTo = [&](Protocol *protocol) -> Value* {
        // A box for a multiprotocol already has the conformance to each of its protocols.
        if (auto index = FunctionCodeGenerator::multiprotocolIndex(from, protocol)) {
            return fg->buildGetBoxConformance(boxInfo, from, *index);
        }
        if (boxPtr == nullptr) {
            boxPtr = fg->createEntryAlloca(fg->typeHelper().box());
            fg->builder().CreateStore(box, boxPtr);
            valueBoxInfo = fg->buildGetValueBoxInfo(boxInfo, from);
        }
        return fg->buildFindProtocolConformance(boxPtr, valueBoxInfo, protocol->rtti());
    };

    auto &to = expressionType().boxedFor();
    if (to.type() == TypeType::MultiProtocol) {
        // The box points to a table of the conformances to the protocols of the multiprotocol.
        auto table = fg->buildMultiprotocolTable(to.protocols(), [&](size_t i) {
            return conformanceTo(to.protocols()[i].protocol());
        });
        return fg->builder().CreateInsertValue(box, table, 0);
    }
    return fg->builder().CreateInsertValue(box, conformanceTo(to.protocol()), 0);
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
    auto remote = fg->builder().CreateLoad(ptr, fg->buildGetRemoteBoxObjectPtr(box));
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
    Value *remoteObject;
    if (isValueTypeInit()) {
        setBoxInfo(box, fg);
        valueTypeInit(fg, buildStoreAddress(box, fg, &remoteObject));
    }
    else {
        remoteObject = getPutValueIntoBox(box, expr_->generate(fg), fg);
    }
    // The value is released as a temporary, but a heap object storing it is not. It is released after the value,
    // which is in it.
    if (remoteObject != nullptr) {
        if (auto objectVariable = temporaryRemoteObjectVariable(fg)) {
            fg->builder().CreateStore(remoteObject, objectVariable);
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
        auto remoteObject = getPutValueIntoBox(box, fg->buildGetOptionalValue(value, expr_->expressionType()), fg);
        if (objectVariable != nullptr) {
            fg->builder().CreateStore(remoteObject, objectVariable);
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

Value* ASTToBox::buildStoreAddress(Value *box, FunctionCodeGenerator *fg, Value **remoteObject) const {
    auto containedType = expr_->expressionType().unboxed().unoptionalized();
    if (fg->typeHelper().isRemote(containedType)) {
        auto mngType = fg->typeHelper().managable(fg->typeHelper().llvmTypeFor(containedType));
        *remoteObject = allocate(fg, mngType);
        return fg->buildSetRemoteBoxObject(box, mngType, *remoteObject);
    }
    *remoteObject = nullptr;
    return getBoxValuePtr(box, fg);
}

Value* ASTToBox::getPutValueIntoBox(Value *box, Value *value, FunctionCodeGenerator *fg) const {
    setBoxInfo(box, fg);
    Value *remoteObject;
    fg->builder().CreateStore(value, buildStoreAddress(box, fg, &remoteObject));
    return remoteObject;
}

void ASTToBox::setBoxInfo(Value *box, FunctionCodeGenerator *fg) const {
    auto boxedFor = expressionType().boxedFor();
    // ASTSimpleOptionalToBox only calls this if there is a value, which is of the type the optional contains.
    auto valueType = expr_->expressionType().unoptionalized();
    if (boxedFor.type() == TypeType::Protocol || boxedFor.type() == TypeType::MultiProtocol) {
        llvm::Value *table;
        if (boxedFor.type() == TypeType::MultiProtocol) {
            table = ProtocolsTableGenerator(fg->generator()).multiprotocol(boxedFor, valueType);
        }
        else {
            table = valueType.typeDefinition()->protocolTableFor(boxedFor);
        }
        fg->builder().CreateStore(table, fg->buildGetBoxInfoPtr(box));
        return;
    }
    auto boxInfo = fg->boxInfoFor(valueType);
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
    if (LLVMTypeHelper::isErasedReference(expressionType())) {
        return fg->buildErasedReference(store);
    }
    return store;
}

Value* ASTBoxReferenceToReference::generate(FunctionCodeGenerator *fg) const {
    auto containedType = expr_->expressionType().unboxed().unoptionalized();
    auto reference = expr_->generate(fg);
    auto address = fg->buildErasedReferenceAddress(reference);
    auto entry = fg->builder().CreateExtractValue(reference, 1);
    // A reference to a value in memory refers to the value itself.
    return fg->createIfElsePhi(fg->builder().CreateIsNull(entry), [&]() -> Value* {
        if (fg->typeHelper().isRemote(containedType)) {
            if (mutated_) {  // A mutation must not change copies of the box, which share the object storing the value.
                fg->makeRemoteBoxValueUnique(address, containedType);
            }
            return fg->builder().CreateLoad(fg->typeHelper().pointer(), fg->buildGetBoxValuePtr(address));
        }
        return fg->buildGetBoxValuePtr(address);
    }, [&] { return address; });
}

void ASTBoxReferenceToReference::mutateReference(ExpressionAnalyser *analyser) {
    mutated_ = true;
    expr_->mutateReference(analyser);
}

Value* ASTDereference::generate(FunctionCodeGenerator *fg) const {
    if (LLVMTypeHelper::isErasedReference(expr_->expressionType())) {
        return handleResult(fg, fg->buildLoadErased(expr_->generate(fg), expressionType()));
    }
    auto ptr = expr_->generate(fg);
    auto val = fg->builder().CreateLoad(fg->typeHelper().llvmTypeFor(expressionType()), ptr);
    if (expressionType().isManaged() && fg->isManagedByReference(expressionType())) {
        // The temporary owns a copy: ptr can point into storage that is moved while the copy is alive, e.g. when
        // it is appended to the list it was read from.
        auto copy = fg->createEntryAlloca(val->getType());
        fg->builder().CreateStore(val, copy);
        fg->retain(copy, expressionType());
        return handleResult(fg, val, copy);
    }
    if (expressionType().isManaged()) {
        fg->retain(val, expressionType());
    }
    return handleResult(fg, val, ptr);
}

void ASTDereference::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) {
    analyser->take(expr_.get());
}

Value* ASTBoxReferenceToSimple::generate(FunctionCodeGenerator *fg) const {
    auto reference = expr_->generate(fg);
    auto box = fg->buildErasedReferenceAddress(reference);
    auto entry = fg->builder().CreateExtractValue(reference, 1);
    auto containedType = expr_->expressionType().unboxed().unoptionalized();

    // A reference to a value in memory refers to the value itself.
    auto valuePtr = fg->createIfElsePhi(fg->builder().CreateIsNull(entry), [&]() -> Value* {
        if (fg->typeHelper().isRemote(containedType)) {
            return fg->builder().CreateLoad(fg->typeHelper().pointer(), fg->buildGetBoxValuePtr(box));
        }
        return getBoxValuePtr(box, fg);
    }, [&] { return box; });

    auto val = fg->builder().CreateLoad(fg->typeHelper().llvmTypeFor(containedType), valuePtr);
    if (expressionType().isManaged() && fg->isManagedByReference(expressionType())) {
        // The temporary owns a copy, as valuePtr can point into storage that is changed while the copy is alive.
        auto copy = fg->createEntryAlloca(val->getType());
        fg->builder().CreateStore(val, copy);
        fg->retain(copy, expressionType());
        return handleResult(fg, val, copy);
    }
    if (expressionType().isManaged()) {
        fg->retain(val, expressionType());
    }
    return handleResult(fg, val, valuePtr);
}

}  // namespace EmojicodeCompiler
