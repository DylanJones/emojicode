//
//  CodeGenerator.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 19/09/16.
//  Copyright © 2016 Theo Weidmann. All rights reserved.
//

#include "CodeGenerator.hpp"
#include "Compiler.hpp"
#include "CompilerError.hpp"
#include "RunTimeHelper.hpp"
#include "FunctionCodeGenerator.hpp"
#include "Mangler.hpp"
#include "OptimizationManager.hpp"
#include "Package/RecordingPackage.hpp"
#include "ReificationContext.hpp"
#include "StringPool.hpp"
#include "Types/Class.hpp"
#include "Types/ValueType.hpp"
#include "Types/TypeContext.hpp"
#include "Creator.hpp"
#include "RunTimeTypeInfoFlags.hpp"
#include <algorithm>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Transforms/IPO/StripDeadPrototypes.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>
#include <vector>

namespace EmojicodeCompiler {

CodeGenerator::CodeGenerator(Compiler *compiler, bool optimize)
: compiler_(compiler), typeHelper_(context(), this),
  module_(std::make_unique<llvm::Module>(compiler->mainPackage()->name(), context())),
  pool_(std::make_unique<StringPool>(this)), runTime_(std::make_unique<RunTimeHelper>(this)) {
    runTime_->declareRunTime();

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto targetTriple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
    if (target == nullptr) {
        throw std::domain_error("Could not find a target for " + targetTriple.str() + ": " + error);
    }

    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    targetMachine_ = target->createTargetMachine(targetTriple, cpu, features, opt, llvm::Reloc::PIC_);
    module()->setTargetTriple(targetTriple);
    module()->setDataLayout(targetMachine_->createDataLayout());

    optimizationManager_ = std::make_unique<OptimizationManager>(optimize, runTime_.get(), targetMachine_);
}

CodeGenerator::~CodeGenerator() = default;

Compiler* CodeGenerator::compiler() const {
    return compiler_;
}

uint64_t CodeGenerator::querySize(llvm::Type *type) const {
    return module()->getDataLayout().getTypeAllocSize(type);
}

llvm::Constant *CodeGenerator::boxInfoFor(const Type &type) {
    if (type.type() == TypeType::Class) {
        return runTime_->boxInfoForObjects();
    }
    if (type.type() == TypeType::Callable) {
        return runTime_->boxInfoForCallables();
    }
    if (type.type() == TypeType::TypeAsValue) {
        return compiler()->sInteger->boxInfo();
    }
    assert(type.type() == TypeType::ValueType || type.type() == TypeType::Enum);
    return type.valueType()->boxInfo();
}

llvm::Constant* buildConstant00Gep(llvm::Type *type, llvm::Constant *value, llvm::LLVMContext &context) {
    return llvm::ConstantExpr::getInBoundsGetElementPtr(type, value,
                                                        llvm::ArrayRef<llvm::Constant *> {
                                                            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
                                                            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0)
                                                        });
}

void CodeGenerator::generate() {
    for (auto package : compiler()->importedPackages()) {
        ImportedPackageCreator(package, this).generate();
    }
    PackageCreator(compiler()->mainPackage(), this).generate();

    for (auto package : compiler()->importedPackages()) {
        generateFunctions(package, true);
    }
    generateFunctions(compiler()->mainPackage(), false);

    optimizationManager_->optimize(module());
}

void CodeGenerator::emit(bool ir, const std::string &outPath) {
    std::error_code errorCode;
    llvm::raw_fd_ostream dest(outPath, errorCode, llvm::sys::fs::OF_None);

    if (ir) {
        llvm::LoopAnalysisManager lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager cgam;
        llvm::ModuleAnalysisManager mam;
        llvm::PassBuilder passBuilder(targetMachine_);
        passBuilder.registerModuleAnalyses(mam);
        passBuilder.registerCGSCCAnalyses(cgam);
        passBuilder.registerFunctionAnalyses(fam);
        passBuilder.registerLoopAnalyses(lam);
        passBuilder.crossRegisterProxies(lam, fam, cgam, mam);

        llvm::ModulePassManager pass;
        pass.addPass(llvm::VerifierPass(false));
        pass.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::PromotePass()));
        pass.addPass(llvm::StripDeadPrototypesPass());
        pass.run(*module(), mam);
        module()->print(dest, nullptr);
    }
    else {
        llvm::legacy::PassManager pass;
        pass.add(llvm::createVerifierPass(false));
        if (targetMachine_->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
            throw std::domain_error("TargetMachine can't emit a file of this type");
        }
        pass.run(*module());
    }
    dest.flush();
}

void CodeGenerator::generateFunctions(Package *package, bool imported) {
    for (auto &valueType : package->valueTypes()) {
        valueType->eachFunction([&](auto *function) {
            generateFunction(function);
        });
    }
    for (auto &klass : package->classes()) {
        klass->eachFunction([&](auto *function) {
            generateFunction(function);
        });
    }
    for (auto &function : package->functions()) {
        generateFunction(function.get());
    }
}

void CodeGenerator::generateFunction(Function *function) {
    if (!function->isExternal()) {
        function->eachReification([this, function](auto &reification) {
            typeHelper_.withReificationContext(ReificationContext(*function, reification), [&] {
                FunctionCodeGenerator(function, reification.entity.function, this).generate();
            });
            optimizationManager_->optimize(reification.entity.function);
        });
    }
}

