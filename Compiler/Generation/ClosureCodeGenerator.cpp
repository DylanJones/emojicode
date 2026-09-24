//
//  ClosureCodeGenerator.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 21/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ClosureCodeGenerator.hpp"
#include "Functions/Function.hpp"
#include <llvm/IR/Function.h>

namespace EmojicodeCompiler {


ClosureCodeGenerator::ClosureCodeGenerator(const Capture &capture, Function *f, CodeGenerator *generator, bool escaping)
    : FunctionCodeGenerator(f, f->unspecificReification().function, generator),
    capture_(capture), escaping_(escaping) {}


ClosureCodeGenerator::ClosureCodeGenerator(Function *f, CodeGenerator *generator)
    : FunctionCodeGenerator(f, f->unspecificReification().function, generator), thunk_(true) {}

void ClosureCodeGenerator::declareArguments(llvm::Function *llvmFunction) {
    unsigned int i = 0;
    auto it = llvmFunction->args().begin();
    if (function()->isC()) {  // C callables have no captures.
        for (auto &arg : function()->parameters()) {
            auto &llvmArg = *(it++);
            setVariable(i++, &llvmArg);
            llvmArg.setName(utf8(arg.name));
        }
        return;
    }
    (it++)->setName("captures");
    for (auto &arg : function()->parameters()) {
        auto &llvmArg = *(it++);
        setVariable(i++, &llvmArg);
        llvmArg.setName(utf8(arg.name));
    }

    loadCapturedVariables(&*llvmFunction->args().begin());
}

void ClosureCodeGenerator::loadCapturedVariables(Value *value) {
    if (thunk_) {
        auto callable = builder().CreateConstInBoundsGEP2_32(typeHelper().callableBoxCapture(), value, 0, 2);
        thisValue_ = builder().CreateLoad(typeHelper().callable(), callable);
        return;
    }

    auto loadCapture = [&](unsigned index) {
        return builder().CreateLoad(capture_.type->getElementType(index),
                                    builder().CreateConstInBoundsGEP2_32(capture_.type, value, 0, index));
    };

    unsigned index = 2;
    if (capture_.capturesSelf()) {
        thisValue_ = loadCapture(index++);
    }
    for (size_t i = 0; i < capture_.captures.size(); i++) {
        auto &capture = capture_.captures[i];
        if (escaping_) {
            setVariable(capture.captureId, loadCapture(index++));
        }
        else {
            scoper().getVariable(capture.captureId) = CGVariable(loadCapture(index++), capture_.variableTypes[i]);
        }
    }
}

}  // namespace EmojicodeCompiler
