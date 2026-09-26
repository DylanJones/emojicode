//
//  ASTLiterals_CG.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 03/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include <utility>
#include "ASTInitialization.hpp"
#include "ASTLiterals.hpp"
#include "Types/ValueType.hpp"
#include "Generation/TypeDescriptionGenerator.hpp"
#include "Compiler.hpp"
#include "Generation/CallCodeGenerator.hpp"
#include "Generation/RunTimeHelper.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Generation/LLVMTypeHelper.hpp"
#include "Generation/StringPool.hpp"
#include "Types/Class.hpp"

namespace EmojicodeCompiler {

Value* ASTStringLiteral::generate(FunctionCodeGenerator *fg) const {
    return fg->generator()->stringPool().pool(value_);
}

Value* ASTCGUTF8Literal::generate(FunctionCodeGenerator *fg) const {
    return fg->generator()->stringPool().addToPool(value_);
}

Value* ASTBooleanTrue::generate(FunctionCodeGenerator *fg) const {
    return llvm::ConstantInt::getTrue(fg->ctx());
}

Value* ASTBooleanFalse::generate(FunctionCodeGenerator *fg) const {
    return llvm::ConstantInt::getFalse(fg->ctx());
}

Value* ASTNumberLiteral::generate(FunctionCodeGenerator *fg) const {
    switch (type_) {
        case NumberType::Byte:
            return fg->int8(integerValue_);
        case NumberType::Integer:
            return fg->int64(integerValue_);
        case NumberType::Double:
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(fg->ctx()), doubleValue_);
        case NumberType::C: {
            auto type = fg->typeHelper().llvmTypeFor(cType_);
            if (cType_.valueType()->cRepresentation()->isFloat()) {
                return llvm::ConstantFP::get(type, doubleValue_);
            }
            return llvm::ConstantInt::get(type, integerValue_, cType_.valueType()->cRepresentation()->isSigned);
        }
    }
}

Value* ASTThis::generate(FunctionCodeGenerator *fg) const {
    if (expressionType().type() == TypeType::Class && !isTemporary()) {
        // Only for class objects. For value types, $this$ is a reference, which is retained when dereferenced.
        fg->retain(fg->thisValue(), expressionType());
    }
    return fg->thisValue();
}

Value* ASTNoValue::generate(FunctionCodeGenerator *fg) const {
    if (type_.storageType() == StorageType::Box) {
        return fg->buildBoxWithoutValue();
    }
    return fg->buildSimpleOptionalWithoutValue(type_);
}

Value* ASTCollectionLiteral::init(FunctionCodeGenerator *fg, std::vector<llvm::Value*> args) const {
    auto td = TypeDescriptionGenerator(fg, TypeDescriptionUser::ValueTypeOrValue).generate(type_.genericArguments());
    auto value = fg->createEntryAlloca(fg->typeHelper().llvmTypeFor(type_));
    args.emplace_back(td);
    CallCodeGenerator(fg, CallType::StaticDispatch).generate(value, type_, ASTArguments(position()),
                                                             initializer_, nullptr, args);
    handleResult(fg, nullptr, value);
    return fg->builder().CreateLoad(fg->typeHelper().llvmTypeFor(type_), value);
}

std::pair<llvm::Value *, llvm::Value *> EmojicodeCompiler::ASTCollectionLiteral::prepareValueArray(FunctionCodeGenerator *fg, llvm::Type *type, size_t count,
                                                                                                   const char *name) const {
    auto arrayType = llvm::ArrayType::get(type, count);
    auto structType = llvm::StructType::get(fg->generator()->context(),
                                            { fg->builder().getPtrTy(), arrayType });

    auto structure = fg->createEntryAlloca(structType, name);
    fg->builder().CreateStore(fg->generator()->runTime().ignoreBlockPtr(),
                              fg->builder().CreateConstInBoundsGEP2_32(structType, structure, 0, 0));
    auto values = fg->builder().CreateConstInBoundsGEP2_32(structType, structure, 0, 1);
    return std::make_pair(fg->builder().CreateConstInBoundsGEP2_32(arrayType, values, 0, 0), structure);
}