llvm::Function* CodeGenerator::createLlvmFunction(Function *function, ReificationContext reificationContext) {
    llvm::FunctionType *ft;
    typeHelper().withReificationContext(reificationContext, [&] {
        ft = typeHelper().functionTypeFor(function);
    });
    auto name = function->externalName().empty() ? mangleFunction(function, reificationContext.arguments())
    : function->externalName();

    auto fn = llvm::Function::Create(ft, linkageForFunction(function), name, module());
    fn->addFnAttr(llvm::Attribute::NoUnwind);
    if (function->isInline()) {
        fn->addFnAttr(llvm::Attribute::InlineHint);
    }

    size_t i = function->isClosure() ? 1 : 0;
    if (hasThisArgument(function) && !function->isClosure()) {
        addParamDereferenceable(function->typeContext().calleeType(), i, fn, false);
        if (function->functionType() == FunctionType::ObjectInitializer ||
            function->functionType() == FunctionType::ValueTypeInitializer) {
            fn->addParamAttr(0, llvm::Attribute::NoAlias);
        }
        if (function->functionType() == FunctionType::ObjectInitializer) {
            if (!function->errorProne()) {
                fn->addParamAttr(i, llvm::Attribute::Returned);
                addParamDereferenceable(function->typeContext().calleeType(), 0, fn, true);
            }
        }
        else if (!function->memoryFlowTypeForThis().isEscaping()) {
            fn->addParamAttr(i, llvm::Attribute::getWithCaptureInfo(fn->getContext(), llvm::CaptureInfo::none()));
        }

        if (function->typeContext().calleeType().type() == TypeType::ValueType && !function->mutating()) {
            fn->addParamAttr(0, llvm::Attribute::ReadOnly);
        }

        i++;
    }
    else if (function->functionType() == FunctionType::ObjectInitializer && !function->errorProne()) {  // foreign initializers
        addParamDereferenceable(function->typeContext().calleeType(), 0, fn, true);
    }
    for (auto &param : function->parameters()) {
        addParamAttrs(param, i, fn);
        i++;
    }

    if (function->functionType() == FunctionType::ValueTypeInitializer) {
        if (function->typeContext().calleeType().typeDefinition()->storesGenericArgs()) {
            fn->addParamAttr(i, llvm::Attribute::NonNull);
            fn->addParamAttr(i, llvm::Attribute::ReadOnly);
            i++;
        }
    }
    if (!function->genericParameters().empty() && dynamic_cast<Class*>(function->owner()) == nullptr) {
        fn->addParamAttr(i, llvm::Attribute::NonNull);
        fn->addParamAttr(i, llvm::Attribute::getWithCaptureInfo(fn->getContext(), llvm::CaptureInfo::none()));
        fn->addParamAttr(i, llvm::Attribute::ReadOnly);
        i++;
    }
    if (function->errorProne()) {
        fn->addParamAttr(i, llvm::Attribute::NonNull);
        fn->addParamAttr(i, llvm::Attribute::getWithCaptureInfo(fn->getContext(), llvm::CaptureInfo::none()));
        fn->addParamAttr(i, llvm::Attribute::NoAlias);
        i++;
    }

    if (function->isClosure()) {
        fn->setUnnamedAddr(llvm::GlobalVariable::UnnamedAddr::Global);
    }

    addParamDereferenceable(function->returnType()->type(), 0, fn, true);
    return fn;
}

void CodeGenerator::declareLlvmFunction(Function *function) {
    if (function->externalName() == "ejcBuiltIn") {
        return;
    }
    function->eachReification([&](auto &reification) {
        reification.entity.function = createLlvmFunction(function, ReificationContext(*function, reification));
    });
}

void CodeGenerator::addParamAttrs(const Parameter &param, size_t index, llvm::Function *function) {
    if (!param.memoryFlowType.isEscaping() && param.type->type().type() == TypeType::Class) {
        function->addParamAttr(index, llvm::Attribute::getWithCaptureInfo(function->getContext(),
                                                                          llvm::CaptureInfo::none()));
    }

    addParamDereferenceable(param.type->type(), index, function, false);
}

void CodeGenerator::addParamDereferenceable(const Type &type, size_t index, llvm::Function *function, bool ret) {
    if (typeHelper_.isDereferenceable(type)) {
        auto size = querySize(typeHelper_.llvmTypeForPointee(type));
        if (ret) {
            function->addRetAttrs(llvm::AttrBuilder(context()).addDereferenceableAttr(size));
        }
        else {
            function->addDereferenceableParamAttr(index, size);
        }
    }
}

llvm::Function::LinkageTypes CodeGenerator::linkageForFunction(Function *function) const {
    if (function->isInline() && function->package()->isImported()) {
        return llvm::Function::AvailableExternallyLinkage;
    }
    if ((function->accessLevel() == AccessLevel::Private && !function->isExternal() &&
         (function->owner() == nullptr || !function->owner()->exported())) || function->isClosure()) {
        return llvm::Function::PrivateLinkage;
    }
    return llvm::Function::ExternalLinkage;
}

}  // namespace EmojicodeCompiler
