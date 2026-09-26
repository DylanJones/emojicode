//
// Created by Theo Weidmann on 26.02.18.
//

#ifndef EMOJICODE_SEMANTICANALYSER_HPP
#define EMOJICODE_SEMANTICANALYSER_HPP

#include <queue>
#include <vector>
#include <functional>
#include <map>
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
    /// @param calleeType The type on which @p function is called. A method of a generic value type is specialized for
    /// the generic arguments of this type too.
    Function* specialize(Function *function, const Type &calleeType, const std::vector<Type> &genericArguments);

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
    /// Uses @p specialization once no specialization it calls is unfinished.
    void finishSpecialization(Function *specialization);
    /// Discards @p specialization and those that call it. If @p failed, it is not created anew.
    void discardSpecialization(Function *specialization, bool failed);
    void enqueueFunctionsOfTypeDefinition(TypeDefinition *typeDef);
    /// Analyses the protocols to which @p type conforms. Their generic arguments are checked by appending checks to
    /// @p constraintChecks.
    void finalizeProtocols(const Type &type, std::vector<std::function<void()>> *constraintChecks);
    void checkProtocolConformance(const Type &type);
    void finalizeProtocol(const Type &type, ProtocolConformance &conformance);
    void checkStartFlagFunction(bool executable);

    Package *package_;
    std::queue<Function *> queue_;
    /// The specializations by generic function and generic arguments, or nullptr if the function could not be
    /// specialized with them.
    std::map<std::pair<Function *, std::vector<Type>>, Function *> specializations_;
    /// Specializations that are analysed or wait for specializations they call to be analysed. They are owned here until
    /// they are used or discarded.
    std::map<Function *, std::unique_ptr<Function>> unfinishedSpecializations_;
    /// The specializations being analysed, the innermost last.
    std::vector<Function *> specializationStack_;
    /// The unfinished specializations that an unfinished specialization calls, which it can only be used with.
    std::map<Function *, std::set<Function *>> specializationDependencies_;
    /// Specializations that are not used, kept until no specialization is analysed as code analysed with them refers to
    /// them.
    std::vector<std::unique_ptr<Function>> unusedSpecializations_;
    /// Whether all declarations were analysed, before which no specialization can be analysed.
    bool declarationsAnalysed_ = false;
    bool imported_;

    bool checkArgumentPromise(const Function *sub, const Function *super, const TypeContext &subContext,
                                  const TypeContext &superContext) const;
    bool checkReturnPromise(const Function *sub, const TypeContext &subContext, const Function *super,
                            const TypeContext &superContext, const Type &superSource) const;
};

}  // namespace EmojicodeCompiler

#endif //EMOJICODE_SEMANTICANALYSER_HPP
