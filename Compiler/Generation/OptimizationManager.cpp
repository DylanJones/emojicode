//
// Created by Theo Weidmann on 25.03.18.
//

#include "OptimizationManager.hpp"
#include "ReferenceCountingPasses.hpp"
#include <llvm/Transforms/Scalar/EarlyCSE.h>
#include <llvm/Transforms/Scalar/LowerExpectIntrinsic.h>
#include <llvm/Transforms/Scalar/SROA.h>

namespace EmojicodeCompiler {

OptimizationManager::OptimizationManager(bool optimize, RunTimeHelper *runTime, llvm::TargetMachine *targetMachine)
        : optimize_(optimize), runTime_(runTime) {
    if (!optimize_) {
        return;
    }

    llvm::PipelineTuningOptions options;
    options.MergeFunctions = true;
    passBuilder_ = std::make_unique<llvm::PassBuilder>(targetMachine, options);
    // Retains and releases of inlined code are only redundant after inlining, and they block loop optimizations
    // until they are removed, so remove them whenever the pipeline cleans up instructions.
    passBuilder_->registerPeepholeEPCallback([runTime](llvm::FunctionPassManager &fpm, llvm::OptimizationLevel) {
        fpm.addPass(RedundantReferenceCountingPass(runTime));
    });
    passBuilder_->registerModuleAnalyses(mam_);
    passBuilder_->registerCGSCCAnalyses(cgam_);
    passBuilder_->registerFunctionAnalyses(fam_);
    passBuilder_->registerLoopAnalyses(lam_);
    passBuilder_->crossRegisterProxies(lam_, fam_, cgam_, mam_);

    // Clean up each function right after it was generated to keep the module small.
    functionPassManager_.addPass(llvm::SROAPass(llvm::SROAOptions::ModifyCFG));
    functionPassManager_.addPass(llvm::EarlyCSEPass());
    functionPassManager_.addPass(llvm::LowerExpectIntrinsicPass());
}

void OptimizationManager::optimize(llvm::Function *function) {
    if (optimize_) {
        functionPassManager_.run(*function, fam_);
    }
}

void OptimizationManager::optimize(llvm::Module *module) {
    if (!optimize_) {
        return;
    }

    // Analyses cached while optimizing single functions must not leak into the module pipeline.
    fam_.clear();

    llvm::ModulePassManager passManager;
    passManager.addPass(llvm::createModuleToFunctionPassAdaptor(LocalReferenceCountingPass(runTime_)));
    passManager.addPass(passBuilder_->buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3));

    llvm::FunctionPassManager referenceCounting;
    referenceCounting.addPass(ConstantReferenceCountingPass(runTime_));
    referenceCounting.addPass(RedundantReferenceCountingPass(runTime_));
    passManager.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(referenceCounting)));

    passManager.run(*module, mam_);
}

}  // namespace EmojicodeCompiler
