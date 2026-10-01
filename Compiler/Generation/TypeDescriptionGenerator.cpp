//
//  TypeDescriptionGenerator.cpp
//  runtime
//
//  Created by Theo Weidmann on 25.03.19.
//

#include "TypeDescriptionGenerator.hpp"
#include <algorithm>
#include "Generation/LLVMTypeHelper.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Types/Class.hpp"
#include "Types/ValueType.hpp"
#include "Types/Protocol.hpp"
#include "Compiler.hpp"
#include "Generation/RunTimeHelper.hpp"
#include "Generation/ValueWitnessBuilder.hpp"

namespace EmojicodeCompiler {

namespace {

/// A class type holds the arguments to its superclass as the class declaration writes them, followed by its own
/// arguments. The former can therefore mention generic variables of the class or its superclasses, which stand for
/// later arguments: 🎁🐚🔢🍆 of 🐇 🎁🐚V⚪️🍆 📦🐚🔡 V🍆 holds [🔡, V, 🔢].
/// This replaces these variables in @p type, the argument at @p slot of @p args, with the arguments they stand for.
/// Variables that don't refer to a later argument belong to the code the type appears in and are kept.
///
/// This is deliberately not Type::selfResolvedGenericArgs(). Both substitute from the instance's argument list and
/// only accept class constraints the class inherits from, but they differ in:
///  - the bound: this refuses any variable at or before @p slot (index <= slot), so a variable never resolves to
///    itself or an earlier argument; resolveOnWithoutCompletion() substitutes any index the constraint accepts and
///    only stops chaining when the next variable doesn't move forward;
///  - boxes: resolveOnWithoutCompletion() also resolves the box's boxedFor type, this keeps it;
///  - storage: resolveOnWithoutCompletion() keeps the reference and mutable flags and normalizes optionals
///    (rewrapped), this rebuilds the wrappers with optionalized() and boxedFor().
Type resolveSuperArgument(const Type &type, const std::vector<Type> &args, size_t slot, size_t superCount,
                          Class *klass) {
    switch (type.type()) {
        case TypeType::Optional:
            return resolveSuperArgument(type.optionalType(), args, slot, superCount, klass).optionalized();
        case TypeType::Box:
            return resolveSuperArgument(type.unboxed(), args, slot, superCount, klass).unboxed()
                .boxedFor(type.boxedFor());
        case TypeType::GenericVariable: {
            auto index = type.genericVariableIndex();
            auto constraint = dynamic_cast<Class *>(type.resolutionConstraint());
            if (constraint == nullptr || !klass->inheritsFrom(constraint) || index <= slot || index >= args.size()) {
                return type;
            }
            if (index < superCount) {
                return resolveSuperArgument(args[index], args, index, superCount, klass);
            }
            return args[index];
        }
        default:
            break;
    }
    if (!type.canHaveGenericArguments()) {
        return type;
    }
    Type resolved = type;
    for (size_t i = 0; i < type.genericArguments().size(); i++) {
        resolved.setGenericArgument(i, resolveSuperArgument(type.genericArguments()[i], args, slot, superCount,
                                                            klass));
    }
    return resolved;
}

}  // namespace

void TypeDescriptionGenerator::addType(const Type &type, bool exactInherited) {
    llvm::Constant *genericInfo;
    auto &notype = type.withoutBoxAndOptional();
    auto isOptional = type.withoutBox().type() == TypeType::Optional;
    if (isOptional && (notype.type() == TypeType::GenericVariable ||
                       notype.type() == TypeType::LocalGenericVariable)) {
        throw CompilerError(fg_->position(), "Optional generic variables as generic type arguments are not "
                            "supported yet (see issue #186).");
    }
    switch (notype.type()) {
        case TypeType::Class:
            genericInfo = buildConstant00Gep(fg_->typeHelper().classInfo(), notype.klass()->classInfo(), fg_->ctx());
            break;
        case TypeType::Protocol:
            genericInfo = notype.protocol()->rtti();
            break;
        case TypeType::ValueType:
        case TypeType::Enum:
            genericInfo = buildConstant00Gep(fg_->typeHelper().boxInfo(), fg_->boxInfoFor(notype), fg_->ctx());
            break;
        case TypeType::Something:
            genericInfo = fg_->generator()->runTime().somethingRtti();
            break;
        case TypeType::Someobject:
            genericInfo = fg_->generator()->runTime().someobjectRtti();
            break;
        case TypeType::GenericVariable:
            if (!fg_->calleeType().is<TypeType::TypeAsValue>() &&
                fg_->calleeType().typeDefinition()->isGenericDynamismDisabled()) {
                throw CompilerError(fg_->position(), "Generic dynamism is disabled in this type.");
            }
            addDynamic(extractTypeDescriptionPtr(), notype.genericVariableIndex());
            return;
        case TypeType::LocalGenericVariable:
            addDynamic(fg_->functionGenericArgs(), notype.genericVariableIndex());
            return;
        case TypeType::Callable:
        case TypeType::TypeAsValue:
        case TypeType::MultiProtocol:
            genericInfo = fg_->generator()->runTime().somethingRtti();
            fg_->compiler()->warn(SourcePosition(), "Run-time type information for multiprotocols, callables and type "\
                                  "values is not available yet. Casts and other reflection may not behave as "\
                                  "expected with these types.");
            break;
        default:
            throw std::logic_error("Cannot create type description for compile-time type.");
    }

    // A class that is named in its own superclass arguments would have an infinite description. Where it recurs within
    // those arguments it is described as something, which has no arguments.
    auto recurs = notype.type() == TypeType::Class &&
        std::find(expandingSuper_.begin(), expandingSuper_.end(), notype.klass()) != expandingSuper_.end();
    if (recurs) {
        genericInfo = fg_->generator()->runTime().somethingRtti();
    }

    auto strct = llvm::ConstantStruct::get(fg_->typeHelper().typeDescription(), {
        genericInfo,
        isOptional ? llvm::ConstantInt::getTrue(fg_->ctx()) : llvm::ConstantInt::getFalse(fg_->ctx()),
        fg_->generator()->valueWitnesses().witnessFor(type),
    });
    types_.emplace_back(strct);

    if (recurs || !notype.canHaveGenericArguments()) return;
    auto &args = notype.completeGenericArguments();
    auto superCount = notype.type() == TypeType::Class ? notype.klass()->offset() : 0;
    if (superCount > 0 && !exactInherited) expandingSuper_.emplace_back(notype.klass());
    for (size_t i = 0; i < args.size(); i++) {
        if (i == superCount && superCount > 0 && !exactInherited) expandingSuper_.pop_back();
        addType(i < superCount ? resolveSuperArgument(args[i], args, i, superCount, notype.klass()) : args[i]);
    }
    if (args.size() <= superCount && superCount > 0 && !exactInherited) expandingSuper_.pop_back();
}

llvm::Value* TypeDescriptionGenerator::extractTypeDescriptionPtr() {
    if (fg_->calleeType().is<TypeType::TypeAsValue>()) {
        return fg_->genericArgsPtr();
    }
    auto ptr = fg_->builder().CreateLoad(fg_->genericArgsType(), fg_->genericArgsPtr());
    if (fg_->calleeType().is<TypeType::Class>()) {
        return fg_->builder().CreateExtractValue(ptr, 0);
    }
    auto type = fg_->typeHelper().managable(fg_->typeHelper().typeDescription());
    return fg_->builder().CreateConstInBoundsGEP2_32(type, ptr, 0, 1);
}

llvm::Value* TypeDescriptionGenerator::entryFor(const Type &otype) {
    auto type = otype.unboxed();
    llvm::Value *gargs;
    if (type.type() == TypeType::LocalGenericVariable) {
        gargs = fg_->functionGenericArgs();
    }
    else {
        assert(type.type() == TypeType::GenericVariable);
        if (!fg_->calleeType().is<TypeType::TypeAsValue>() &&
            fg_->calleeType().typeDefinition()->isGenericDynamismDisabled()) {
            throw CompilerError(fg_->position(), "Generic dynamism is disabled in this type.");
        }
        gargs = extractTypeDescriptionPtr();
    }
    auto index = type.genericVariableIndex();
    if (index == 0) {
        return gargs;
    }
    return fg_->builder().CreateCall(fg_->generator()->runTime().indexTypeDescription(), { gargs, fg_->int64(index) });
}

void TypeDescriptionGenerator::addDynamic(llvm::Value *gargs, size_t index) {
    dynamic_++;
    auto idf = fg_->generator()->runTime().indexTypeDescription();
    auto td = index > 0 ? fg_->builder().CreateCall(idf, { gargs, fg_->int64(index) }) : gargs;
    auto size = fg_->builder().CreateCall(fg_->generator()->runTime().typeDescriptionLength(), td);
    types_.emplace_back(td, size);
}

llvm::Value* TypeDescriptionGenerator::generate(const std::vector<Type> &types) {
    assert(types_.empty());
    for (auto &type : types) {
        addType(type);
    }
    return finish();
}

llvm::Value* TypeDescriptionGenerator::generate(const std::vector<std::shared_ptr<ASTType>> &types) {
    assert(types_.empty());
    for (auto &type : types) {
        addType(type->type());
    }
    return finish();
}

llvm::Value* TypeDescriptionGenerator::generate(const Type &type) {
    assert(types_.empty());
    addType(type, user_ == User::TypeValue);
    return finish();
}

llvm::Value* TypeDescriptionGenerator::finish() {
    if (dynamic_ == 0) return finishStatic();

    llvm::Value *size = fg_->int64(types_.size() - dynamic_);
    for (auto &tdv : types_) {
        if (tdv.isCopy()) {
            size = fg_->builder().CreateAdd(size, tdv.size);
        }
    }

    llvm::Value *current, *alloc;
    auto typeDesc = fg_->typeHelper().typeDescription();
    if (user_ == User::Function || user_ == User::TypeValue) {
        stack_ = fg_->builder().CreateStackSave();
        current = alloc = fg_->builder().CreateAlloca(typeDesc, size);
    }
    else {
        auto allocSize = fg_->builder().CreateMul(fg_->sizeOf(typeDesc), size);
        if (user_ == User::Class) {
            current = alloc = fg_->builder().CreateCall(fg_->generator()->runTime().allocDescription(), allocSize);
        }
        else {
            auto size = fg_->builder().CreateAdd(fg_->sizeOf(fg_->typeHelper().pointer()), allocSize);
            alloc = fg_->builder().CreateCall(fg_->generator()->runTime().alloc(), size);
            auto type = fg_->typeHelper().managable(fg_->typeHelper().typeDescription());
            current = fg_->builder().CreateConstInBoundsGEP2_32(type, alloc, 0, 1);
        }
    }

    for (auto &tdv : types_) {
        if (tdv.isCopy()) {
            fg_->builder().CreateMemCpy(current, llvm::MaybeAlign(), tdv.from, llvm::MaybeAlign(),
                                        fg_->builder().CreateMul(fg_->sizeOf(typeDesc), tdv.size));
            current = fg_->builder().CreateInBoundsGEP(typeDesc, current, tdv.size);
        }
        else {
            fg_->builder().CreateStore(tdv.concrete, current);
            current = fg_->builder().CreateConstInBoundsGEP1_32(typeDesc, current, 1);
        }
    }
    if (user_ == User::Class) {
        auto sct = llvm::ConstantStruct::getAnon({ llvm::UndefValue::get(fg_->typeHelper().pointer()),
            llvm::ConstantInt::getFalse(fg_->ctx()) });;
        return fg_->builder().CreateInsertValue(sct, alloc, { 0 });
    }
    if (user_ == User::TypeValue) {
        auto value = fg_->builder().CreateCall(fg_->generator()->runTime().typeValue(), alloc);
        fg_->builder().CreateStackRestore(stack_);
        return value;
    }
    return alloc;
}

llvm::Value* TypeDescriptionGenerator::finishStatic() {
    auto typeDesc = fg_->typeHelper().typeDescription();
    auto type = llvm::ArrayType::get(typeDesc, types_.size());
    std::vector<llvm::Constant*> cargs;
    for (auto &arg : types_) {
        cargs.emplace_back(arg.concrete);
    }

    llvm::Constant *init = llvm::ConstantArray::get(type, cargs);
    if (user_ == User::ValueTypeOrValue) {
        init = llvm::ConstantStruct::getAnon({ fg_->generator()->runTime().ignoreBlockPtr(), init });
    }
    auto var = new llvm::GlobalVariable(*fg_->generator()->module(), init->getType(), true,
                                        llvm::GlobalValue::LinkageTypes::PrivateLinkage, init);
    var->setUnnamedAddr(llvm::GlobalVariable::UnnamedAddr::Global);

    if (user_ == User::ValueTypeOrValue) {
        return var;
    }
    auto gep = buildConstant00Gep(type, var, fg_->ctx());
    if (user_ == User::Class) {
        return llvm::ConstantStruct::getAnon({ gep, llvm::ConstantInt::getTrue(fg_->ctx()) });
    }
    return gep;
}

void TypeDescriptionGenerator::restoreStack() {
    assert(user_ == User::Function);
    if (stack_ != nullptr) {
        fg_->builder().CreateStackRestore(stack_);
    }
}

}  // namespace EmojicodeCompiler
