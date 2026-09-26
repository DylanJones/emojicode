//
//  FunctionCodeGenerator.hpp
//  Emojicode
//
//  Created by Theo Weidmann on 29/07/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#ifndef FunctionCodeGenerator_hpp
#define FunctionCodeGenerator_hpp

#include "CodeGenerator.hpp"
#include "Scoping/IDScoper.hpp"
#include <llvm/IR/IRBuilder.h>
#include <functional>
#include <queue>

namespace EmojicodeCompiler {

class FunctionCodeGenerator;
class Compiler;
class Function;
class TypeContext;
struct SourcePosition;

class TemporaryObjectsManager {
public:
    void addTemporaryObject(llvm::Value *value, const Type &type) {
        temporaryObjects_.emplace_back(value, type, false);
    }
    /// Registers a variable that holds the heap object storing a remote value in a temporary box, or null if no object
    /// was allocated. The object is released without deinitialization, as the value in it is a temporary of its own.
    void addTemporaryRemoteObject(llvm::Value *objectVariable) {
        temporaryObjects_.emplace_back(objectVariable, Type::noReturn(), true);
    }

    void releaseTemporaryObjects(FunctionCodeGenerator *fg, bool clearQueue, bool skipLast);

private:
    struct Temporary {
        Temporary(llvm::Value *value, Type type, bool remoteObject)
            : value(value), type(std::move(type)), remoteObject(remoteObject) {}
        llvm::Value *value;
        Type type;
        bool remoteObject;
    };

    std::vector<Temporary> temporaryObjects_;
};

/// A FunctionCodeGenerator instance is responsible for generating the LLVM IR for a single function.
///
/// It manages the declaration of arguments, the context of the function, the scoper and is responsible for keeping
/// track of temporary objects.
///
/// This class provides helper methods to simplify use of the LLVM API and for common actions inside the compiler’s
/// code base, as for example retrieving the class info from an object.
class FunctionCodeGenerator {
public:
    /// Constructs a FunctionCodeGenerator.
    FunctionCodeGenerator(Function *function, llvm::Function *llvmFunc, CodeGenerator *generator);

    /// Constructs a FunctionCodeGenerator for generating a function that does not have corresponding Function.
    /// @see createEntry()
    FunctionCodeGenerator(llvm::Function *llvmFunc, CodeGenerator *generator, std::unique_ptr<TypeContext> tc);

    /// Generates the code for the provided Function.
    /// @pre A Function must have been provided to the constructor.
    void generate();

    /// Creates the entry block and initializes the builder to it.
    void createEntry();

    CGScoper& scoper() { return scoper_; }
    Compiler* compiler() const;
    CodeGenerator* generator() const { return generator_; }
    llvm::IRBuilder<>& builder() { return builder_; }
    LLVMTypeHelper& typeHelper() { return generator()->typeHelper(); }
    llvm::LLVMContext& ctx() { return generator()->context(); }
    virtual llvm::Value* thisValue() const { return &*function_->args().begin(); }
    llvm::Type* llvmReturnType() const { return function_->getReturnType(); }
    llvm::Value* errorPointer() const { return &*(function_->args().end() - 1); }
    const Type& calleeType() const;
    const SourcePosition& position() const;

    void buildErrorReturn();

    llvm::Value* instanceVariablePointer(size_t id);
    /// @returns The LLVM type of the instance variable to which instanceVariablePointer() returns a pointer.
    llvm::Type* instanceVariableType(size_t id);
    llvm::Value* genericArgsPtr();
    /// @returns The LLVM type of the value to which genericArgsPtr() returns a pointer.
    /// @pre The function must not be a type method.
    llvm::Type* genericArgsType();
    llvm::Value* functionGenericArgs() const { return functionGenericArgs_; };

    /// @returns The number of bytes an instance of @c type takes up in memory.
    llvm::Value* sizeOf(llvm::Type *type);

    /// Converts @p value of type @p from to type @p to. Both types must have a C representation, or @p from must be
    /// 🧠 and @p to a C pointer, in which case the address of the memory's payload is returned.
    llvm::Value* buildCConversion(llvm::Value *value, const Type &from, const Type &to);

