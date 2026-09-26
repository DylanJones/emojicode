//
//  LLVMTypeHelper.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 06/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "Functions/Function.hpp"
#include "CodeGenerator.hpp"
#include "Compiler.hpp"
#include "Functions/Initializer.hpp"
#include "Generation/ReificationContext.hpp"
#include "LLVMTypeHelper.hpp"
#include "Mangler.hpp"
#include "Package/Package.hpp"
#include "Scoping/CapturingSemanticScoper.hpp"
#include "Types/Class.hpp"
#include "Types/ValueType.hpp"
#include "Types/TypeDefinition.hpp"
#include "Types/TypeContext.hpp"
#include <llvm/IR/DerivedTypes.h>
#include <AST/ASTClosure.hpp>

namespace EmojicodeCompiler {

/// The number of bytes a box provides for storing value type data.
const unsigned kBoxSize = 32;

LLVMTypeHelper::LLVMTypeHelper(llvm::LLVMContext &context, CodeGenerator *codeGenerator)
        : context_(context), codeGenerator_(codeGenerator), mdBuilder_(context) {

    runTimeTypeInfo_ = llvm::StructType::create({
        llvm::Type::getInt16Ty(context_),  // generic parameter count
        llvm::Type::getInt16Ty(context_),  // generic parameter offset (for subclasses)
        llvm::Type::getInt8Ty(context_), // flag (see RunTimeTypeInfoFlags)
    }, "runTimeTypeInfo");

    typeDescription_ = llvm::StructType::create(context_, "typeDescription");
    typeDescription_->setBody({
        // Pointer to the generic type info of the described type.
        // The address itself is used to determine whether to types are equal!
        pointer(),
        llvm::Type::getInt1Ty(context_)  // optional
    });

    boxInfoType_ = llvm::StructType::create(context_, "boxInfo");
    box_ = llvm::StructType::create(context_, "box");

    boxRetainRelease_ = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), pointer(), false);

    protocolsTable_ = llvm::StructType::create({
        llvm::Type::getInt1Ty(context_),  // whether the boxed value itself is the callee (i.e. value type) or not
        pointer(),  // dispatch table
        pointer(),  // box info
        pointer(), pointer(),  // box retain and release
        pointer(),  // makes the value of a box unique before a mutation, or null (see buildBoxMakeUnique)
    }, "protocolConformance");
    protocolConformanceEntry_ = llvm::StructType::create({ pointer(), pointer() }, "protocolConformanceEntry");

    boxInfoType_->setBody({
        runTimeTypeInfo_,  // must be first so that we can cast back and forth between boxInfo and runTimeTypeInfo
        pointer(),  // box retain
        pointer(),  // box release
        pointer()  // protocol conformance entries
    });

    box_->setBody({
        pointer(), llvm::ArrayType::get(llvm::Type::getInt8Ty(context_), kBoxSize),
    });

    classInfoType_ = llvm::StructType::create(context_, "classInfo");
    classInfoType_->setBody({
        runTimeTypeInfo_,  // must be first so that we can cast back and forth between classInfo and runTimeTypeInfo
        pointer(),  // dispatch table
        pointer(),  // protocol conformance entries
        pointer(),  // superclass class info
        pointer()  // destructor pointer
    });

    callable_ = llvm::StructType::create({
        pointer(),  // function pointer
        pointer()  // capture pointer
    }, "callable");

    someobject_ = llvm::StructType::create({
        pointer(),  // control block
        pointer()  // class info
    }, "someobject");

    captureDeinit_ = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), pointer(), false);

    callableBoxCapture_ = llvm::StructType::get(pointer(), pointer(), callable());

    auto compiler = codeGenerator_->compiler();
    compiler->sInteger->createUnspecificReification().type = llvm::Type::getInt64Ty(context_);
    compiler->sReal->createUnspecificReification().type = llvm::Type::getDoubleTy(context_);
    compiler->sBoolean->createUnspecificReification().type = llvm::Type::getInt1Ty(context_);
    compiler->sMemory->createUnspecificReification().type = pointer();
    compiler->sByte->createUnspecificReification().type = llvm::Type::getInt8Ty(context_);

    tbaaRoot_ = mdBuilder_.createTBAARoot("something");
}

LLVMTypeHelper::~LLVMTypeHelper() = default;

void LLVMTypeHelper::withReificationContext(ReificationContext context, std::function<void ()> function) {
    auto ptr = std::make_unique<ReificationContext>(std::move(context));
    std::swap(ptr, reifiContext_);
    function();
    reifiContext_ = std::move(ptr);
}

