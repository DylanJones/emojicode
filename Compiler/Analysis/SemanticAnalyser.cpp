//
// Created by Theo Weidmann on 26.02.18.
//

#include "SemanticAnalyser.hpp"
#include "AnalysisObserver.hpp"
#include <algorithm>
#include <set>
#include "Compiler.hpp"
#include "FunctionAnalyser.hpp"
#include "AST/ASTExpr.hpp"
#include "Scoping/Scope.hpp"
#include "Scoping/SemanticScoper.hpp"
#include "Types/TypeExpectation.hpp"
#include "Package/Package.hpp"
#include "ThunkBuilder.hpp"
#include "Parsing/SpecializationParser.hpp"
#include "Types/Class.hpp"
#include "Types/Protocol.hpp"
#include "Types/TypeDefinition.hpp"
#include "Types/ValueType.hpp"

namespace EmojicodeCompiler {

Compiler* SemanticAnalyser::compiler() const {
    return package_->compiler();
}

SemanticAnalyser::SemanticAnalyser(Package *package, bool imported) : package_(package), imported_(imported) {}

SemanticAnalyser::~SemanticAnalyser() = default;

void SemanticAnalyser::analyse(bool executable) {
    for (auto &vt : package_->valueTypes()) {
        finalizeProtocols(Type(vt.get()));
        vt->analyseConstraints(TypeContext(Type(vt.get())));
    }
    for (auto &klass : package_->classes()) {
        klass->analyseSuperType();
        finalizeProtocols(Type(klass.get()));
        klass->analyseConstraints(TypeContext(Type(klass.get())));
    }

    package_->recreateClassTypes();

    // Now all types are ready to be used with compatibleTo

    for (auto &protocol : package_->protocols()) {
        protocol->eachFunction([this](Function *function) {
            analyseFunctionDeclaration(function);
        });
    }
    for (auto &vt : package_->valueTypes()) {
        enqueueFunctionsOfTypeDefinition(vt.get());
        checkProtocolConformance(Type(vt.get()));
        declareInstanceVariables(Type(vt.get()));
    }
    for (auto &vt : package_->valueTypes()) {
        std::set<ValueType *> visited;
        if (storesInline(vt.get(), vt.get(), visited)) {
            throw CompilerError(vt->position(), Type(vt.get()).toString(TypeContext()), " contains itself through its "
                                "instance variables, so it would be infinitely large.");
        }
    }
    for (auto &klass : package_->classes()) {
        for (auto init : klass->inits().list()) {
            if (init->required()) {
                klass->typeMethods().add(buildRequiredInitThunk(klass.get(), init, this));
            }
        }

        enqueueFunctionsOfTypeDefinition(klass.get());
        klass->inherit(this);
        checkProtocolConformance(Type(klass.get()));

        if (!klass->hasSubclass() && !klass->exported()) {
            klass->setFinal();
        }
    }
    for (auto &function : package_->functions()) {
        enqueueFunction(function.get());
    }

    if (auto observer = compiler()->analysisObserver()) {
        observer->analysingFunctions(package_);
    }
    analyseQueue();
    checkStartFlagFunction(executable);
}

/// Bounds the specializations that specializations cause, as a function could otherwise specialize itself with ever
/// larger types, e.g. by calling itself with a list of its generic argument.
constexpr size_t kMaxSpecializationDepth = 8;

/// Whether generic code represents a value of @p type differently than code in which the generic variables are
/// replaced with concrete types: A value of a generic type is boxed, and a callable takes and returns values of generic
/// types boxed. Other types, like a list of a generic type, are represented the same.
static bool representationDependsOnGenericArguments(const Type &type) {
    auto unoptionalized = type.unboxed().unoptionalized();
    if (unoptionalized.unboxedType() == TypeType::GenericVariable ||
        unoptionalized.unboxedType() == TypeType::LocalGenericVariable) {
        return true;
    }
    return unoptionalized.type() == TypeType::Callable && unoptionalized.containsGenericVariables();
}

/// Whether an instance variable of @p typeDef is represented differently in specialized methods, which therefore
/// could not use the instance variables stored by generic code.
static bool storesGenericValues(TypeDefinition *typeDef) {
    return std::any_of(typeDef->instanceVariables().begin(), typeDef->instanceVariables().end(), [](auto &var) {
        return representationDependsOnGenericArguments(var.type->type());
    });
}

static bool isSpecializable(Function *function) {
    if (function->isExternal() || function->ast() == nullptr || function->isC() || function->isClosure() ||
        function->isThunk() || function->unsafe() || function->owner() == nullptr) {
        return false;
    }
    if (function->genericParameters().empty() && function->owner()->genericParameters().empty()) {
        return false;
    }
    if (!function->owner()->genericParameters().empty() && storesGenericValues(function->owner())) {
        return false;
    }
    // Only statically dispatched functions, as a virtual table has no entry per specialization.
    return function->functionType() == FunctionType::ValueTypeMethod ||
           function->functionType() == FunctionType::Function;
}

/// Appends @p types to @p arguments without reference and mutability. Returns false if one is not concrete.
static bool appendConcreteArguments(const std::vector<Type> &types, std::vector<Type> *arguments) {
    for (auto &argument : types) {
        if (argument.containsGenericVariables()) {
            return false;
        }
        Type type = argument;
        type.setReference(false);
        type.setMutable(false);
        arguments->emplace_back(type);
    }
    return true;
}

Function* SemanticAnalyser::specialize(Function *function, const Type &calleeType,
                                       const std::vector<Type> &genericArguments, Function *caller) {
    if (imported_ || function->package() != package_ || !isSpecializable(function)) {
        return nullptr;
    }
    auto owner = function->owner();
    auto callee = calleeType.unboxed();
    callee.setReference(false);
    callee.setMutable(false);
    auto genericOwner = !owner->genericParameters().empty();
    if (genericOwner && (!callee.canHaveGenericArguments() || callee.typeDefinition() != owner)) {
        return nullptr;
    }

    // The generic arguments of the type come first, followed by those of the function.
    std::vector<Type> arguments;
    if ((genericOwner && !appendConcreteArguments(callee.genericArguments(), &arguments)) ||
        !appendConcreteArguments(genericArguments, &arguments)) {
        return nullptr;
    }
    Function *existing;
    if (function->findSpecialization(arguments, &existing)) {
        return existing;
    }
    auto depth = caller == nullptr ? 1 : caller->specializationDepth() + 1;
    if (depth > kMaxSpecializationDepth) {
        return nullptr;
    }

    auto created = std::make_unique<Function>(function->name(), function->accessLevel(), function->final(),
                                              function->owner(), function->package(), function->position(), false,
                                              function->documentation(), function->deprecated(),
                                              function->mutating(), function->mood(), function->unsafe(),
                                              function->functionType(), function->isInline());
    created->setMemoryFlowTypeForThis(function->memoryFlowTypeForThis());
    size_t argument = 0;
    if (genericOwner) {
        for (auto &parameter : owner->genericParameters()) {
            created->bindVariable(parameter.name, arguments[argument++]);
        }
        // The instance variables have the same storage, but their types are resolved on the concrete type.
        auto &ownerScope = owner->instanceScope();
        auto scope = std::make_unique<Scope>(ownerScope.maxVariableId());
        for (auto &pair : ownerScope.map()) {
            auto &var = pair.second;
            scope->declareVariableWithId(var.name(), var.type().resolveOn(TypeContext(callee)), var.constant(),
                                         var.id(), var.position());
        }
        created->setSpecializedCallee(callee, std::move(scope));
    }
    for (auto &parameter : function->genericParameters()) {
        created->bindVariable(parameter.name, arguments[argument++]);
    }
    created->setSpecializationOf(function, arguments, depth);

    // Registered before the analysis, so that a recursive call uses the specialization too.
    auto specialization = created.get();
    function->setSpecialization(arguments, specialization);
    auto first = pendingSpecializations_.size();
    pendingSpecializations_.push_back(PendingSpecialization{ function, arguments, std::move(created) });

    if (!analyseSpecialization(specialization)) {
        // The specializations created while analysing it are dropped too, as they may call it.
        for (auto it = pendingSpecializations_.begin() + first; it != pendingSpecializations_.end(); it++) {
            it->generic->eraseSpecialization(it->arguments);
            unusedSpecializations_.emplace_back(std::move(it->specialization));
        }
        pendingSpecializations_.erase(pendingSpecializations_.begin() + first, pendingSpecializations_.end());
        function->setSpecialization(arguments, nullptr);
        return nullptr;
    }
    if (specializationTrials_ == 0) {
        for (auto &pending : pendingSpecializations_) {
            pending.generic->owner()->addSpecialization(std::move(pending.specialization));
        }
        pendingSpecializations_.clear();
    }
    return specialization;
}

bool SemanticAnalyser::analyseSpecialization(Function *specialization) {
    auto trapped = compiler()->trapsErrors();
    compiler()->setTrapsErrors(true);
    specializationTrials_++;
    bool compiled = true;
    try {
        SpecializationParser::parse(specialization->specializedFunction(), specialization);
        analyseFunctionDeclaration(specialization);
        FunctionAnalyser(specialization, this).analyse();
    }
    catch (CompilerError &) {
        compiled = false;
    }
    catch (Compiler::TrappedError &) {
        compiled = false;
    }
    specializationTrials_--;
    compiler()->setTrapsErrors(trapped);
    return compiled;
}

void SemanticAnalyser::checkStartFlagFunction(bool executable) {
    if (package_->hasStartFlagFunction()) {
        auto returnType = package_->startFlagFunction()->returnType()->type();
        if (returnType.type() != TypeType::NoReturn &&
            !returnType.compatibleTo(Type(package_->compiler()->sInteger), TypeContext())) {
            package_->compiler()->error(CompilerError(package_->startFlagFunction()->position(),
                                                      "🏁 must either have no return or return 🔢."));
        }
        if (!executable) {
            package_->startFlagFunction()->makeExternal();  // Prevent function from being included in object file
        }
    }
    else if (executable) {
        compiler()->error(CompilerError(SourcePosition(), "No 🏁 block was found."));
    }
}

void SemanticAnalyser::analyseQueue() {
    while (!queue_.empty()) {
        std::unique_ptr<FunctionAnalyser> analyser;
        try {
            analyser = std::make_unique<FunctionAnalyser>(queue_.front(), this);
            analyser->analyse();
        }
        catch (CompilerError &ce) {
            package_->compiler()->error(ce);
            auto observer = package_->compiler()->analysisObserver();
            if (observer != nullptr && analyser != nullptr) {
                observer->analysisFailed(analyser.get());
            }
        }
        queue_.pop();
    }
}

void SemanticAnalyser::enqueueFunctionsOfTypeDefinition(TypeDefinition *typeDef) {
    typeDef->eachFunction([this](Function *function) {
        enqueueFunction(function);
    });
}

void SemanticAnalyser::enqueueFunction(Function *function) {
    analyseFunctionDeclaration(function);
    if (!function->isExternal()) {
        queue_.emplace(function);
    }
}

void SemanticAnalyser::analyseFunctionDeclaration(Function *function) const {
    if (function->returnType() == nullptr) {
        function->setReturnType(std::make_unique<ASTLiteralType>(Type::noReturn()));
    }

    auto context = function->typeContext();

    if (function->errorType() == nullptr) {
        function->setErrorType(std::make_unique<ASTLiteralType>(Type::noReturn()));
    }
    auto errType = function->errorType()->analyseType(context);
    if (errType.type() != TypeType::NoReturn && !errType.compatibleTo(Type(compiler()->sError), context)) {
        throw CompilerError(function->errorType()->position(), "Error type must be a subclass of 🚧.");
    }

    function->analyseConstraints(context);
    for (auto &param : function->parameters()) {
        param.type->analyseType(context);
        if (!function->externalName().empty() && !function->isC() &&
            param.type->type().type() == TypeType::ValueType && !param.type->type().valueType()->isPrimitive()) {
            param.type->type().setReference();
        }
    }
    function->returnType()->analyseType(context, true);

    if (function->isC()) {
        checkCFunctionDeclaration(function);
    }
}

void SemanticAnalyser::checkCFunctionDeclaration(Function *function) const {
    if (function->errorProne()) {
        throw CompilerError(function->position(), "Functions with 🎍🌊 cannot raise errors.");
    }
    if (!function->genericParameters().empty() ||
        (function->owner() != nullptr && function->owner()->storesGenericArgs())) {
        throw CompilerError(function->position(), "Functions with 🎍🌊 cannot be generic.");
    }
    auto context = function->typeContext();
    for (auto &param : function->parameters()) {
        if (!param.type->type().isCRepresentable()) {
            throw CompilerError(param.type->position(), param.type->type().toString(context),
                                " cannot be used in a function with 🎍🌊.");
        }
    }
    auto &returnType = function->returnType()->type();
    if (returnType.type() != TypeType::NoReturn && !returnType.isCRepresentable()) {
        throw CompilerError(function->returnType()->position(), returnType.toString(context),
                            " cannot be returned from a function with 🎍🌊.");
    }
    // C structs are passed by value through trampolines, which only exist for calls from Emojicode into C.
    if (!function->isExternal()) {
        auto byValue = [](const Type &type) {
            return type.type() == TypeType::ValueType && type.valueType()->isCStruct();
        };
        if (byValue(returnType) || std::any_of(function->parameters().begin(), function->parameters().end(),
                                               [&](auto &param) { return byValue(param.type->type()); })) {
            throw CompilerError(function->position(), "C functions written in Emojicode cannot take or return C "
                                "structs by value. Pass a 📍 to the struct instead.");
        }
    }
}

bool SemanticAnalyser::storesInline(TypeDefinition *container, ValueType *target, std::set<ValueType *> &visited) {
    for (auto &ivar : container->instanceVariables()) {
        auto type = ivar.type->type();
        if (type.type() == TypeType::Optional) {
            type = type.optionalType();  // Optionals of value types store the value inline.
        }
        if (type.type() != TypeType::ValueType || type.isReference()) {
            continue;
        }
        auto valueType = type.valueType();
        if (valueType == target || (visited.insert(valueType).second && storesInline(valueType, target, visited))) {
            return true;
        }
    }
    return false;
}

void SemanticAnalyser::declareInstanceVariables(const Type &type) {
    TypeDefinition *typeDef = type.typeDefinition();

    auto context = TypeContext(type);
    auto scoper = std::make_unique<SemanticScoper>();
    scoper->pushScope();  // For closure analysis
    ExpressionAnalyser analyser(this, context, package_, std::move(scoper));

    auto cStruct = type.type() == TypeType::ValueType && type.valueType()->isCStruct();
    if (cStruct && !typeDef->genericParameters().empty()) {
        throw CompilerError(typeDef->position(), "A C struct (🎍🌊) cannot be generic.");
    }

    for (auto &var : typeDef->instanceVariablesMut()) {
        typeDef->instanceScope().declareVariable(var.name, var.type->analyseType(context), false,
                                                 var.position);

        if (cStruct && !var.type->type().isCRepresentable()) {
            throw CompilerError(var.position, var.type->type().toString(context),
                                " cannot be a field of a C struct (🎍🌊).");
        }
        if (var.expr != nullptr) {
            analyser.expectType(var.type->type(), &var.expr);
        }
    }

    // C structs are often only read from memory C wrote.
    if (!cStruct && !typeDef->instanceVariables().empty() && typeDef->inits().list().empty()) {
        package_->compiler()->warn(typeDef->position(), "Type defines ", typeDef->instanceVariables().size(),
                                   " instances variables but has no initializers.");
    }
}

bool SemanticAnalyser::checkReturnPromise(const Function *sub, const TypeContext &subContext,
                                          const Function *super, const TypeContext &superContext,
                                          const Type &superSource) const {
    auto superReturn = super->returnType()->type().resolveOn(superContext);
    auto subReturn = sub->returnType()->type().resolveOn(subContext);
    if (!subReturn.compatibleTo(superReturn, subContext)) {
        auto supername = superReturn.toString(subContext);
        auto thisname = sub->returnType()->type().toString(subContext);
        package_->compiler()->error(CompilerError(sub->position(), "Return type ",
                                                  sub->returnType()->type().toString(subContext), " of ",
                                                  utf8(sub->name()),
                                                  " is not compatible to the return type defined in ",
                                                  superSource.toString(subContext)));
    }
    return subReturn.storageType() == superReturn.storageType() && subReturn.isReference() == superReturn.isReference();
}

std::unique_ptr<Function> SemanticAnalyser::enforcePromises(Function *sub, Function *super,
                                                            const Type &superSource,
                                                            const TypeContext &subContext,
                                                            const TypeContext &superContext) {
    analyseFunctionDeclaration(sub);
    analyseFunctionDeclaration(super);
    if (super->final()) {
        package_->compiler()->error(CompilerError(sub->position(), superSource.toString(subContext),
                                                  "’s implementation of ", utf8(sub->name()), " was marked 🔏."));
    }
    if (sub->accessLevel() == AccessLevel::Private || (sub->accessLevel() == AccessLevel::Protected &&
            super->accessLevel() == AccessLevel::Public)) {
        package_->compiler()->error(CompilerError(sub->position(), "Overriding method must be as accessible or more ",
                                                  "accessible than the overridden method."));
    }

    bool isReturnOk = checkReturnPromise(sub, subContext, super, superContext, superSource);
    bool isParamsOk = checkArgumentPromise(sub, super, subContext, superContext) ;
    if (!isParamsOk || !isReturnOk) {
        auto thunk = buildBoxingThunk(superContext, super, sub);
        enqueueFunction(thunk.get());  // promises are enforced after calls to enqueueFunctionsOfTypeDefinition
        return thunk;
    }
    return nullptr;
}

bool SemanticAnalyser::checkArgumentPromise(const Function *sub, const Function *super, const TypeContext &subContext,
                                            const TypeContext &superContext) const {
    if (super->parameters().size() != sub->parameters().size()) {
        package_->compiler()->error(CompilerError(sub->position(), "Parameter count does not match."));
        return true;
    }

    bool compatible = true;
    for (size_t i = 0; i < super->parameters().size(); i++) { // More general arguments are OK
        auto superArgumentType = super->parameters()[i].type->type().resolveOn(superContext);
        if (!superArgumentType.compatibleTo(sub->parameters()[i].type->type().resolveOn(subContext), subContext)) {
            auto supertype = superArgumentType.toString(subContext);
            auto thisname = sub->parameters()[i].type->type().toString(subContext);
            package_->compiler()->error(CompilerError(sub->position(), "Type ", thisname, " of argument ", i + 1,
                                                      " is not compatible with its ", thisname, " argument type ",
                                                      supertype, "."));
        }
        if (sub->parameters()[i].type->type().resolveOn(subContext).storageType() != superArgumentType.storageType()) {
            compatible = false;  // Boxing Thunk required for parameter i
        }
    }
    return compatible;
}

void SemanticAnalyser::finalizeProtocol(const Type &type, ProtocolConformance &conformance) {
    auto protocol = conformance.type->type().unboxed();
    conformance.implementations.reserve(protocol.protocol()->methods().list().size());
    for (auto method : protocol.protocol()->methods().list()) {
        auto implementation = type.typeDefinition()->methods().lookup(method,
                                                                      TypeContext(conformance.type->type()),this);
        if (implementation == nullptr) {
            package_->compiler()->error(
                    CompilerError(conformance.type->position(), type.toString(TypeContext()),
                                  " does not conform to protocol ", protocol.toString(TypeContext()),
                                  ": Method ", utf8(method->name()), " not provided."));
            continue;
        }

        if (imported_) {
            continue;
        }

        implementation->createUnspecificReification();
        auto thunk = enforcePromises(implementation, method, protocol, TypeContext(type), TypeContext(protocol));
        if (thunk != nullptr) {
            conformance.implementations.emplace_back(thunk.get());
            type.typeDefinition()->methods().add(std::move(thunk));
        }
        else {
            conformance.implementations.emplace_back(implementation);
        }
    }
}

void SemanticAnalyser::checkProtocolConformance(const Type &type) {
    for (auto &protocol : type.typeDefinition()->protocols()) {
        finalizeProtocol(type, protocol);
    }
}

void SemanticAnalyser::finalizeProtocols(const Type &type) {
    // A type can conform to a protocol only once, even with different generic arguments.
    std::set<Protocol *> protocols;

    for (auto &protocol : type.typeDefinition()->protocols()) {
        auto &protocolType = protocol.type->analyseType(TypeContext(type));
        Type unboxed = protocolType.unboxed();
        if (!unboxed.is<TypeType::Protocol>()) {
            package_->compiler()->error(CompilerError(protocol.type->position(), "Type is not a protocol."));
            continue;
        }
        if (protocols.find(unboxed.protocol()) != protocols.end()) {
            package_->compiler()->error(CompilerError(protocol.type->position(),
                                                      "Conformance to protocol was already declared."));
            continue;
        }
        protocols.emplace(unboxed.protocol());
    }
}

Type SemanticAnalyser::defaultLiteralType(const Type &type) const {
    if (type.is<TypeType::IntegerLiteral>()) {
        return compiler()->sInteger->type();
    }
    if (type.is<TypeType::RealLiteral>()) {
        return compiler()->sReal->type();
    }
    if (type.is<TypeType::ListLiteral>()) {
        Type dtype = compiler()->sList->type();
        dtype.setGenericArgument(0, type.genericArguments()[0]);
        return dtype;
    }
    if (type.is<TypeType::DictionaryLiteral>()) {
        Type dtype = compiler()->sDictionary->type();
        dtype.setGenericArgument(0, type.genericArguments()[0]);
        return dtype;
    }
    return type;
}

}  // namespace EmojicodeCompiler
