//
//  BoxRetainReleaseBuilder.cpp
//  runtime
//
//  Created by Theo Weidmann on 30.03.19.
//

#include "BoxRetainReleaseBuilder.hpp"
#include "Types/Type.hpp"
#include "Types/TypeContext.hpp"
#include "Types/ValueType.hpp"
#include "Types/Class.hpp"
#include "Scoping/Scope.hpp"
#include "FunctionCodeGenerator.hpp"
#include "CallCodeGenerator.hpp"
#include "CodeGenerator.hpp"
#include "Mangler.hpp"
#include "RunTimeHelper.hpp"
#include "AST/ASTExpr.hpp"

namespace EmojicodeCompiler {

llvm::Function* createFunction(CodeGenerator *cg, const std::string &name) {
    auto fn = llvm::Function::Create(cg->typeHelper().boxRetainRelease(),
                                     llvm::GlobalValue::LinkageTypes::PrivateLinkage, name,
                                     cg->module());
    fn->setUnnamedAddr(llvm::GlobalVariable::UnnamedAddr::Global);
    return fn;
}

llvm::Function* createMemoryFunction(const std::string &str, CodeGenerator *cg, TypeDefinition *typeDef) {
    auto type = typeDef->type().is<TypeType::ValueType>() ? typeDef->type().referenced() : typeDef->type();
    auto ft = llvm::FunctionType::get(llvm::Type::getVoidTy(cg->context()), llvm::ArrayRef<llvm::Type*>{ cg->typeHelper().llvmTypeFor(type) }, false);
    auto fn = llvm::Function::Create(ft, llvm::GlobalValue::LinkageTypes::ExternalLinkage, str, cg->module());
    fn->setUnnamedAddr(llvm::GlobalVariable::UnnamedAddr::Global);
    return fn;
}

void buildCopyRetain(CodeGenerator *cg, ValueType *typeDef) {
    FunctionCodeGenerator fg(typeDef->copyRetain(), cg, std::make_unique<TypeContext>(Type(typeDef)));
    fg.createEntry();
    for (auto &decl : typeDef->instanceVariables()) {
        auto &var = typeDef->instanceScope().getLocalVariable(decl.name);
        if (var.type().isManaged()) {
            auto ptr = fg.instanceVariablePointer(var.id());
            if (!fg.isManagedByReference(var.type())) {
                ptr = fg.builder().CreateLoad(fg.instanceVariableType(var.id()), ptr);
            }
            fg.retain(ptr, var.type());
        }
    }

    if (typeDef->storesGenericArgs()) {
        auto genericArgs = fg.builder().CreateLoad(fg.genericArgsType(), fg.genericArgsPtr());
        fg.builder().CreateCall(fg.generator()->runTime().retain(), { genericArgs });
    }
    fg.builder().CreateRetVoid();
}

void buildDestructor(CodeGenerator *cg, TypeDefinition *typeDef) {
    FunctionCodeGenerator fg(typeDef->destructor(), cg, std::make_unique<TypeContext>(typeDef->type()));
    fg.createEntry();
    auto klass = dynamic_cast<Class *>(typeDef);
    if (klass != nullptr) {
        CallCodeGenerator ccg(&fg, CallType::StaticDispatch);
        for (auto aKlass = klass; aKlass != nullptr; aKlass = aKlass->superclass()) {
            if (auto deinit = aKlass->deinitializer()) {
                ccg.generate(fg.thisValue(), fg.calleeType(), ASTArguments(typeDef->position()), deinit, nullptr);
            }
        }
    }

    for (auto it = typeDef->instanceVariables().rbegin(); it < typeDef->instanceVariables().rend(); it++) {
        auto &decl = *it;
        auto &var = typeDef->instanceScope().getLocalVariable(decl.name);
        if (var.type().isManaged()) {
            fg.releaseByReference(fg.instanceVariablePointer(var.id()), var.type());
        }
    }

    if (typeDef->storesGenericArgs()) {
        auto val = fg.builder().CreateLoad(fg.genericArgsType(), fg.genericArgsPtr());
        if (fg.calleeType().is<TypeType::ValueType>()) {
            fg.builder().CreateCall(fg.generator()->runTime().releaseMemory(), { val });
        }
        else {
            fg.createIf(fg.builder().CreateIsNull(fg.builder().CreateExtractValue(val, { 1 })), [&] {
                fg.builder().CreateCall(fg.generator()->runTime().free(), { fg.builder().CreateExtractValue(val, { 0 }) });
            });
        }
    }
    fg.builder().CreateRetVoid();
}


std::pair<llvm::Function*, llvm::Function*> buildBoxRetainRelease(CodeGenerator *cg, const Type &type) {
    auto release = createFunction(cg, mangleBoxRelease(type));
    auto retain = createFunction(cg, mangleBoxRetain(type));

    FunctionCodeGenerator releaseFg(release, cg, std::make_unique<TypeContext>(type));
    releaseFg.createEntry();
    FunctionCodeGenerator retainFg(retain, cg, std::make_unique<TypeContext>(type));
    retainFg.createEntry();

    if (cg->typeHelper().isRemote(type)) {
        // The allocation of a remote value is counted even if the value has no managed contents.
        auto ptr = cg->typeHelper().pointer();
        auto mngType = cg->typeHelper().managable(cg->typeHelper().llvmTypeFor(type));

        auto objPtr = releaseFg.buildGetBoxValuePtrAfter(release->args().begin(), ptr, ptr);
        auto remotePtr = releaseFg.builder().CreateLoad(ptr, objPtr);
        if (type.isManaged()) {
            releaseFg.release(releaseFg.managableGetValuePtr(mngType, remotePtr), type);
        }
        releaseFg.builder().CreateCall(cg->runTime().releaseWithoutDeinit(), remotePtr);

        auto objPtrRetain = retainFg.buildGetBoxValuePtrAfter(retain->args().begin(), ptr, ptr);
        auto remotePtrRetain = retainFg.builder().CreateLoad(ptr, objPtrRetain);
        if (type.isManaged()) {
            retainFg.retain(retainFg.managableGetValuePtr(mngType, remotePtrRetain), type);
        }
        retainFg.builder().CreateCall(cg->runTime().retain(), remotePtrRetain);
    }
    else if (type.isManaged()) {
        if (!releaseFg.isManagedByReference(type)) {
            auto llvmType = cg->typeHelper().llvmTypeFor(type);
            auto objPtr = releaseFg.buildGetBoxValuePtr(release->args().begin());
            releaseFg.release(releaseFg.builder().CreateLoad(llvmType, objPtr), type);

            auto objPtrRetain = retainFg.buildGetBoxValuePtr(retain->args().begin());
            retainFg.retain(retainFg.builder().CreateLoad(llvmType, objPtrRetain), type);
        }
        else {
            auto objPtr = releaseFg.buildGetBoxValuePtr(release->args().begin());
            releaseFg.release(objPtr, type);

            auto objPtrRetain = retainFg.buildGetBoxValuePtr(retain->args().begin());
            retainFg.retain(objPtrRetain, type);
        }
    }

    releaseFg.builder().CreateRetVoid();
    retainFg.builder().CreateRetVoid();
    return std::make_pair(retain, release);
}

}