llvm::StructType* LLVMTypeHelper::llvmTypeForCapture(const Capture &capture, llvm::Type *thisType, bool escaping) {
    std::vector<llvm::Type *> types { pointer(), pointer() };
    if (capture.capturesSelf()) {
        types.emplace_back(thisType);
    }
    std::transform(capture.variableTypes.begin(), capture.variableTypes.end(), std::back_inserter(types),
                   [this, escaping](llvm::Type *type) -> llvm::Type* {
        return escaping ? type : pointer();
    });
    if (capture.genericArgsOf != nullptr) {
        types.emplace_back(pointer());
    }
    return llvm::StructType::get(context_, types);
}

llvm::ArrayType* LLVMTypeHelper::multiprotocolConformance(const Type &type) {
    return llvm::ArrayType::get(pointer(), type.protocols().size());
}

llvm::Type* LLVMTypeHelper::typeForFunction(const Type &type, Function *function) {
    if (reifiContext_ != nullptr && type.type() == TypeType::LocalGenericVariable &&
        reifiContext_->providesActualTypeFor(type.genericVariableIndex())) {
        return llvmTypeFor(reifiContext_->actualType(type.genericVariableIndex()));
    }
    // The local generic variable can belong to a function enclosing the closure function.
    if (type.type() == TypeType::LocalGenericVariable) {
        return llvmTypeFor(type.localResolutionConstraint()->constraintForIndex(type.genericVariableIndex()));
    }
    if (type.type() == TypeType::GenericVariable && function->owner()->canResolve(type.resolutionConstraint())) {
        return llvmTypeFor(function->owner()->constraintForIndex(type.genericVariableIndex()));
    }
    if (type.unoptionalized().type() == TypeType::LocalGenericVariable) {
        auto local = type.unoptionalized();
        return llvmTypeFor(local.localResolutionConstraint()->constraintForIndex(local.genericVariableIndex())
                           .optionalized());
    }
    if (type.unoptionalized().type() == TypeType::GenericVariable &&
        function->owner()->canResolve(type.unoptionalized().resolutionConstraint())) {
        return llvmTypeFor(function->owner()->constraintForIndex(type.unoptionalized().genericVariableIndex()).optionalized());
    }
    return llvmTypeFor(type);
}

llvm::FunctionType* LLVMTypeHelper::functionTypeFor(Function *function) {
    std::vector<llvm::Type *> args;
    if (function->isClosure()) {
        if (!function->isC()) {
            args.emplace_back(pointer());
        }
    }
    else if (hasThisArgument(function)) {
        args.emplace_back(typeForFunction(function->typeContext().calleeType(), function));
    }
    std::transform(function->parameters().begin(), function->parameters().end(), std::back_inserter(args), [&](auto &arg) {
        return typeForFunction(arg.type->type(), function);
    });
    if ((function->functionType() == FunctionType::ObjectInitializer ||
         function->functionType() == FunctionType::ValueTypeInitializer) && function->owner()->storesGenericArgs()) {
        args.emplace_back(genericArgsStore(function->typeContext().calleeType()));
    }
    if (isTypeMethod(function) && function->owner()->storesGenericArgs()) {
        args.emplace_back(pointer());
    }
    if (!function->genericParameters().empty()) {
        args.emplace_back(pointer());
    }
    if (function->errorProne()) {
        args.emplace_back(pointer());
    }
    llvm::Type *returnType;
    if (function->functionType() == FunctionType::ObjectInitializer) {
        auto init = dynamic_cast<Initializer *>(function);
        returnType = typeForFunction(init->constructedType(init->typeContext().calleeType()), function);
    }
    else {
        returnType = typeForFunction(function->returnType()->type(), function);
    }
    return llvm::FunctionType::get(returnType, args, false);
}

llvm::Type* LLVMTypeHelper::box() const {
    return box_;
}

bool LLVMTypeHelper::isDereferenceable(const Type &type) const {
    return ((type.type() == TypeType::Class || type.type() == TypeType::Someobject) &&
            type.storageType() != StorageType::Box) || type.isReference();
}

bool LLVMTypeHelper::isRemote(const Type &type) {
    return codeGenerator_->querySize(llvmTypeFor(type)) > kBoxSize;
}

llvm::Type* LLVMTypeHelper::genericArgsStore(const Type &calleeType) {
    if (calleeType.is<TypeType::ValueType>()) {
        return pointer();
    }
    return llvm::StructType::get(pointer(), llvm::Type::getInt1Ty(context_));
}

llvm::Type* LLVMTypeHelper::llvmTypeFor(const Type &type) {
    auto llvmType = typeForOrdinaryType(type);
    assert(llvmType != nullptr);
    return type.isReference() ? pointer() : llvmType;
}

