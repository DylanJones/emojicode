//
//  CodeGenerator.hpp
//  Emojicode
//
//  Created by Theo Weidmann on 19/09/16.
//  Copyright © 2016 Theo Weidmann. All rights reserved.
//

#ifndef CodeGenerator_hpp
#define CodeGenerator_hpp

#include "LLVMTypeHelper.hpp"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <string>
#include <map>

namespace llvm {
class TargetMachine;
}  // namespace llvm

namespace EmojicodeCompiler {

class Compiler;
class Package;
class Class;
class Function;
class TypeDefinition;
class StringPool;
class RunTimeHelper;
class OptimizationManager;
struct Parameter;

/// Manages code generation.
///
/// A CodeGenerator instance is bound to a Compiler and always generates code for its main package including any
/// relevant declaration and inline functions from all imported packages.
/// Returns the attribute C compilers put on an integer argument or return value of the type: Integers narrower than
/// 32 bits are sign or zero extended. Returns llvm::Attribute::None for all other types.
llvm::Attribute::AttrKind cExtensionAttribute(const Type &type);

class CodeGenerator {
public:
    /// Creates a CodeGenerator bound to the provided Compiler.
    /// @param optimize Whether optimizations should be run.
    CodeGenerator(Compiler *compiler, bool optimize);

    /// Generates the package.
    void generate();

    /// Emits the generated code to `outPath`
    /// @param ir If this parameter is true, IR is emitted.
    /// @pre Call generate().
    void emit(bool ir, const std::string &outPath);

    /// The LLVM module that represents the package.
    llvm::Module* module() const { return module_.get(); }

    LLVMTypeHelper& typeHelper() { return typeHelper_; }
    StringPool& stringPool() { return *pool_; }
    RunTimeHelper& runTime() { return *runTime_; }
    llvm::LLVMContext& context() { return context_; }

    Compiler* compiler() const;

    /// Queries the DataLayout of the module for the size of this type in bytes.
    /// @note Use this method only when absolutely necessary. Prefer FunctionCodeGenerator::sizeOf in all function
    /// code.
    uint64_t querySize(llvm::Type *type) const;

    /// Determines a box info value that is used to identify values of the provided type in a box.
    /// @returns An LLVM value representing the box info that must be stored in the box info field.
    llvm::Constant* boxInfoFor(const Type &type);

    /// Declares an LLVM function for each reification of the provided function.
    void declareLlvmFunction(Function *function);

    ~CodeGenerator();

private:
    Compiler *const compiler_;
    llvm::LLVMContext context_;
    std::unique_ptr<llvm::Module> module_;

    LLVMTypeHelper typeHelper_;
    std::unique_ptr<StringPool> pool_;
    std::unique_ptr<RunTimeHelper> runTime_;
    std::unique_ptr<OptimizationManager> optimizationManager_;

    llvm::TargetMachine *targetMachine_ = nullptr;

    void generateFunctions(Package *package, bool imported);
    void generateFunction(Function *function);

    void addParamAttrs(const Parameter &param, size_t index, llvm::Function *function);
    /// Adds the sign and zero extension attributes C requires to the parameters and return value of a 🎍🌊 function.
    void addCExtensionAttributes(Function *function, llvm::Function *fn);
    /// Returns @p existing, a function already declared under the name of the 🎍🌊 function @p function.
    llvm::Function* reuseCFunction(Function *function, llvm::Function *existing, llvm::FunctionType *ft);
    /// The type of the trampoline of @p function. @see needsCTrampoline()
    llvm::FunctionType* cTrampolineFunctionType(Function *function);
    void addParamDereferenceable(const Type &type, size_t index, llvm::Function *function, bool ret);

    llvm::Function::LinkageTypes linkageForFunction(Function *function) const;
    llvm::Function* createLlvmFunction(Function *function, ReificationContext reificationContext);
};

llvm::Constant* buildConstant00Gep(llvm::Type *type, llvm::Constant *value, llvm::LLVMContext &context);

}  // namespace EmojicodeCompiler

#endif /* CodeGenerator_hpp */
