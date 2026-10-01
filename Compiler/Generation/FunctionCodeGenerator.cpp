//
//  FunctionCodeGenerator.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 29/07/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "FunctionCodeGenerator.hpp"
#include "AST/ASTStatements.hpp"
#include "Compiler.hpp"
#include "RunTimeHelper.hpp"
#include "Functions/Function.hpp"
#include "Generation/CallCodeGenerator.hpp"
#include "Package/Package.hpp"
#include "Types/Class.hpp"
#include "Types/Protocol.hpp"
#include "TypeDescriptionGenerator.hpp"
#include "Types/ValueType.hpp"
#include "Types/TypeContext.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <stdexcept>

namespace EmojicodeCompiler {

FunctionCodeGenerator::FunctionCodeGenerator(Function *function, llvm::Function *llvmFunc, CodeGenerator *generator)
    : fn_(function), function_(llvmFunc), scoper_(function->variableCount()),
      generator_(generator), builder_(generator->context()), typeContext_(std::make_unique<TypeContext>(fn_->typeContext())) {}

FunctionCodeGenerator::FunctionCodeGenerator(llvm::Function *llvmFunc, CodeGenerator *generator,
                                             std::unique_ptr<TypeContext> tc)
    : fn_(nullptr), function_(llvmFunc), scoper_(0), generator_(generator), builder_(generator->context()),
      typeContext_(std::move(tc)) {}

void FunctionCodeGenerator::generate() {
    createEntry();

    declareArguments(function_);

    fn_->ast()->generate(this);

    // Invalid IR must not reach the optimizer, which assumes valid IR and may hide the problem or miscompile.
    std::string problems;
    llvm::raw_string_ostream problemsStream(problems);
    if (llvm::verifyFunction(*function_, &problemsStream)) {
        auto ows = function()->owner() != nullptr ? function()->owner()->type().toString(fn_->typeContext()) : "";
        throw std::logic_error("Invalid LLVM IR was generated for " + ows + utf8(fn_->name()) + " (" +
                               function_->getName().str() + "):\n" + problems);
    }
}

void FunctionCodeGenerator::createEntry() {
    auto basicBlock = llvm::BasicBlock::Create(ctx(), "entry", function_);
    builder_.SetInsertPoint(basicBlock);
}

Compiler* FunctionCodeGenerator::compiler() const {
    return generator()->compiler();
}

void FunctionCodeGenerator::declareArguments(llvm::Function *function) {
    unsigned int i = 0;
    auto it = function->args().begin();
    if (hasThisArgument(fn_)) {
        if (readsTypeGenericArgsFromThis(fn_)) {
            typeMethodGenericArgs_ = buildGetGenericArgsFromTypeValue(&*it);
        }
        (it++)->setName("this");
    }
    for (auto &arg : fn_->parameters()) {
        auto &llvmArg = *(it++);
        setVariable(i++, &llvmArg);
        llvmArg.setName(utf8(arg.name));
    }

    if (takesInitializerGenericArgs(fn_)) {
        auto llvmArg = (it++);
        llvmArg->setName("genericArgs");
        builder().CreateStore(llvmArg, genericArgsPtr());
    }

    if (takesTypeGenericArgs(fn_)) {
        auto llvmArg = (it++);
        llvmArg->setName("genericArgs");
        typeMethodGenericArgs_ = llvmArg;
    }

    if (!fn_->genericParameters().empty()) {
        auto llvmArg = (it++);
        llvmArg->setName("fnGenericArgs");
        functionGenericArgs_ = llvmArg;
    }

    if (fn_->errorProne()) {
        it->setName("error");
    }
}

const Type& FunctionCodeGenerator::calleeType() const {
    return typeContext_->calleeType();
}

const SourcePosition& FunctionCodeGenerator::position() const {
    return fn_->position();
}

void FunctionCodeGenerator::setVariable(size_t id, llvm::Value *value, const llvm::Twine &name) {
    auto alloca = createEntryAlloca(value->getType(), name);
    builder().CreateStore(value, alloca);
    scoper_.getVariable(id) = CGVariable(alloca, value->getType());
}

void FunctionCodeGenerator::buildErrorReturn() {
    if (llvmReturnType()->isVoidTy()) {
        builder().CreateRetVoid();
    }
    else {
        builder().CreateRet(llvm::UndefValue::get(llvmReturnType()));
    }
}

llvm::Value* FunctionCodeGenerator::sizeOf(llvm::Type *type) {
    auto one = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx()), 1);
    auto sizeg = builder().CreateGEP(type, llvm::ConstantPointerNull::get(typeHelper().pointer()), one);
    return builder().CreatePtrToInt(sizeg, llvm::Type::getInt64Ty(ctx()));
}

Value* FunctionCodeGenerator::buildGetBoxInfoPtr(Value *box) {
    return builder().CreateConstInBoundsGEP2_32(typeHelper().box(), box, 0, 0);
}

llvm::Value* FunctionCodeGenerator::buildGetClassInfoPtrFromObject(Value *object) {
    return builder().CreateConstInBoundsGEP2_32(typeHelper().someobject(), object, 0, 1); // classInfo*
}

llvm::Value* FunctionCodeGenerator::buildGetClassInfoFromObject(llvm::Value *object) {
    return builder().CreateLoad(typeHelper().pointer(), buildGetClassInfoPtrFromObject(object), "info");
}

