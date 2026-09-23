//
//  ReferenceCountingPasses.hpp
//  runtime
//
//  Created by Theo Weidmann on 01.04.19.
//

#ifndef ReferenceCountingPasses_hpp
#define ReferenceCountingPasses_hpp

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include "RunTimeHelper.hpp"
#include <map>

namespace EmojicodeCompiler {

class ReferenceCountingPass {
public:
    explicit ReferenceCountingPass(RunTimeHelper *runTime) : runTime_(runTime) {}
    virtual ~ReferenceCountingPass() = default;

    llvm::PreservedAnalyses run(llvm::Function &function, llvm::FunctionAnalysisManager &) {
        return runOnFunction(function) ? llvm::PreservedAnalyses::none() : llvm::PreservedAnalyses::all();
    }
protected:
    RunTimeHelper *runTime_;
    std::vector<llvm::Instruction*> toBeDeleted_;
    bool modified_ = false;

    /// Called by this class’s implementation of runOnFunction() for every ejcRetain/Release family call.
    virtual void transformMemoryInst(llvm::CallInst *callInst) {}

    virtual bool runOnFunction(llvm::Function &function);

    /// Calls eraseFromParent() on all instructions in toBeDeleted_ and clears it.
    void deleteInstructions();

    bool isMemoryFunction(llvm::Function *function);
    bool isRetainFunction(llvm::Function *function);
    bool isReleaseFunction(llvm::Function *function);
};

/// Detects calls to the ejcRetain/Relase family with constant expresssions as argument and removes them.
class ConstantReferenceCountingPass : public ReferenceCountingPass,
    public llvm::PassInfoMixin<ConstantReferenceCountingPass> {
public:
    explicit ConstantReferenceCountingPass(RunTimeHelper *runTime) : ReferenceCountingPass(runTime) {}
private:
    void transformMemoryInst(llvm::CallInst *callInst) override;
};

/// This pass finds calls to the ejcRetain/Relase family where the argument is certainly stack allocated and replaces
/// the calls with either ejcReleaseLocal or code to directly increment the counter.
class LocalReferenceCountingPass : public ReferenceCountingPass,
    public llvm::PassInfoMixin<LocalReferenceCountingPass> {
public:
    explicit LocalReferenceCountingPass(RunTimeHelper *runTime) : ReferenceCountingPass(runTime) {}
private:
    void transformMemoryInst(llvm::CallInst *callInst) override;
};

/// This pass finds calls to the ejcRetain family where the counterpart call to a member of the ejcRelease family
/// is within the same block without other calls in-between and removes them.
///
/// This will also optimize transfer of ownership as in this example:
/// ```
/// call void @ejcRetain(i8* %18) #2, !noalias !0  ; will be removed
/// store %s.class_1f521* %16, %s.class_1f521** %20, align 8, !alias.scope !0
/// call void @ejcRelease(i8* %18)  ; will be removed
/// ret %_.class_1f41f* %1
/// ```
class RedundantReferenceCountingPass : public ReferenceCountingPass,
    public llvm::PassInfoMixin<RedundantReferenceCountingPass> {
public:
    explicit RedundantReferenceCountingPass(RunTimeHelper *runTime) : ReferenceCountingPass(runTime) {}
private:
    void transformBlock(llvm::BasicBlock &block);
    bool findCounterpart(llvm::CallInst *release, std::map<llvm::Value*, std::vector<llvm::CallInst*>> &retains);
    bool runOnFunction(llvm::Function &function) override;
};

}

#endif /* ReferenceCountingPasses_hpp */
