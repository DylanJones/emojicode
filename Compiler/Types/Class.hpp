//
//  Class.hpp
//  Emojicode
//
//  Created by Theo Weidmann on 09/01/16.
//  Copyright © 2016 Theo Weidmann. All rights reserved.
//

#ifndef Class_hpp
#define Class_hpp

#include "Functions/Initializer.hpp"
#include "TypeDefinition.hpp"
#include <functional>
#include <set>

namespace llvm {
class GlobalVariable;
class Constant;
}  // namespace llvm

namespace EmojicodeCompiler {

class Type;
class SemanticAnalyser;

class Class : public TypeDefinition {
public:
    Class(std::u32string name, Package *pkg, SourcePosition p, const std::u32string &documentation, bool exported,
          bool final, bool foreign);

    Type type() override { return Type(this); }

    /// The class's superclass.
    /// @returns TypeDefinition::superType().eclass(). Guaranteed to be @c nullptr if the class has no superclass.
    Class* superclass() const {
        if (superType() == nullptr) {
            return nullptr;
        }
        return superType()->type().klass();
    }

    std::vector<Type> superGenericArguments() const override;

    /// Sets the super type to the given Type.
    /// All generic arguments are offset by the number of generic arguments this type has.
    void setSuperType(std::unique_ptr<ASTType> type) { superType_ = std::move(type); }

    ASTType* superType() const { return superType_.get(); }

    /// @returns True iff this class inherits from @c from
    bool inheritsFrom(Class *from) const;
    /** Whether this class can be subclassed. */
    bool final() const { return final_; }
    bool foreign() const { return foreign_; }

    /// @returns The variable containing the class info or nullptr if none has been set yet.
    /// @see LLVMTypeHelper::classInfo
    llvm::GlobalVariable* classInfo() { return classInfo_; }
    void setClassInfo(llvm::GlobalVariable *classInfo) { classInfo_ = classInfo; }

    std::vector<llvm::Constant *>& virtualTable() { return virtualTable_; }

    bool canResolve(TypeDefinition *resolutionConstraint) const override;
    void addInstanceVariable(const InstanceVariableDeclaration &declaration) override;

    void inherit(SemanticAnalyser *analyser);
    /// Analyses the superclass. Its generic arguments are checked by appending checks to @p constraintChecks, as the
    /// constraints of the superclass and of this class may not be analysed yet.
    void analyseSuperType(std::vector<std::function<void()>> *constraintChecks);
    /// Analyses the superclass like analyseSuperType() if a type of this class is used while the super types are
    /// analysed, e.g. as generic argument of the superclass of a class declared earlier, as the type needs the generic
    /// arguments this class inherits. Does nothing if the superclass is being analysed already.
    void analyseSuperTypeWhenUsed(std::vector<std::function<void()>> *constraintChecks);

    /// Makes hasSubclass() return true.
    void setHasSubclass() { hasSubclass_ = true; }
    /// @returns true if this class has a subclass.
    /// @see setHasSubclass()
    bool hasSubclass() const { return hasSubclass_; }

    void setFinal() { final_ = true; }

    void setVirtualFunctionCount(size_t n) { virtualFunctionCount_ = n; }
    size_t virtualFunctionCount() { return virtualFunctionCount_; }

    Function* deinitializer() { return deinitializer_; }
    void setDeinitializer(Function *fn) { deinitializer_ = fn; }

    bool storesGenericArgs() const override;

private:
    std::unique_ptr<ASTType> superType_ = nullptr;

    /// Checks that @c function, if at all, is a valid override.
    /// @pre superclass() != nullptr
    /// @throws CompilerError if the override is improper, e.g. implicit
    void checkOverride(Function *function, SemanticAnalyser *analyser);

    Function *findSuperFunction(Function *function, SemanticAnalyser *analyser);

    std::vector<llvm::Constant *> virtualTable_;
    size_t virtualFunctionCount_ = 0;

    bool final_;
    bool foreign_;
    bool hasSubclass_ = false;
    /// Whether analyseSuperType() is analysing the superclasses of this class. Used to detect circular inheritance.
    bool analysingSuperType_ = false;
    /// Whether analyseSuperType() started. A type of this class used while it is not finished yet, e.g. as generic
    /// argument of its own superclass, lacks the generic arguments this class inherits.
    bool superTypeAnalysisStarted_ = false;
    /// Whether analyseSuperType() finished, which it must do only once as it offsets the generic parameter indices.
    bool superTypeAnalysed_ = false;

    llvm::GlobalVariable *classInfo_ = nullptr;

    Function *deinitializer_ = nullptr;
};

}  // namespace EmojicodeCompiler

#endif /* Class_hpp */