llvm::Value* FunctionCodeGenerator::buildGetClassInfoFromTypeValue(llvm::Value *typeValue) {
    auto ptr = builder().CreateConstInBoundsGEP2_32(typeHelper().typeDescription(), typeValue, 0, 0);
    return builder().CreateLoad(typeHelper().pointer(), ptr, "info");
}

llvm::Value* FunctionCodeGenerator::buildGetGenericArgsFromTypeValue(llvm::Value *typeValue) {
    return builder().CreateConstInBoundsGEP1_32(typeHelper().typeDescription(), typeValue, 1, "typeGenericArgs");
}

llvm::Value* FunctionCodeGenerator::buildGetBoxConformance(llvm::Value *boxInfo, const Type &type,
                                                           size_t multiprotocolN) {
    if (type.boxedFor().type() == TypeType::MultiProtocol) {
        auto table = typeHelper().multiprotocolConformance(type.boxedFor());
        return builder().CreateLoad(typeHelper().pointer(),
                                    builder().CreateConstInBoundsGEP2_32(table, boxInfo, 0, multiprotocolN));
    }
    assert(type.boxedFor().type() == TypeType::Protocol);
    return boxInfo;
}

std::optional<size_t> FunctionCodeGenerator::multiprotocolIndex(const Type &type, const Protocol *protocol) {
    if (type.boxedFor().type() != TypeType::MultiProtocol) {
        return std::nullopt;
    }
    auto &protocols = type.boxedFor().protocols();
    auto it = std::find_if(protocols.begin(), protocols.end(), [protocol](const Type &t) {
        return t.protocol() == protocol;
    });
    if (it == protocols.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(it - protocols.begin());
}

llvm::Value* FunctionCodeGenerator::buildGetValueBoxInfo(llvm::Value *boxInfo, const Type &type) {
    if (type.boxedFor().type() != TypeType::Protocol && type.boxedFor().type() != TypeType::MultiProtocol) {
        return boxInfo;
    }
    auto conformance = buildGetBoxConformance(boxInfo, type);
    return builder().CreateLoad(typeHelper().pointer(),
                                builder().CreateConstInBoundsGEP2_32(typeHelper().protocolConformance(),
                                                                     conformance, 0, 2));
}

llvm::Value* FunctionCodeGenerator::buildHasNoValueBoxPtr(llvm::Value *box) {
    return builder().CreateIsNull(builder().CreateLoad(typeHelper().pointer(), buildGetBoxInfoPtr(box)));
}

llvm::Value* FunctionCodeGenerator::buildHasNoValueBox(llvm::Value *box) {
    return builder().CreateIsNull(builder().CreateExtractValue(box, 0));
}

Value* FunctionCodeGenerator::buildOptionalHasNoValue(llvm::Value *simpleOptional, const Type &type) {
    if (type.storageType() == StorageType::PointerOptional) {
        return builder().CreateIsNull(simpleOptional);
    }
    auto vf = builder().CreateExtractValue(simpleOptional, 0);
    return builder().CreateICmpEQ(vf, llvm::ConstantInt::getFalse(ctx()));
}

Value* FunctionCodeGenerator::buildOptionalHasValue(llvm::Value *simpleOptional, const Type &type) {
    if (type.storageType() == StorageType::PointerOptional) {
        return builder().CreateIsNotNull(simpleOptional);
    }
    return builder().CreateExtractValue(simpleOptional, 0);
}

Value* FunctionCodeGenerator::buildOptionalHasValuePtr(llvm::Value *simpleOptional, const Type &type) {
    if (type.storageType() == StorageType::PointerOptional) {
        return builder().CreateIsNotNull(simpleOptional);
    }
    auto ptype = typeHelper().llvmTypeFor(type);
    return builder().CreateLoad(llvm::Type::getInt1Ty(ctx()),
                                builder().CreateConstInBoundsGEP2_32(ptype, simpleOptional, 0, 0));
}

Value* FunctionCodeGenerator::buildGetOptionalValuePtr(llvm::Value *simpleOptional, const Type &type) {
    if (type.storageType() == StorageType::PointerOptional) {
        return builder().CreateLoad(typeHelper().pointer(), simpleOptional);
    }
    auto ptype = typeHelper().llvmTypeFor(type);
    return builder().CreateConstInBoundsGEP2_32(ptype, simpleOptional, 0, 1);
}

Value* FunctionCodeGenerator::buildSimpleOptionalWithoutValue(const Type &type) {
    if (type.storageType() == StorageType::PointerOptional) {
        return llvm::Constant::getNullValue(typeHelper().llvmTypeFor(type.optionalType()));
    }
    auto structType = typeHelper().llvmTypeFor(type);
    auto undef = llvm::UndefValue::get(structType);
    return builder().CreateInsertValue(undef, llvm::ConstantInt::getFalse(ctx()), 0);
}

Value* FunctionCodeGenerator::buildBoxWithoutValue() {
    auto undef = llvm::UndefValue::get(typeHelper().box());
    return builder().CreateInsertValue(undef, llvm::Constant::getNullValue(typeHelper().pointer()), 0);
}

Value* FunctionCodeGenerator::buildSimpleOptionalWithValue(llvm::Value *value, const Type &type) {
    if (type.storageType() == StorageType::PointerOptional) {
        return value;
    }
    auto structType = typeHelper().llvmTypeFor(type);
    auto undef = llvm::UndefValue::get(structType);
    auto simpleOptional = builder().CreateInsertValue(undef, value, 1);
    return builder().CreateInsertValue(simpleOptional, llvm::ConstantInt::getTrue(ctx()), 0);
}

Value* FunctionCodeGenerator::buildGetOptionalValue(llvm::Value *value, const Type &type) {
    if (type.storageType() == StorageType::PointerOptional) {
        return value;
    }
    return builder().CreateExtractValue(value, 1);
}

Value* FunctionCodeGenerator::buildGetBoxValuePtr(Value *box) {
    return builder().CreateConstInBoundsGEP2_32(typeHelper().box(), box, 0, 1);
}

llvm::Value* FunctionCodeGenerator::buildGetBoxValuePtrAfter(llvm::Value *box, llvm::Type *llvmType,
                                                             llvm::Type *after) {
    auto val = builder().CreateConstInBoundsGEP2_32(typeHelper().box(), box, 0, 1);
    auto strType = llvm::StructType::get(after, llvmType);
    return builder().CreateConstInBoundsGEP2_32(strType, val, 0, 1);
}

void FunctionCodeGenerator::createIfElseBranchCond(llvm::Value *cond, const std::function<bool()> &then,
                                   const std::function<bool()> &otherwise) {
    auto function = builder().GetInsertBlock()->getParent();
    auto success = llvm::BasicBlock::Create(ctx(), "then", function);
    auto fail = llvm::BasicBlock::Create(ctx(), "else", function);
    auto mergeBlock = llvm::BasicBlock::Create(ctx(), "cont", function);

    builder().CreateCondBr(cond, success, fail);

    builder().SetInsertPoint(success);
    if (then()) {
        builder().CreateBr(mergeBlock);
    }

    builder().SetInsertPoint(fail);
    if (otherwise()) {
        builder().CreateBr(mergeBlock);
    }
    builder().SetInsertPoint(mergeBlock);
}

void FunctionCodeGenerator::createIf(llvm::Value *cond, const std::function<void()> &then) {
    auto function = builder().GetInsertBlock()->getParent();
    auto thenBlock = llvm::BasicBlock::Create(ctx(), "then", function);
    auto cont = llvm::BasicBlock::Create(ctx(), "cont", function);

    builder().CreateCondBr(cond, thenBlock, cont);
    builder().SetInsertPoint(thenBlock);
    then();
    builder().CreateBr(cont);
    builder().SetInsertPoint(cont);
}

void FunctionCodeGenerator::createCountedLoop(llvm::Value *count, const std::function<void(llvm::Value *)> &body) {
    auto entry = builder().GetInsertBlock();
    auto loop = createBlock("loop");
    auto cont = createBlock("loopCont");
    builder().CreateCondBr(builder().CreateICmpSGT(count, int64(0)), loop, cont);
    builder().SetInsertPoint(loop);
    auto index = builder().CreatePHI(builder().getInt64Ty(), 2);
    index->addIncoming(int64(0), entry);
    body(index);
    auto next = builder().CreateAdd(index, int64(1));
    index->addIncoming(next, builder().GetInsertBlock());
    builder().CreateCondBr(builder().CreateICmpSLT(next, count), loop, cont);
    builder().SetInsertPoint(cont);
}

void FunctionCodeGenerator::createForEachValue(llvm::Value *address, llvm::Value *count, llvm::Value *size,
                                               const std::function<void(llvm::Value *)> &body) {
    createCountedLoop(count, [&](llvm::Value *index) {
        body(builder().CreateGEP(builder().getInt8Ty(), address, builder().CreateMul(index, size)));
    });
}

llvm::BasicBlock* FunctionCodeGenerator::createBlock(const llvm::Twine &name) {
    auto function = builder().GetInsertBlock()->getParent();
    return llvm::BasicBlock::Create(ctx(), name, function);
}

void FunctionCodeGenerator::createIfElse(llvm::Value *cond, const std::function<void()> &then,
                                         const std::function<void()> &otherwise) {
    createIfElseBranchCond(cond, [then]() { then(); return true; }, [otherwise]() { otherwise(); return true; });
}

llvm::Value* FunctionCodeGenerator::createIfElsePhi(llvm::Value* cond, const std::function<llvm::Value* ()> &then,
                                              const std::function<llvm::Value *()> &otherwise) {
    auto function = builder().GetInsertBlock()->getParent();
    auto thenBlock = llvm::BasicBlock::Create(ctx(), "then", function);
    auto otherwiseBlock = llvm::BasicBlock::Create(ctx(), "else", function);
    auto mergeBlock = llvm::BasicBlock::Create(ctx(), "cont", function);

    builder().CreateCondBr(cond, thenBlock, otherwiseBlock);

    builder().SetInsertPoint(thenBlock);
    auto thenValue = then();
    auto thenIncoming = builder().GetInsertBlock();
    builder().CreateBr(mergeBlock);

    builder().SetInsertPoint(otherwiseBlock);
    auto otherwiseValue = otherwise();
    auto otherwiseIncoming = builder().GetInsertBlock();
    builder().CreateBr(mergeBlock);

    builder().SetInsertPoint(mergeBlock);
    auto phi = builder().CreatePHI(thenValue->getType(), 2);
    phi->addIncoming(thenValue, thenIncoming);
    phi->addIncoming(otherwiseValue, otherwiseIncoming);
    return phi;
}

std::pair<llvm::Value*, llvm::Value*>
    FunctionCodeGenerator::createIfElsePhi(llvm::Value* cond, const FunctionCodeGenerator::PairIfElseCallback &then,
                                           const FunctionCodeGenerator::PairIfElseCallback &otherwise) {
    auto function = builder().GetInsertBlock()->getParent();
    auto thenBlock = llvm::BasicBlock::Create(ctx(), "then", function);
    auto otherwiseBlock = llvm::BasicBlock::Create(ctx(), "else", function);
    auto mergeBlock = llvm::BasicBlock::Create(ctx(), "cont", function);

    builder().CreateCondBr(cond, thenBlock, otherwiseBlock);

    builder().SetInsertPoint(thenBlock);
    auto thenValue = then();
    auto thenIncoming = builder().GetInsertBlock();
    builder().CreateBr(mergeBlock);

    builder().SetInsertPoint(otherwiseBlock);
    auto otherwiseValue = otherwise();
    auto otherwiseIncoming = builder().GetInsertBlock();
    builder().CreateBr(mergeBlock);

    builder().SetInsertPoint(mergeBlock);
    auto phi1 = builder().CreatePHI(thenValue.first->getType(), 2);
    phi1->addIncoming(thenValue.first, thenIncoming);
    phi1->addIncoming(otherwiseValue.first, otherwiseIncoming);
    auto phi2 = builder().CreatePHI(thenValue.second->getType(), 2);
    phi2->addIncoming(thenValue.second, thenIncoming);
    phi2->addIncoming(otherwiseValue.second, otherwiseIncoming);
    return std::make_pair(phi1, phi2);
}

llvm::ConstantInt* FunctionCodeGenerator::int8(int8_t value) {
    return llvm::ConstantInt::getSigned(llvm::Type::getInt8Ty(ctx()), value);
}

llvm::ConstantInt* FunctionCodeGenerator::int16(int16_t value) {
    return llvm::ConstantInt::getSigned(llvm::Type::getInt16Ty(ctx()), value);
}

llvm::ConstantInt* FunctionCodeGenerator::int32(int32_t value) {
    return llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(ctx()), value);
}

