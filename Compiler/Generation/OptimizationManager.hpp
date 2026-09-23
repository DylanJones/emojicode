//
// Created by Theo Weidmann on 25.03.18.
//

#ifndef EMOJICODE_OPTIMIZATIONMANAGER_HPP
#define EMOJICODE_OPTIMIZATIONMANAGER_HPP

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <memory>

namespace llvm {
class Function;
class Module;
class TargetMachine;
}  // namespace llvm

namespace EmojicodeCompiler {

class RunTimeHelper;

class OptimizationManager {
public:
    OptimizationManager(bool optimize, RunTimeHelper *runTime, llvm::TargetMachine *targetMachine);
    void optimize(llvm::Function *function);
    void optimize(llvm::Module *module);
private:
    bool optimize_;
    RunTimeHelper *runTime_;

    llvm::LoopAnalysisManager lam_;
    llvm::FunctionAnalysisManager fam_;
    llvm::CGSCCAnalysisManager cgam_;
    llvm::ModuleAnalysisManager mam_;
    std::unique_ptr<llvm::PassBuilder> passBuilder_;
    llvm::FunctionPassManager functionPassManager_;
};

}  // namespace EmojicodeCompiler

#endif //EMOJICODE_OPTIMIZATIONMANAGER_HPP