llvm::Type* LLVMTypeHelper::llvmTypeForPointee(const Type &type) {
    assert(isDereferenceable(type));
    if (type.isReference()) {
        return typeForOrdinaryType(type);
    }
    if (type.type() == TypeType::Someobject) {
        return someobject_;
    }
    return llvmTypeForTypeDefinition(type);
}

llvm::PointerType* LLVMTypeHelper::pointer() const {
    return llvm::PointerType::get(context_, 0);
}

llvm::Type* LLVMTypeHelper::typeForOrdinaryType(const Type &type) {
    switch (type.storageType()) {
        case StorageType::Box:
            return box_;
        case StorageType::SimpleOptional: {
            std::vector<llvm::Type *> types{ llvm::Type::getInt1Ty(context_), llvmTypeFor(type.optionalType()) };
            return llvm::StructType::get(context_, types);
        }
        case StorageType::PointerOptional:
            return llvmTypeFor(type.optionalType());
        case StorageType::Simple:
            return getSimpleType(type);
    }
}

llvm::Type* LLVMTypeHelper::getSimpleType(const Type &type) {
    switch (type.type()) {
        case TypeType::Callable:
            return type.isCCallable() ? static_cast<llvm::Type *>(pointer()) : callable_;
        case TypeType::TypeAsValue:
            if (type.typeOfTypeValue().type() == TypeType::Class) {
                return pointer();
            }
            return llvm::StructType::get(context_);
        case TypeType::Enum:
            return llvm::Type::getInt64Ty(context_);
        case TypeType::Someobject:
            return pointer();
        case TypeType::NoReturn:
            return llvm::Type::getVoidTy(context_);
        case TypeType::ValueType:
            return llvmTypeForTypeDefinition(type);
        case TypeType::Class:
            return pointer();
        default:
            throw std::logic_error("No LLVM type could be established.");
    }
}

llvm::Type* LLVMTypeHelper::llvmTypeForTypeDefinition(const Type &type) {
    auto &reification = type.typeDefinition()->reificationFor(type.genericArguments());
    if (reification.type != nullptr) {
        return reification.type;
    }

    if (type.type() == TypeType::ValueType && type.valueType()->cRepresentation()) {
        reification.type = type.valueType()->cRepresentation()->llvmType(context_);
        return reification.type;
    }

    auto structType = llvm::StructType::create(context_, mangleTypeName(type));
    reification.type = structType;

    std::vector<llvm::Type *> types;
    if (type.is<TypeType::Class>()) {
        types.emplace_back(pointer());
        types.emplace_back(pointer());
    }

    if (type.typeDefinition()->storesGenericArgs()) {
        types.emplace_back(genericArgsStore(type));
    }

    for (auto &ivar : type.typeDefinition()->instanceVariables()) {
        types.emplace_back(llvmTypeFor(ivar.type->type()));
    }

    structType->setBody(types);  // for self referencing types
    return structType;
}

llvm::StructType* LLVMTypeHelper::managable(llvm::Type *type) const {
    return llvm::StructType::get(context_, { pointer(), type });
}

llvm::MDNode* LLVMTypeHelper::tbaaNodeFor(const Type &type, bool classAsStruct) {
    if (type.is<TypeType::Enum>() || (type.is<TypeType::ValueType>() && type.valueType()->isPrimitive())) {
        return mdBuilder_.createTBAAScalarTypeNode(mangleTypeName(type), tbaaRoot_);
    }
    if (!classAsStruct && type.is<TypeType::Class>()) {
        auto superclass = type.klass()->superclass();
        auto super = superclass != nullptr ? tbaaNodeFor(type.klass()->superType()->type(), false) : tbaaRoot_;
        return mdBuilder_.createTBAAScalarTypeNode(mangleTypeName(type) + ".ref", super);
    }
    if (type.is<TypeType::ValueType>() || type.is<TypeType::Class>()) {
        std::vector<std::pair<llvm::MDNode*, uint64_t>> elements;
        uint64_t offset = 0;
        for (auto &ivar : type.typeDefinition()->instanceVariables()) {
            elements.emplace_back(tbaaNodeFor(ivar.type->type(), false), offset++);
        }
        return mdBuilder_.createTBAAStructTypeNode(mangleTypeName(type), elements);
    }
    return tbaaRoot_;
}

bool LLVMTypeHelper::shouldAddTbaa(const Type &loadStoreType) const {
    return loadStoreType.is<TypeType::Class>() || loadStoreType.is<TypeType::Enum>() ||
        (loadStoreType.is<TypeType::ValueType>() && loadStoreType.valueType()->isPrimitive());
}

}  // namespace EmojicodeCompiler