llvm::ConstantInt* FunctionCodeGenerator::int64(int64_t value) {
    return llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(ctx()), value);
}

llvm::Value* FunctionCodeGenerator::alloc(llvm::Type *type) {
    return builder().CreateCall(generator()->runTime().alloc(), sizeOf(type), "alloc");
}

llvm::Value* FunctionCodeGenerator::stackAlloc(llvm::Type *type) {
    auto structType = llvm::StructType::get(llvm::Type::getInt64Ty(ctx()), type);
    auto ptr = createEntryAlloca(structType);

    builder().CreateStore(int64(1), builder().CreateConstInBoundsGEP2_32(structType, ptr, 0, 0));
    auto object = builder().CreateConstInBoundsGEP2_32(structType, ptr, 0, 1);
    auto controlBlockField = builder().CreateConstInBoundsGEP2_32(type, object, 0, 0);
    builder().CreateStore(llvm::ConstantPointerNull::get(typeHelper().pointer()), controlBlockField);
    return object;
}

void FunctionCodeGenerator::makeRemoteBoxValueUnique(llvm::Value *box, const Type &type) {
    auto llvmType = typeHelper().llvmTypeFor(type);
    auto mngType = typeHelper().managable(llvmType);
    auto valuePtrPtr = buildGetBoxValuePtr(box);
    auto object = builder().CreateLoad(typeHelper().pointer(), buildGetRemoteBoxObjectPtr(box));
    auto isUnique = builder().CreateCall(generator()->runTime().isOnlyReference(), object);
    createIf(builder().CreateNot(isUnique), [&] {
        // The contents of the value are not retained, as this box's references to them move to the copy.
        auto value = builder().CreateLoad(llvmType, builder().CreateLoad(typeHelper().pointer(), valuePtrPtr));
        builder().CreateStore(value, buildSetRemoteBoxObject(box, mngType, alloc(mngType)));
        builder().CreateCall(generator()->runTime().releaseWithoutDeinit(), object);
    });
}

