//
//  ValueWitnessBuilder.cpp
//  EmojicodeCompiler
//

#include "ValueWitnessBuilder.hpp"
#include "CodeGenerator.hpp"
#include "FunctionCodeGenerator.hpp"
#include "Mangler.hpp"
#include "RunTimeHelper.hpp"
#include "Types/Protocol.hpp"
#include "Types/Type.hpp"
#include "Types/TypeContext.hpp"

namespace EmojicodeCompiler {

namespace {

/// Stores @p value of @p type, whose storage type is simple, into the box to which @p box points.
void boxSimple(FunctionCodeGenerator &fg, llvm::Value *box, llvm::Value *value, const Type &type) {
    auto boxInfo = type.type() == TypeType::Someobject ? fg.generator()->runTime().boxInfoForObjects()
                                                       : fg.boxInfoFor(type);
    fg.builder().CreateStore(boxInfo, fg.buildGetBoxInfoPtr(box));
    if (fg.typeHelper().isRemote(type)) {
        auto managable = fg.typeHelper().managable(fg.typeHelper().llvmTypeFor(type));
        fg.builder().CreateStore(value, fg.buildSetRemoteBoxObject(box, managable, fg.alloc(managable)));
    }
    else {
        fg.builder().CreateStore(value, fg.buildGetBoxValuePtr(box));
    }
}

/// Returns the value of @p type, whose storage type is simple, in the box to which @p box points.
llvm::Value* unboxSimple(FunctionCodeGenerator &fg, llvm::Value *box, const Type &type) {
    auto llvmType = fg.typeHelper().llvmTypeFor(type);
    auto valuePtr = fg.buildGetBoxValuePtr(box);
    if (fg.typeHelper().isRemote(type)) {
        valuePtr = fg.builder().CreateLoad(fg.typeHelper().pointer(), valuePtr);
    }
    return fg.builder().CreateLoad(llvmType, valuePtr);
}

/// Retains the value of @p type at @p ptr.
void retainAt(FunctionCodeGenerator &fg, llvm::Value *ptr, const Type &type) {
    if (!type.isManaged()) {
        return;
    }
    fg.retain(fg.isManagedByReference(type) ? ptr : fg.builder().CreateLoad(fg.typeHelper().llvmTypeFor(type), ptr),
              type);
}

/// Whether the first field of a box of @p type is a protocol conformance, which boxes of the witness replace with the
/// box info of the value's type.
bool hasConformance(const Type &type) {
    return type.type() == TypeType::Box && type.boxedFor().type() == TypeType::Protocol;
}

llvm::Function* createWitnessFunction(CodeGenerator *generator, llvm::FunctionType *type, const std::string &name) {
    auto fn = llvm::Function::Create(type, llvm::GlobalValue::PrivateLinkage, name, generator->module());
    fn->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    fn->addFnAttr(llvm::Attribute::NoUnwind);
    return fn;
}

}  // namespace

llvm::Constant* ValueWitnessBuilder::witnessFor(const Type &otype) {
    auto type = otype.withMinimalBoxing();
    auto name = mangleTypeName(type) + ".valueWitness";
    auto existing = witnesses_.find(name);
    if (existing != witnesses_.end()) {
        return existing->second;
    }

    auto &typeHelper = generator_->typeHelper();
    auto size = generator_->querySize(typeHelper.llvmTypeFor(type));
    auto witness = llvm::ConstantStruct::get(typeHelper.valueWitness(), {
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(generator_->context()), size),
        buildLoad(type, name + ".load"),
        buildStore(type, name + ".store"),
        buildRelease(type, name + ".release"),
    });
    auto variable = new llvm::GlobalVariable(*generator_->module(), typeHelper.valueWitness(), true,
                                             llvm::GlobalValue::PrivateLinkage, witness, name);
    variable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    witnesses_.emplace(name, variable);
    return variable;
}

