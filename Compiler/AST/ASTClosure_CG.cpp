//
//  ASTClosure_CG.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 03/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTClosure.hpp"
#include "Generation/ClosureCodeGenerator.hpp"
#include "Generation/RunTimeHelper.hpp"
#include "Generation/TypeDescriptionGenerator.hpp"
#include "Compiler.hpp"
#include "Types/TypeContext.hpp"
#include "Functions/Function.hpp"
#include <llvm/Support/raw_ostream.h>

namespace EmojicodeCompiler {

Value* ASTClosure::generate(FunctionCodeGenerator *fg) const {
    if (closure_->isC()) {
        // A C function pointer: the closure captures nothing, so the function itself is the value.
        closure_->createUnspecificReification();
        fg->generator()->declareLlvmFunction(closure_.get());
        ClosureCodeGenerator(capture_, closure_.get(), fg->generator(), false).generate();
        return closure_->unspecificReification().function;
    }
    if (!allocatesOnStack() && !isEscaping_) {
        auto ce = CompilerError(position(), "Using non-escaping closure as escaping value.");
        ce.addNotes(position(), "Add 🎍🥡 after 🍇 to make closure escaping.");
        throw ce;
    }
    if (allocatesOnStack() && isEscaping_) {
        fg->compiler()->warn(position(), "Using escaping closure as non-escaping value.");
    }

    closure_->createUnspecificReification();
    fg->generator()->declareLlvmFunction(closure_.get());

    auto thisValue = capture_.capturesSelf() ? fg->thisValue()->getType() : nullptr;
    auto capture = capture_;
    for (auto &capturedVar : capture.captures) {
        capture.variableTypes.emplace_back(fg->scoper().getVariable(capturedVar.sourceId).type);
    }
    if (fg->functionGenericArgs() != nullptr) {
        for (auto function = closure_->enclosingFunction(); function != nullptr;
             function = function->enclosingFunction()) {
            if (!function->genericParameters().empty()) {
                capture.genericArgsOf = function;
                break;
            }
        }
    }
    capture.type = fg->generator()->typeHelper().llvmTypeForCapture(capture, thisValue, isEscaping_);

    ClosureCodeGenerator closureGenerator(capture, closure_.get(), fg->generator(), isEscaping_);
    closureGenerator.generate();

    auto alloc = storeCapturedVariables(fg, capture);
    auto callable = fg->builder().CreateInsertValue(llvm::UndefValue::get(fg->typeHelper().callable()),
                                                    closure_->unspecificReification().function, 0);
    return handleResult(fg, fg->builder().CreateInsertValue(callable, alloc, 1));
}

llvm::Value* ASTClosure::createDeinit(CodeGenerator *cg, const Capture &capture) const {
    auto deinit = llvm::Function::Create(cg->typeHelper().captureDeinit(),
                                         llvm::GlobalValue::LinkageTypes::PrivateLinkage, "captureDeinit",
                                         cg->module());
    deinit->setUnnamedAddr(llvm::GlobalVariable::UnnamedAddr::Global);

    FunctionCodeGenerator fg(deinit, cg, std::make_unique<TypeContext>(closure_->typeContext()));
    fg.createEntry();

    if (isEscaping_) {
        auto captures = deinit->args().begin();

        auto i = 2;
        if (capture.capturesSelf()) {
            auto ep = fg.builder().CreateConstInBoundsGEP2_32(capture.type, captures, 0, i++);
            fg.releaseByReference(ep, capture.self);
        }
        for (auto &capturedVar : capture.captures) {
            auto ptr = fg.builder().CreateConstInBoundsGEP2_32(capture.type, captures, 0, i++);
            if (capturedVar.type.isManaged()) {
                fg.releaseByReference(ptr, capturedVar.type);
            }
        }
        if (capture.genericArgsOf != nullptr) {
            auto ptr = fg.builder().CreateConstInBoundsGEP2_32(capture.type, captures, 0, i++);
            fg.builder().CreateCall(fg.generator()->runTime().free(),
                                    fg.builder().CreateLoad(fg.typeHelper().pointer(), ptr));
        }
    }

    fg.builder().CreateRetVoid();
    return deinit;
}

llvm::Value* ASTClosure::storeCapturedVariables(FunctionCodeGenerator *fg, const Capture &capture) const {
    auto captures = allocate(fg, capture.type);

    auto ep = fg->builder().CreateConstInBoundsGEP2_32(capture.type, captures, 0, 1);
    fg->builder().CreateStore(createDeinit(fg->generator(), capture), ep);

    auto i = 2;
    if (capture.capturesSelf()) {
        if (isEscaping_) fg->retain(fg->thisValue(), capture_.self);
        auto ep = fg->builder().CreateConstInBoundsGEP2_32(capture.type, captures, 0, i++);
        fg->builder().CreateStore(fg->thisValue(), ep);
    }

    if (isEscaping_) {
        for (auto &capturedVar : capture.captures) {
            Value *value;
            auto &variable = fg->scoper().getVariable(capturedVar.sourceId);

            if (capturedVar.type.isManaged() && fg->isManagedByReference(capturedVar.type)) {
                fg->retain(variable.ptr, capturedVar.type);
            }
            value = fg->builder().CreateLoad(variable.type, variable.ptr);
            if (capturedVar.type.isManaged() && !fg->isManagedByReference(capturedVar.type)) {
                fg->retain(value, capturedVar.type);
            }

            fg->builder().CreateStore(value, fg->builder().CreateConstInBoundsGEP2_32(capture.type, captures, 0, i++));
        }
    }
    else {
        for (auto &capturedVar : capture.captures) {
            auto &var = fg->scoper().getVariable(capturedVar.sourceId);
            auto ep = fg->builder().CreateConstInBoundsGEP2_32(capture.type, captures, 0, i++);
            fg->builder().CreateStore(var.ptr, ep);
        }
    }
    if (capture.genericArgsOf != nullptr) {
        llvm::Value *genericArgs = fg->functionGenericArgs();
        if (isEscaping_) {
            // The generic arguments are on the stack of the caller of the generic function, which the closure can
            // outlive, so it gets a copy, which its deinitializer frees.
            std::vector<Type> variables;
            for (size_t j = 0; j < capture.genericArgsOf->genericParameters().size(); j++) {
                variables.emplace_back(j, capture.genericArgsOf);
            }
            auto copy = TypeDescriptionGenerator(fg, TypeDescriptionUser::Class).generate(variables);
            genericArgs = fg->builder().CreateExtractValue(copy, 0);
        }
        fg->builder().CreateStore(genericArgs, fg->builder().CreateConstInBoundsGEP2_32(capture.type, captures, 0, i++));
    }
    return captures;
}

llvm::Function *ASTCallableBox::kRelease = nullptr;

llvm::Function* ASTCallableBox::getRelease(CodeGenerator *cg) {
    if (kRelease != nullptr) return kRelease;
    kRelease = llvm::Function::Create(cg->typeHelper().captureDeinit(),
                                      llvm::GlobalValue::LinkageTypes::LinkOnceAnyLinkage, "callableBoxRelease",
                                      cg->module());
    FunctionCodeGenerator fg(kRelease, cg, std::make_unique<TypeContext>());
    fg.createEntry();

    auto capture = kRelease->args().begin();
    auto callable = fg.builder().CreateConstInBoundsGEP2_32(fg.typeHelper().callableBoxCapture(), capture, 0, 2);
    fg.release(fg.builder().CreateLoad(fg.typeHelper().callable(), callable),
               Type(Type::noReturn(), {}, Type::noReturn()));

    fg.builder().CreateRetVoid();
    return kRelease;
}

llvm::Value* ASTCallableBox::generate(FunctionCodeGenerator *fg) const {
    thunk_->createUnspecificReification();
    fg->generator()->declareLlvmFunction(thunk_.get());

    ClosureCodeGenerator closureGenerator(thunk_.get(), fg->generator());
    closureGenerator.generate();

    auto captureIn = fg->typeHelper().callableBoxCapture();
    auto captures = allocate(fg, captureIn);

    fg->builder().CreateStore(expr_->generate(fg), fg->builder().CreateConstInBoundsGEP2_32(captureIn, captures, 0, 2));
    fg->builder().CreateStore(getRelease(fg->generator()),
                              fg->builder().CreateConstInBoundsGEP2_32(captureIn, captures, 0, 1));

    auto wcallable = fg->builder().CreateInsertValue(llvm::UndefValue::get(fg->typeHelper().callable()),
                                                     thunk_->unspecificReification().function, 0);
    return handleResult(fg, fg->builder().CreateInsertValue(wcallable, captures, 1));
}

llvm::Value* ASTCallableThunkDestination::generate(FunctionCodeGenerator *fg) const {
    return fg->thisValue();
}

}  // namespace EmojicodeCompiler