llvm::Value* FunctionCodeGenerator::buildErasedReference(llvm::Value *address, llvm::Value *entry) {
    if (entry == nullptr) {
        entry = llvm::ConstantPointerNull::get(typeHelper().pointer());
    }
    llvm::Value *reference = llvm::UndefValue::get(typeHelper().erasedReference());
    reference = builder().CreateInsertValue(reference, address, 0);
    return builder().CreateInsertValue(reference, entry, 1);
}

llvm::Value* FunctionCodeGenerator::buildErasedReferenceAddress(llvm::Value *reference) {
    return builder().CreateExtractValue(reference, 0);
}

llvm::Value* FunctionCodeGenerator::buildTypeDescriptionEntry(const Type &type) {
    return TypeDescriptionGenerator(this, TypeDescriptionUser::Function).entryFor(type);
}

/// Returns the field at @p index of the value witness of the type described by @p entry.
static llvm::Value* witnessField(FunctionCodeGenerator *fg, llvm::Value *entry, unsigned index) {
    auto &th = fg->typeHelper();
    auto witness = fg->builder().CreateLoad(th.pointer(),
                                            fg->builder().CreateConstInBoundsGEP2_32(th.typeDescription(), entry, 0, 2));
    return fg->builder().CreateLoad(th.valueWitness()->getElementType(index),
                                    fg->builder().CreateConstInBoundsGEP2_32(th.valueWitness(), witness, 0, index));
}

bool FunctionCodeGenerator::boxHasConformance(const Type &type) {
    return type.type() == TypeType::Box && (type.boxedFor().type() == TypeType::Protocol ||
                                            type.boxedFor().type() == TypeType::MultiProtocol);
}