    /// Gets a pointer to the box info field of a box.
    /// @param box Pointer to a box.
    llvm::Value* buildGetBoxInfoPtr(llvm::Value *box);
    /// Gets a pointer to the value field of a box.
    /// @param box Pointer to a box.
    llvm::Value* buildGetBoxValuePtr(llvm::Value *box);
    /// Ensures that the box to which @p box points is the only box storing its value of the remote @p type, by copying
    /// the value into a new object if other boxes share the object storing it. Copies of a box share the object, so a
    /// value must be made unique before it is mutated in place.
    void makeRemoteBoxValueUnique(llvm::Value *box, const Type &type);
    /// Makes the box to which @p box points store its value in @p object, a managable of type @p managable, and returns
    /// a pointer to the value in the object.
    llvm::Value* buildSetRemoteBoxObject(llvm::Value *box, llvm::StructType *managable, llvm::Value *object);
    /// Builds an erased reference (see LLVMTypeHelper::erasedReference()) to @p address, which points to a value in
    /// memory of the type described by the type description entry @p entry, or to a box if @p entry is nullptr.
    llvm::Value* buildErasedReference(llvm::Value *address, llvm::Value *entry = nullptr);
    /// Returns the address to which the erased reference @p reference refers.
    llvm::Value* buildErasedReferenceAddress(llvm::Value *reference);
    /// Returns a pointer to a box that holds the value to which the erased @p reference, a reference to a box of
    /// @p type, refers. If the reference refers to a value in memory, the box holds a copy of it, which
    /// buildErasedReferenceWriteBack() writes back after a mutation and releases.
    llvm::Value* buildErasedReferenceBox(llvm::Value *reference, const Type &type);
    /// Writes the copy in @p box, obtained from buildErasedReferenceBox(), back to the memory to which @p reference
    /// refers and releases it, if it is a copy.
    void buildErasedReferenceWriteBack(llvm::Value *reference, llvm::Value *box, const Type &type, bool mutated);
    /// Returns a pointer to the type description entry of the type for which @p type, a generic variable, stands.
    llvm::Value* buildTypeDescriptionEntry(const Type &type);
    /// Returns the size of a value in memory of the type described by the type description entry @p entry.
    llvm::Value* buildValueSize(llvm::Value *entry);
    /// Returns a box of @p type (not a reference) with a copy of the value to which the erased @p reference refers,
    /// which the box owns.
    llvm::Value* buildLoadErased(llvm::Value *reference, const Type &type);
    /// Stores a copy of the value in @p box, a box of @p type, into memory at @p address, where values of the type
    /// described by @p entry are stored.
    void buildStoreErased(llvm::Value *address, llvm::Value *entry, llvm::Value *box, const Type &type);
    /// Releases the value at @p address, which is of the type described by @p entry.
    void buildReleaseErased(llvm::Value *address, llvm::Value *entry);
    /// Makes the value of the box to which @p box points unique, using the function of the protocol @p conformance,
    /// if the value is stored remotely. @p box must point to a variable that owns the box.
    void makeBoxValueUnique(llvm::Value *conformance, llvm::Value *box);
    /// Gets a pointer to a value of type `llvmType` that is stored after a value of type `after` in the value field
    /// of a box.
    /// @param box Pointer to a box.
    llvm::Value* buildGetBoxValuePtrAfter(llvm::Value *box, llvm::Type *llvmType, llvm::Type *after);
    llvm::Value* buildHasNoValueBox(llvm::Value *box);
    llvm::Value* buildHasNoValueBoxPtr(llvm::Value *box);
    llvm::Value* buildBoxWithoutValue();

    llvm::Value* buildOptionalHasNoValue(llvm::Value *simpleOptional, const Type &type);
    /// Determines whether the optional has a value.
    /// @param type An optional type.
    llvm::Value* buildOptionalHasValue(llvm::Value *simpleOptional, const Type &type);
    llvm::Value* buildOptionalHasValuePtr(llvm::Value *simpleOptional, const Type &type);
    llvm::Value* buildGetOptionalValuePtr(llvm::Value *simpleOptional, const Type &type);
    /// Creates an optional value that represents no value for the provided type.
    /// @param type An optional type.
    llvm::Value* buildSimpleOptionalWithoutValue(const Type &type);
    /// Creates an optional of the specified type that contains `value`.
    /// @param type An optional type.
    llvm::Value* buildSimpleOptionalWithValue(llvm::Value *value, const Type &type);
    /// Retrieves the value from an optional. If the optional does not have a value, the behavior is undefined.
    llvm::Value* buildGetOptionalValue(llvm::Value *value, const Type &type);

    /// Gets a pointer to the pointer to the class info of an object.
    /// @see getClassInfoFromObject
    llvm::Value* buildGetClassInfoPtrFromObject(llvm::Value *object);
    /// Gets a pointer to the class info of an object.
    /// @param object Pointer to the object from which the class info shall be obtained.
    /// @returns A llvm::Value* representing a pointer to a class info.
    llvm::Value* buildGetClassInfoFromObject(llvm::Value *object);

    llvm::Value* buildFindProtocolConformance(llvm::Value *box, llvm::Value *boxInfo, llvm::Value *protocolRTTI);

    llvm::ConstantInt* int8(int8_t value);
    llvm::ConstantInt* int16(int16_t value);
    llvm::ConstantInt* int32(int32_t value);
    llvm::ConstantInt* int64(int64_t value);

    llvm::Constant* boxInfoFor(const Type &type);

