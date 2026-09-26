//
// Created by Theo Weidmann on 21.03.18.
//

#include "ASTUnsafeBlock.hpp"
#include "Functions/Function.hpp"
#include "Analysis/FunctionAnalyser.hpp"
#include "CompilerError.hpp"

namespace EmojicodeCompiler {

void ASTUnsafeBlock::analyse(FunctionAnalyser *analyser) {
    if (analyser->function()->enclosingSpecialization() != nullptr) {
        // Unsafe code can store values of generic types in memory that generic code reads, which expects them boxed.
        throw CompilerError(position(), "A specialization cannot contain ☣️ code.");
    }
    if (analyser->isInUnsafeBlock() && !analyser->isUnsafetyInherited()) {
        analyser->error(CompilerError(position(), "Already in a ☣️ block."));
        block_.analyse(analyser);
        return;
    }

    auto wasInUnsafeBlock = analyser->isInUnsafeBlock();
    analyser->setInUnsafeBlock(true);
    block_.analyse(analyser);
    analyser->setInUnsafeBlock(wasInUnsafeBlock);
}

}  // namespace EmojicodeCompiler