llvm::Value* FunctionCodeGenerator::buildBoxConformance(llvm::Value *box, llvm::Value *boxInfo, const Type &type) {
    auto &boxedFor = type.boxedFor();
    if (boxedFor.type() == TypeType::Protocol) {
        return buildFindProtocolConformance(box, boxInfo, boxedFor.protocol()->rtti());
    }
    auto &protocols = boxedFor.protocols();
    return buildMultiprotocolTable(protocols, [&](size_t i) {
        return buildFindProtocolConformance(box, boxInfo, protocols[i].protocol()->rtti());
    });
}

llvm::Value* FunctionCodeGenerator::buildMultiprotocolTable(const std::vector<Type> &protocols,
                                                            const std::function<llvm::Value*(size_t)> &conformanceAt) {
    auto arrayType = llvm::ArrayType::get(typeHelper().pointer(), protocols.size());
    auto conformances = createEntryAlloca(arrayType);
    for (size_t i = 0; i < protocols.size(); i++) {
        builder().CreateStore(conformanceAt(i), builder().CreateConstInBoundsGEP2_32(arrayType, conformances, 0, i));
    }
    return builder().CreateCall(generator()->runTime().multiprotocolTable(), { conformances, int64(protocols.size()) });
}

void FunctionCodeGenerator::conformanceToBoxInfo(llvm::Value *box, const Type &type) {
    auto infoPtr = buildGetBoxInfoPtr(box);
    auto conformance = builder().CreateLoad(typeHelper().pointer(), infoPtr);
    createIf(builder().CreateIsNotNull(conformance), [&] {
        builder().CreateStore(buildGetValueBoxInfo(conformance, type), infoPtr);
    });
}

void FunctionCodeGenerator::boxInfoToConformance(llvm::Value *box, const Type &type) {
    auto infoPtr = buildGetBoxInfoPtr(box);
    auto boxInfo = builder().CreateLoad(typeHelper().pointer(), infoPtr);
    createIf(builder().CreateIsNotNull(boxInfo), [&] {
        builder().CreateStore(buildBoxConformance(box, boxInfo, type), infoPtr);
    });
}

llvm::Value* FunctionCodeGenerator::buildValueSize(llvm::Value *entry) {
    return witnessField(this, entry, 0);
}

llvm::Value* FunctionCodeGenerator::buildLoadErased(llvm::Value *reference, const Type &otype) {
    auto type = otype;
    type.setReference(false);
    auto box = createEntryAlloca(typeHelper().box());
    auto address = buildErasedReferenceAddress(reference);
    auto entry = builder().CreateExtractValue(reference, 1);
    createIfElse(builder().CreateIsNull(entry), [&] {
        builder().CreateStore(builder().CreateLoad(typeHelper().box(), address), box);
        retain(box, type);
    }, [&] {
        builder().CreateCall(typeHelper().valueWitnessCopy(), witnessField(this, entry, 1), { address, box });
        if (boxHasConformance(type)) {
            boxInfoToConformance(box, type);
        }
    });
    return builder().CreateLoad(typeHelper().box(), box);
}

void FunctionCodeGenerator::buildStoreErased(llvm::Value *address, llvm::Value *entry, llvm::Value *boxValue,
                                             const Type &type) {
    auto box = createEntryAlloca(typeHelper().box());
    builder().CreateStore(boxValue, box);
    if (boxHasConformance(type)) {
        conformanceToBoxInfo(box, type);
    }
    builder().CreateCall(typeHelper().valueWitnessCopy(), witnessField(this, entry, 2), { address, box });
}

void FunctionCodeGenerator::buildReleaseErased(llvm::Value *address, llvm::Value *entry) {
    createIf(builder().CreateNot(witnessField(this, entry, 5)), [&] {
        builder().CreateCall(typeHelper().boxRetainRelease(), witnessField(this, entry, 3), { address });
    });
}

void FunctionCodeGenerator::buildReleaseErased(llvm::Value *address, llvm::Value *entry, llvm::Value *count) {
    // The flag is checked once, so that the values of a type that is not managed, like 🔢, aren't visited at all.
    createIf(builder().CreateNot(witnessField(this, entry, 5)), [&] {
        auto release = witnessField(this, entry, 3);
        createForEachValue(address, count, buildValueSize(entry), [&](llvm::Value *valueAddress) {
            builder().CreateCall(typeHelper().boxRetainRelease(), release, { valueAddress });
        });
    });
}

void FunctionCodeGenerator::buildCopyErased(llvm::Value *destination, llvm::Value *source, llvm::Value *count,
                                            llvm::Value *entry) {
    auto size = buildValueSize(entry);
    builder().CreateMemMove(destination, llvm::MaybeAlign(), source, llvm::MaybeAlign(),
                            builder().CreateMul(size, count));
    createIf(builder().CreateNot(witnessField(this, entry, 5)), [&] {
        auto retain = witnessField(this, entry, 4);
        createForEachValue(destination, count, size, [&](llvm::Value *address) {
            builder().CreateCall(typeHelper().boxRetainRelease(), retain, { address });
        });
    });
}

llvm::Value* FunctionCodeGenerator::buildErasedReferenceBox(llvm::Value *reference, const Type &otype) {
    auto type = otype;
    type.setReference(false);
    auto address = buildErasedReferenceAddress(reference);
    auto entry = builder().CreateExtractValue(reference, 1);
    return createIfElsePhi(builder().CreateIsNull(entry), [&] { return address; }, [&]() -> llvm::Value* {
        auto box = createEntryAlloca(typeHelper().box());
        builder().CreateCall(typeHelper().valueWitnessCopy(), witnessField(this, entry, 1), { address, box });
        if (boxHasConformance(type)) {
            boxInfoToConformance(box, type);
        }
        return box;
    });
}