Value* ASTCollectionLiteral::storeElements(FunctionCodeGenerator *fg, const std::vector<Value *> &values,
                                           const char *name) const {
    if (!LLVMTypeHelper::isErased(elementType_)) {
        auto type = fg->typeHelper().llvmTypeFor(elementType_);
        llvm::Value *current, *structure;
        std::tie(current, structure) = prepareValueArray(fg, type, values.size(), name);
        for (auto value : values) {
            fg->builder().CreateStore(value, current);
            current = fg->builder().CreateConstInBoundsGEP1_32(type, current, 1);
        }
        return structure;
    }

    // The elements are values of the type the generic parameter stands for, whose size is only known at run time.
    auto entry = fg->buildTypeDescriptionEntry(elementType_);
    auto size = fg->buildValueSize(entry);
    auto header = fg->typeHelper().pointer();
    auto bytes = fg->builder().CreateAdd(fg->sizeOf(header), fg->builder().CreateMul(size, fg->int64(values.size())));
    auto structure = fg->builder().CreateAlloca(llvm::Type::getInt8Ty(fg->ctx()), bytes, name);
    fg->builder().CreateStore(fg->generator()->runTime().ignoreBlockPtr(), structure);
    llvm::Value *current = fg->builder().CreateGEP(header, structure, fg->int64(1));
    for (auto value : values) {
        auto box = fg->createEntryAlloca(fg->typeHelper().box());
        fg->builder().CreateStore(value, box);
        fg->buildStoreErased(current, entry, value, elementType_);
        fg->release(box, elementType_);  // The memory holds the value instead, like a value stored directly.
        current = fg->builder().CreateGEP(llvm::Type::getInt8Ty(fg->ctx()), current, size);
    }
    return structure;
}

Value* ASTCollectionLiteral::generate(FunctionCodeGenerator *fg) const {
    if (pairs_) return generatePairs(fg);
    std::vector<Value *> values;
    for (auto &value : values_) {
        values.emplace_back(value->generate(fg));
    }
    auto stack = fg->builder().CreateStackSave();
    auto result = init(fg, { storeElements(fg, values, "items"), fg->int64(values_.size()) });
    fg->builder().CreateStackRestore(stack);
    return result;
}

Value *ASTCollectionLiteral::generatePairs(FunctionCodeGenerator *fg) const {
    std::vector<Value *> keyValues, values;
    for (size_t i = 0; i < values_.size(); i += 2) {
        keyValues.emplace_back(values_[i]->generate(fg));
        values.emplace_back(values_[i + 1]->generate(fg));
    }
    llvm::Value *keys, *currentKey;
    auto string = fg->typeHelper().llvmTypeFor(Type(fg->compiler()->sString));
    std::tie(currentKey, keys) = prepareValueArray(fg, string, keyValues.size(), "keys");
    for (auto key : keyValues) {
        fg->builder().CreateStore(key, currentKey);
        currentKey = fg->builder().CreateConstInBoundsGEP1_32(string, currentKey, 1);
    }
    auto stack = fg->builder().CreateStackSave();
    auto result = init(fg, { keys, storeElements(fg, values, "values"), fg->int64(keyValues.size()) });
    fg->builder().CreateStackRestore(stack);
    return result;
}


Value* ASTInterpolationLiteral::generate(FunctionCodeGenerator *fg) const {
    int64_t length = 0;
    for (auto &literal : literals_) {
        length += literal.size() + 6;
    }
    auto type = init_->owner()->type();
    auto lengthNode = std::make_shared<ASTNumberLiteral>(length, U"", position());
    auto builder = ASTInitialization::initObject(fg, ASTArguments(position(), {lengthNode}), init_, type, nullptr, true,
                                                 nullptr);

    auto literalsIt = literals_.begin();
    append(fg, *literalsIt++, builder);
    for (auto &value : values_) {
        auto str = CallCodeGenerator(fg, CallType::DynamicProtocolDispatch).generate(value->generate(fg), value->expressionType(),
                                                                 ASTArguments(position()), toString_, nullptr);
        append(fg, str, builder);
        // 🐻 only borrows the string, so release the +1 reference the 🔡 call returned.
        fg->release(str, fg->compiler()->sString->type());
        append(fg, *literalsIt++, builder);
    }

    auto str = CallCodeGenerator(fg, CallType::StaticDispatch).generate(builder, type,
                                                                        ASTArguments(position()), get_, nullptr);
    fg->release(builder, type);
    return handleResult(fg, str);
}

void
ASTInterpolationLiteral::append(FunctionCodeGenerator *fg, llvm::Value *value, llvm::Value *builder) const {
    CallCodeGenerator(fg, CallType::StaticDispatch).generate(builder, init_->owner()->type(), ASTArguments(position()),
                                                             append_, nullptr,
                                                             {value});
}

void
ASTInterpolationLiteral::append(FunctionCodeGenerator *fg, const std::u32string &literal, llvm::Value *builder) const {
    if (literal.empty()) {
        return;
    }
    append(fg, fg->generator()->stringPool().pool(literal), builder);
}

}  // namespace EmojicodeCompiler