    void setVariable(size_t id, llvm::Value *value, const llvm::Twine &name = "");

    /// Allocates heap memory using the runtime library’s ejcAlloc.
    ///
    /// Allocates enough bytes to hold a value of type `type`.
    ///
    /// @note ejcAlloc expects the first element of the allocated type to be a pointer to the control
    /// block.
    llvm::Value* alloc(llvm::Type *type);
    /// Allocates stack memory as replacement for a heap memory allocation as performed by alloc().
    ///
    /// In order to ensure compatibility with the runtime library’s retain and release functions, additional bytes
    /// are allocated in front of the object.
    ///
    /// @note Like ejcAlloc, this function expects the first element of the allocated type to be a pointer to the
    /// control block.
    llvm::Value* stackAlloc(llvm::Type *type);

    /// @param managable The type of the managable (see LLVMTypeHelper::managable()) to which `managablePtr` points.
    llvm::Value* managableGetValuePtr(llvm::StructType *managable, llvm::Value *managablePtr);

    void release(llvm::Value *value, const Type &type);
    /// Like release() but the value to be released is always provided as a pointer. If isManagedByReference() returns
    /// fales for `type`, the value is loaded before it is passed to release().
    /// @param ptr Pointer to the value to be released.
    void releaseByReference(llvm::Value *ptr, const Type &type);
    void retain(llvm::Value *value, const Type &type);
    bool isManagedByReference(const Type &type) const;

    llvm::Value* createEntryAlloca(llvm::Type *type, const llvm::Twine &name = "");

    /// Creates an if-else branch condition. If the condition evaluates to true, the code produces by the @c then
    /// function is executed, otherwise the code produced by @c otherwise.
    void createIfElse(llvm::Value* cond, const std::function<void()> &then, const std::function<void()> &otherwise);
    /// Like createIfElse() but the if and else block only branch to a continue block if the respective functions
    /// return true.
    void createIfElseBranchCond(llvm::Value* cond, const std::function<bool()> &then,
                                const std::function<bool()> &otherwise);
    /// Creates an if block. The code produced by the @then function is only executed if the condition is true.
    void createIf(llvm::Value* cond, const std::function<void()> &then);

    llvm::BasicBlock* createBlock(const llvm::Twine &name = "");

    llvm::Value* createIfElsePhi(llvm::Value* cond, const std::function<llvm::Value* ()> &then,
                                 const std::function<llvm::Value *()> &otherwise);

    using PairIfElseCallback = std::function<std::pair<llvm::Value*, llvm::Value*> ()>;
    std::pair<llvm::Value*, llvm::Value*> createIfElsePhi(llvm::Value* cond, const PairIfElseCallback &then,
                                                          const PairIfElseCallback &otherwise);

    /// Registers a temporary value that must be released at the end of the statement.
    /// @param value The value that must be destroyed.
    /// @param type The type of the value.
    void addTemporaryObject(llvm::Value *value, const Type &type) {
        tom_.addTemporaryObject(value, type);
    }
    void addTemporaryRemoteObject(llvm::Value *objectVariable) {
        tom_.addTemporaryRemoteObject(objectVariable);
    }
    /// Releases all temporary values that were previously registered with addTemporaryObject() in the order
    /// they were added.
    /// @see addTemporaryObject
    void releaseTemporaryObjects(bool clearQueue = true, bool skipLast = false) {
        tom_.releaseTemporaryObjects(this, clearQueue, skipLast);
    }

    /// Returns the the TemporaryObjectsManager and resets the FunctionCodeGenerator’s internal one.
    TemporaryObjectsManager takeTemporaryObjectsManager() {
        TemporaryObjectsManager g;
        std::swap(g, tom_);
        return g;
    }

   ~FunctionCodeGenerator();

protected:
    virtual void declareArguments(llvm::Function *function);
    Function* function() const { return fn_; }
    void setFunctionGenericArgs(llvm::Value *value) { functionGenericArgs_ = value; }

private:
    Function *const fn_;
    llvm::Function *const function_;
    CGScoper scoper_;
    llvm::Value *functionGenericArgs_ = nullptr;
    llvm::Value *typeMethodGenericArgs_ = nullptr;

    CodeGenerator *const generator_;
    llvm::IRBuilder<> builder_;

    TemporaryObjectsManager tom_;

    std::unique_ptr<TypeContext> typeContext_;

    /// @param retain True if the box should be released, false if it should be retained.
    void manageBox(bool retain, llvm::Value *boxInfo, llvm::Value *value, const Type &type);

    void addParamAttrs(const Type &argType, llvm::Argument &llvmArg);

    /// @returns The LLVM struct type of the instance of the callee type.
    llvm::StructType* calleeStructType();
    unsigned instanceVariableIndex(size_t id) const;
};

}  // namespace EmojicodeCompiler

#endif /* FunctionCodeGenerator_hpp */