void FunctionCodeGenerator::buildErasedReferenceWriteBack(llvm::Value *reference, llvm::Value *box,
                                                          const Type &otype, bool mutated) {
    auto type = otype;
    type.setReference(false);
    auto address = buildErasedReferenceAddress(reference);
    auto entry = builder().CreateExtractValue(reference, 1);
    createIf(builder().CreateIsNotNull(entry), [&] {
        if (mutated) {
            buildReleaseErased(address, entry);
            buildStoreErased(address, entry, builder().CreateLoad(typeHelper().box(), box), type);
        }
        release(box, type);
    });
}

llvm::Value* FunctionCodeGenerator::buildSetRemoteBoxObject(llvm::Value *box, llvm::StructType *managable,
                                                            llvm::Value *object) {
    auto valuePtr = managableGetValuePtr(managable, object);
    // The first element in the value area is a direct pointer to the struct.
    builder().CreateStore(valuePtr, buildGetBoxValuePtr(box));
    // The second is a pointer to the object for management.
    builder().CreateStore(object, buildGetRemoteBoxObjectPtr(box));
    return valuePtr;
}

llvm::Value* FunctionCodeGenerator::buildGetRemoteBoxObjectPtr(llvm::Value *box) {
    return buildGetBoxValuePtrAfter(box, typeHelper().pointer(), typeHelper().pointer());
}

llvm::Value* FunctionCodeGenerator::managableGetValuePtr(llvm::StructType *managable, llvm::Value *managablePtr) {
    return builder().CreateConstInBoundsGEP2_32(managable, managablePtr, 0, 1);
}