llvm::Function* ValueWitnessBuilder::buildLoad(const Type &type, const std::string &name) {
    auto fn = createWitnessFunction(generator_, generator_->typeHelper().valueWitnessCopy(), name);
    FunctionCodeGenerator fg(fn, generator_, std::make_unique<TypeContext>());
    fg.createEntry();
    auto raw = fn->getArg(0);
    auto box = fn->getArg(1);

    switch (type.storageType()) {
        case StorageType::Simple: {
            retainAt(fg, raw, type);
            boxSimple(fg, box, fg.builder().CreateLoad(fg.typeHelper().llvmTypeFor(type), raw), type);
            break;
        }
        case StorageType::SimpleOptional: {
            fg.createIfElse(fg.buildOptionalHasValuePtr(raw, type), [&] {
                auto valuePtr = fg.buildGetOptionalValuePtr(raw, type);
                auto contained = type.optionalType();
                retainAt(fg, valuePtr, contained);
                boxSimple(fg, box, fg.builder().CreateLoad(fg.typeHelper().llvmTypeFor(contained), valuePtr),
                          contained);
            }, [&] {
                fg.builder().CreateStore(fg.buildBoxWithoutValue(), box);
            });
            break;
        }
        case StorageType::PointerOptional: {
            auto value = fg.builder().CreateLoad(fg.typeHelper().llvmTypeFor(type), raw);
            fg.createIfElse(fg.buildOptionalHasValue(value, type), [&] {
                auto contained = type.optionalType();
                auto containedValue = fg.buildGetOptionalValue(value, type);
                if (contained.isManaged()) {
                    fg.retain(containedValue, contained);
                }
                boxSimple(fg, box, containedValue, contained);
            }, [&] {
                fg.builder().CreateStore(fg.buildBoxWithoutValue(), box);
            });
            break;
        }
        case StorageType::Box: {
            fg.builder().CreateStore(fg.builder().CreateLoad(fg.typeHelper().box(), raw), box);
            fg.retain(box, type);
            if (hasConformance(type)) {
                auto infoPtr = fg.buildGetBoxInfoPtr(box);
                auto conformance = fg.builder().CreateLoad(fg.typeHelper().pointer(), infoPtr);
                fg.createIf(fg.builder().CreateIsNotNull(conformance), [&] {
                    auto boxInfoPtr = fg.builder().CreateConstInBoundsGEP2_32(fg.typeHelper().protocolConformance(),
                                                                              conformance, 0, 2);
                    fg.builder().CreateStore(fg.builder().CreateLoad(fg.typeHelper().pointer(), boxInfoPtr), infoPtr);
                });
            }
            break;
        }
    }
    fg.builder().CreateRetVoid();
    return fn;
}

llvm::Function* ValueWitnessBuilder::buildStore(const Type &type, const std::string &name) {
    auto fn = createWitnessFunction(generator_, generator_->typeHelper().valueWitnessCopy(), name);
    FunctionCodeGenerator fg(fn, generator_, std::make_unique<TypeContext>());
    fg.createEntry();
    auto raw = fn->getArg(0);
    auto box = fn->getArg(1);

    switch (type.storageType()) {
        case StorageType::Simple: {
            fg.builder().CreateStore(unboxSimple(fg, box, type), raw);
            retainAt(fg, raw, type);
            break;
        }
        case StorageType::SimpleOptional:
        case StorageType::PointerOptional: {
            auto contained = type.optionalType();
            fg.createIfElse(fg.buildHasNoValueBoxPtr(box), [&] {
                fg.builder().CreateStore(fg.buildSimpleOptionalWithoutValue(type), raw);
            }, [&] {
                auto value = unboxSimple(fg, box, contained);
                fg.builder().CreateStore(fg.buildSimpleOptionalWithValue(value, type), raw);
                if (contained.isManaged()) {
                    fg.retain(fg.isManagedByReference(contained) ? fg.buildGetOptionalValuePtr(raw, type) : value,
                              contained);
                }
            });
            break;
        }
        case StorageType::Box: {
            fg.builder().CreateStore(fg.builder().CreateLoad(fg.typeHelper().box(), box), raw);
            if (hasConformance(type)) {
                auto boxInfo = fg.builder().CreateLoad(fg.typeHelper().pointer(), fg.buildGetBoxInfoPtr(box));
                fg.createIf(fg.builder().CreateIsNotNull(boxInfo), [&] {
                    auto rtti = type.boxedFor().protocol()->rtti();
                    auto conformance = fg.buildFindProtocolConformance(box, boxInfo, rtti);
                    fg.builder().CreateStore(conformance, fg.buildGetBoxInfoPtr(raw));
                });
            }
            fg.retain(raw, type);
            break;
        }
    }
    fg.builder().CreateRetVoid();
    return fn;
}

llvm::Function* ValueWitnessBuilder::buildRelease(const Type &type, const std::string &name) {
    auto ft = generator_->typeHelper().boxRetainRelease();
    auto fn = createWitnessFunction(generator_, ft, name);
    FunctionCodeGenerator fg(fn, generator_, std::make_unique<TypeContext>());
    fg.createEntry();
    if (type.isManaged()) {
        fg.releaseByReference(fn->getArg(0), type);
    }
    fg.builder().CreateRetVoid();
    return fn;
}

}  // namespace EmojicodeCompiler
