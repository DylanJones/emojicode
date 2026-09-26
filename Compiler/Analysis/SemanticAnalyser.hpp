//
// Created by Theo Weidmann on 26.02.18.
//

#ifndef EMOJICODE_SEMANTICANALYSER_HPP
#define EMOJICODE_SEMANTICANALYSER_HPP

#include <queue>
#include <memory>
#include <set>

namespace EmojicodeCompiler {

class Function;
class Package;
class TypeDefinition;
class Type;
class TypeContext;
class Compiler;
class Class;
struct ProtocolConformance;
struct SourcePosition;

/// Manages the semantic analysis of a package.
class ValueType;

class SemanticAnalyser {
public:
    explicit SemanticAnalyser(Package *package, bool imported);
    ~SemanticAnalyser();

    /// Analyses the package.
    /// @throws CompilerError if an unrecoverable error occurs, e.g. if the start flag function is not present but
    /// must be.
    /// @param executable True if this package will be linked to an executable. Requires, for instance, that a start
    /// flag function be present.
    void analyse(bool executable);

    void enqueueFunction(Function *);

    /// Returns the specialization of @p function for @p genericArguments, which the caller should call instead of
    /// the generic function, or nullptr if it must call the generic function.
    /// A specialization is created and analysed if it does not exist yet. If its code does not compile, e.g. because a
    /// cast of a value of a generic type is unnecessary with the concrete type, the generic function is used.
    /// @param caller The function whose code calls @p function, or nullptr.
    Function* specialize(Function *function, const std::vector<Type> &genericArguments, Function *caller);

    /// Iff `type` is a literal type, returns the default inferred type for the literal type. Otherwise the type is
    /// returned.
    Type defaultLiteralType(const Type &literal) const;

    Compiler* compiler() const;

    /// Checks that no promises were broken and builds a boxing layer to keep promises if necessary.
    /// @returns A function that can serve as boxing layer, if necessary, or nullptr.
    std::unique_ptr<Function> enforcePromises(Function *sub, Function *super, const Type &superSource,
                                              const TypeContext &subContext, const TypeContext &superContext);

    void analyseFunctionDeclaration(Function *function) const;
    /// Checks that a 🎍🌊 function only uses C types, cannot raise and is not generic.
    void checkCFunctionDeclaration(Function *function) const;

    void declareInstanceVariables(const Type &type);
    /// Whether @p container stores a @p target directly, or inside another value type it stores.
    static bool storesInline(TypeDefinition *container, ValueType *target, std::set<ValueType *> &visited);

private:
    void analyseQueue();
    /// Analyses @p specialization with trapped errors and returns whether it compiled.
    bool analyseSpecialization(Function *specialization);
    void enqueueFunctionsOfTypeDefinition(TypeDefinition *typeDef);
    void finalizeProtocols(const Type &type);
    void checkProtocolConformance(const Type &type);
    void finalizeProtocol(const Type &type, ProtocolConformance &conformance);
    void checkStartFlagFunction(bool executable);

    Package *package_;
    std::queue<Function *> queue_;
    struct PendingSpecialization {
        Function *generic;
        std::vector<Type> arguments;
        std::unique_ptr<Function> specialization;
    };
    /// Specializations that were created while analysing a specialization. They are only used if that specialization
    /// compiles, as they may call it.
    std::vector<PendingSpecialization> pendingSpecializations_;
    /// The number of specializations being analysed.
    size_t specializationTrials_ = 0;
    /// Specializations that are not used, which are kept as code analysed with them may refer to them.
    std::vector<std::unique_ptr<Function>> unusedSpecializations_;
    bool imported_;

    bool checkArgumentPromise(const Function *sub, const Function *super, const TypeContext &subContext,
                                  const TypeContext &superContext) const;
    bool checkReturnPromise(const Function *sub, const TypeContext &subContext, const Function *super,
                            const TypeContext &superContext, const Type &superSource) const;
};

}  // namespace EmojicodeCompiler

#endif //EMOJICODE_SEMANTICANALYSER_HPP