llvm::Value* FunctionCodeGenerator::createEntryAlloca(llvm::Type *type, const llvm::Twine &name) {
    llvm::IRBuilder<> builder(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    return builder.CreateAlloca(type, nullptr, name);
}

llvm::Value* FunctionCodeGenerator::createPendingPointerAlloca() {
    llvm::IRBuilder<> builder(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    auto alloca = builder.CreateAlloca(typeHelper().pointer());
    builder.CreateStore(llvm::ConstantPointerNull::get(typeHelper().pointer()), alloca);
    return alloca;
}

llvm::Constant* FunctionCodeGenerator::boxInfoFor(const Type &type) {
    return generator()->boxInfoFor(type);
}

void TemporaryObjectsManager::releaseTemporaryObjects(FunctionCodeGenerator *fg, bool clearQueue, bool skipLast,
                                                       bool includeProtected) {
    if (temporaryObjects_.empty()) return;
    auto end = skipLast ? temporaryObjects_.end() - 1 : temporaryObjects_.end();
    // A protected entry not visited here (includeProtected is false) must never be dropped, whatever clearQueue
    // says, since it is still owned by a call that has not been reached yet on this path.
    std::vector<Temporary> kept;
    for (auto it = temporaryObjects_.begin(); it != temporaryObjects_.end(); it++) {
        if (it->protectedEntry && !includeProtected) {
            kept.push_back(*it);
            continue;
        }
        if (it < end) {
            release(fg, *it);
        }
        if (!clearQueue) {
            kept.push_back(*it);
        }
    }
    temporaryObjects_ = std::move(kept);
}

void TemporaryObjectsManager::releaseTemporaryObjectsSince(FunctionCodeGenerator *fg, size_t mark) {
    if (mark >= temporaryObjects_.size()) return;
    // Protected entries stay: they belong to a call that has not been reached yet (see releaseTemporaryObjects()).
    std::vector<Temporary> kept(temporaryObjects_.begin(), temporaryObjects_.begin() + mark);
    for (auto it = temporaryObjects_.begin() + mark; it != temporaryObjects_.end(); it++) {
        if (it->protectedEntry) {
            kept.push_back(*it);
            continue;
        }
        release(fg, *it);
    }
    temporaryObjects_ = std::move(kept);
}

void TemporaryObjectsManager::release(FunctionCodeGenerator *fg, const Temporary &temporary) {
    switch (temporary.kind) {
        case Kind::RemoteObject: {
            auto object = fg->builder().CreateLoad(fg->typeHelper().pointer(), temporary.value);
            fg->createIf(fg->builder().CreateIsNotNull(object), [&] {
                fg->builder().CreateCall(fg->generator()->runTime().releaseWithoutDeinit(), object);
            });
            break;
        }
        case Kind::RawAllocation: {
            auto pointer = fg->builder().CreateLoad(fg->typeHelper().pointer(), temporary.value);
            fg->createIf(fg->builder().CreateIsNotNull(pointer), [&] {
                fg->builder().CreateCall(fg->generator()->runTime().freeDescription(), pointer);
            });
            break;
        }
        case Kind::Managed:
            fg->release(temporary.value, temporary.type);
            break;
    }
}

void FunctionCodeGenerator::release(llvm::Value *value, const Type &otype) {
    auto type = otype.resolveOnSuperArgumentsAndConstraints(*typeContext_);
    if (type.type() == TypeType::Class || type.type() == TypeType::Someobject) {
        builder().CreateCall(generator()->runTime().release(), value);
    }
    else if (type.type() == TypeType::ValueType && type.valueType() == compiler()->sMemory) {
        builder().CreateCall(generator()->runTime().releaseMemory(), value);
    }
    else if (type.type() == TypeType::ValueType) {
        builder().CreateCall(type.valueType()->destructor(), value);
    }
    else if (type.type() == TypeType::Optional) {
        if (isManagedByReference(type)) {
            createIf(buildOptionalHasValuePtr(value, type), [&] {
                release(buildGetOptionalValuePtr(value, type), type.optionalType());
            });
        }
        else {
            createIf(buildOptionalHasValue(value, type), [&] {
                release(buildGetOptionalValue(value, type), type.optionalType());
            });
        }
    }
    else if (type.type() == TypeType::Box) {
        auto boxInfo = builder().CreateLoad(typeHelper().pointer(), buildGetBoxInfoPtr(value));
        if (type.unboxed().type() == TypeType::Optional || type.unboxed().type() == TypeType::Something) {
            auto null = llvm::ConstantPointerNull::get(typeHelper().pointer());
            createIf(builder().CreateICmpNE(boxInfo, null), [&] {
                manageBox(false, boxInfo, value, type);
            });
        }
        else {
            manageBox(false, boxInfo, value, type);
        }
    }
    else if (type.type() == TypeType::Callable && !type.isCCallable()) {
        builder().CreateCall(generator()->runTime().releaseCapture(), builder().CreateExtractValue(value, 1));
    }
}

void FunctionCodeGenerator::retain(llvm::Value *value, const Type &otype) {
    auto type = otype.resolveOnSuperArgumentsAndConstraints(*typeContext_);
    if (type.type() == TypeType::Class || type.type() == TypeType::Someobject) {
        builder().CreateCall(generator()->runTime().retain(), value);
    }
    else if (type.type() == TypeType::ValueType && type.valueType() == compiler()->sMemory) {
        builder().CreateCall(generator()->runTime().retainMemory(), value);
    }
    else if (type.type() == TypeType::Callable && !type.isCCallable()) {
        builder().CreateCall(generator()->runTime().retain(), builder().CreateExtractValue(value, 1));
    }
    else if (type.type() == TypeType::ValueType) {
        builder().CreateCall(type.valueType()->copyRetain(), value);
    }
    else if (type.type() == TypeType::Optional) {
        if (isManagedByReference(type)) {
            createIf(buildOptionalHasValuePtr(value, type), [&] {
                retain(buildGetOptionalValuePtr(value, type), type.optionalType());
            });
        }
        else {
            createIf(buildOptionalHasValue(value, type), [&] {
                retain(buildGetOptionalValue(value, type), type.optionalType());
            });
        }
    }
    else if (type.type() == TypeType::Box) {
        auto boxInfo = builder().CreateLoad(typeHelper().pointer(), buildGetBoxInfoPtr(value));
        if (type.unboxed().type() == TypeType::Optional || type.unboxed().type() == TypeType::Something) {
            auto null = llvm::ConstantPointerNull::get(typeHelper().pointer());
            createIf(builder().CreateICmpNE(boxInfo, null), [&] {
                manageBox(true, boxInfo, value, type);
            });
        }
        else {
            manageBox(true, boxInfo, value, type);
        }
    }
}

void FunctionCodeGenerator::makeBoxValueUnique(llvm::Value *conformance, llvm::Value *box) {
    auto fnPtr = builder().CreateConstInBoundsGEP2_32(typeHelper().protocolConformance(), conformance, 0, 5);
    auto fn = builder().CreateLoad(typeHelper().pointer(), fnPtr, "makeUnique");
    createIf(builder().CreateIsNotNull(fn), [&] {
        auto call = builder().CreateCall(typeHelper().boxRetainRelease(), fn, box);
        call->addParamAttr(0, llvm::Attribute::getWithCaptureInfo(call->getContext(), llvm::CaptureInfo::none()));
        call->addFnAttr(llvm::Attribute::NoUnwind);
    });
}

void FunctionCodeGenerator::manageBox(bool retain, llvm::Value *boxInfo, llvm::Value *value, const Type &type) {
    llvm::Value *fnPtr;
    if (type.boxedFor().type() == TypeType::Protocol || type.boxedFor().type() == TypeType::MultiProtocol) {
        fnPtr = builder().CreateConstInBoundsGEP2_32(typeHelper().protocolConformance(),
                                                     buildGetBoxConformance(boxInfo, type), 0, retain ? 3 : 4);
    }
    else {
        fnPtr = builder().CreateConstInBoundsGEP2_32(typeHelper().boxInfo(), boxInfo, 0, retain ? 1 : 2);
    }
    auto fn = builder().CreateLoad(typeHelper().pointer(), fnPtr, retain ? "retain" : "release");
    auto call = builder().CreateCall(typeHelper().boxRetainRelease(), fn, value);
    call->addParamAttr(0, llvm::Attribute::getWithCaptureInfo(call->getContext(), llvm::CaptureInfo::none()));
    call->addParamAttr(0, llvm::Attribute::ReadOnly);
    call->addFnAttr(llvm::Attribute::NoUnwind);
}

bool FunctionCodeGenerator::isManagedByReference(const Type &type) const {
    return (type.type() == TypeType::ValueType && !type.valueType()->isPrimitive()) || type.type() == TypeType::Box
        || (type.type() == TypeType::Optional && isManagedByReference(type.optionalType()));
}

void FunctionCodeGenerator::releaseByReference(llvm::Value *ptr, const Type &type) {
    release(isManagedByReference(type) ? ptr : builder().CreateLoad(typeHelper().llvmTypeFor(type), ptr), type);
}

void FunctionCodeGenerator::retainByReference(llvm::Value *ptr, const Type &type) {
    retain(isManagedByReference(type) ? ptr : builder().CreateLoad(typeHelper().llvmTypeFor(type), ptr), type);
}

llvm::Value* FunctionCodeGenerator::buildFindProtocolConformance(llvm::Value *box, llvm::Value *boxInfo,
                                                                 llvm::Value *protocolRTTI) {
    auto objBoxInfo = generator()->runTime().boxInfoForObjects();
    auto conformanceEntries = createIfElsePhi(builder().CreateICmpEQ(boxInfo, objBoxInfo), [&]() {
        auto obj = builder().CreateLoad(typeHelper().pointer(), buildGetBoxValuePtr(box));
        auto classInfo = buildGetClassInfoFromObject(obj);
        return builder().CreateLoad(typeHelper().pointer(),
                                    builder().CreateConstInBoundsGEP2_32(typeHelper().classInfo(), classInfo, 0, 2));
    }, [&] {
        auto conformanceEntriesPtr = builder().CreateConstInBoundsGEP2_32(typeHelper().boxInfo(), boxInfo, 0, 3);
        return builder().CreateLoad(typeHelper().pointer(), conformanceEntriesPtr);
    });

    return builder().CreateCall(generator()->runTime().findProtocolConformance(),
                                    { conformanceEntries, protocolRTTI });
}

llvm::StructType* FunctionCodeGenerator::calleeStructType() {
    return llvm::cast<llvm::StructType>(typeHelper().llvmTypeForTypeDefinition(calleeType()));
}

unsigned FunctionCodeGenerator::instanceVariableIndex(size_t id) const {
    auto &callee = typeContext_->calleeType();
    auto offset = callee.type() != TypeType::NoReturn ? (callee.type() == TypeType::Class ? 2 : 0) +
                    (callee.typeDefinition()->storesGenericArgs() ? 1 : 0) : 0;
    return offset + id;
}

llvm::Value* FunctionCodeGenerator::instanceVariablePointer(size_t id) {
    return builder().CreateConstInBoundsGEP2_32(calleeStructType(), thisValue(), 0, instanceVariableIndex(id));
}

llvm::Type* FunctionCodeGenerator::instanceVariableType(size_t id) {
    return calleeStructType()->getElementType(instanceVariableIndex(id));
}

llvm::Value* FunctionCodeGenerator::genericArgsPtr() {
    if (genericArgsPtr_ != nullptr) {
        return genericArgsPtr_;
    }
    if (fn_ != nullptr && isTypeMethod(fn_)) {
        return typeMethodGenericArgs_;
    }

    auto callee = typeContext_->calleeType();
    assert(callee.typeDefinition()->storesGenericArgs());
    return builder().CreateConstInBoundsGEP2_32(calleeStructType(), thisValue(), 0,
                                                callee.type() == TypeType::Class ? 2 : 0);
}

void FunctionCodeGenerator::freeOwnedDescription(llvm::Value *gargs) {
    createIf(builder().CreateIsNull(builder().CreateExtractValue(gargs, { 1 })), [&] {
        builder().CreateCall(generator()->runTime().freeDescription(), { builder().CreateExtractValue(gargs, { 0 }) });
    });
}

llvm::Value* FunctionCodeGenerator::isErrorSet(llvm::Value *errorPointer) {
    return builder().CreateICmpNE(llvm::ConstantPointerNull::get(typeHelper().pointer()),
                                  builder().CreateLoad(typeHelper().pointer(), errorPointer));
}

llvm::Type* FunctionCodeGenerator::genericArgsType() {
    return typeHelper().genericArgsStore(calleeType());
}

FunctionCodeGenerator::~FunctionCodeGenerator() = default;

llvm::Value* FunctionCodeGenerator::buildCConversion(llvm::Value *value, const Type &from, const Type &to) {
    auto &target = *to.valueType()->cRepresentation();
    auto targetType = typeHelper().llvmTypeFor(to);

    if (from.type() == TypeType::ValueType && from.valueType() == compiler()->sMemory) {
        assert(target.isPointer());
        return builder().CreateConstInBoundsGEP1_64(llvm::Type::getInt8Ty(ctx()), value,
                                                    generator()->querySize(typeHelper().pointer()));
    }

    auto &source = *from.valueType()->cRepresentation();
    if (source.isPointer()) {
        if (target.isPointer()) {
            return value;
        }
        return builder().CreatePtrToInt(value, targetType);
    }
    if (target.isPointer()) {
        return builder().CreateIntToPtr(value, targetType);
    }
    if (source.isFloat()) {
        if (target.isFloat()) {
            return builder().CreateFPCast(value, targetType);
        }
        if (target.bits == 1) {
            return builder().CreateFCmpUNE(value, llvm::ConstantFP::get(value->getType(), 0));
        }
        return target.isSigned ? builder().CreateFPToSI(value, targetType) : builder().CreateFPToUI(value, targetType);
    }
    if (target.isFloat()) {
        return source.isSigned && source.bits > 1 ? builder().CreateSIToFP(value, targetType)
                                                  : builder().CreateUIToFP(value, targetType);
    }
    if (target.bits == 1) {
        return builder().CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0));
    }
    return builder().CreateIntCast(value, targetType, source.isSigned && source.bits > 1);
}

}  // namespace EmojicodeCompiler
