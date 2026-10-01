//
//  Class.c
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 05.01.15.
//  Copyright (c) 2015 Theo Weidmann. All rights reserved.
//

#include "Class.hpp"
#include "AST/ASTType.hpp"
#include "Analysis/SemanticAnalyser.hpp"
#include "Analysis/ThunkBuilder.hpp"
#include "Compiler.hpp"
#include "CompilerError.hpp"
#include "Functions/Initializer.hpp"
#include "Package/Package.hpp"
#include "TypeContext.hpp"
#include "Utils/StringUtils.hpp"
#include "Scoping/Scope.hpp"
#include <algorithm>
#include <utility>

namespace EmojicodeCompiler {

Class::Class(std::u32string name, Package *pkg, SourcePosition p, const std::u32string &documentation, bool exported,
             bool final, bool foreign) : TypeDefinition(std::move(name), pkg, p, documentation, exported),
                                         final_(final), foreign_(foreign) {}

std::vector<Type> Class::superGenericArguments() const {
    // The super type may be analysed but invalid, which analyseSuperType() reports.
    if (superType_ != nullptr && superType_->wasAnalysed() && superType_->type().type() == TypeType::Class) {
        return superType_->type().completeGenericArguments();
    }
    return std::vector<Type>();
}

void Class::analyseSuperTypeWhenUsed(std::vector<std::function<void()>> *constraintChecks) {
    if (!superTypeAnalysisStarted_) {
        analyseSuperType(constraintChecks);
    }
}

void Class::analyseSuperType(std::vector<std::function<void()>> *constraintChecks) {
    if (superType() == nullptr || superTypeAnalysed_) {
        return;
    }

    auto classType = Type(this);
    if (analysingSuperType_) {
        throw CompilerError(superType()->position(), Type(this).toString(TypeContext(classType)),
                            " inherits from itself.");
    }
    if (superTypeAnalysisStarted_) {
        return;  // A subclass is used in the superclass type, e.g. as generic argument, which is not circular.
    }
    superTypeAnalysisStarted_ = true;
    // The generic parameters of this class follow the generic arguments of its superclass, which may mention them. So
    // the superclass's superclass is analysed and the indices are offset before the superclass type is analysed.
    if (auto typeId = dynamic_cast<ASTTypeId *>(superType())) {
        auto rawType = typeId->rawType();
        if (rawType.type() == TypeType::Class) {
            analysingSuperType_ = true;
            rawType.klass()->analyseSuperType(constraintChecks);
            analysingSuperType_ = false;
            offsetIndicesBy(rawType.klass()->offset() + typeId->genericArgumentCount());
        }
    }

    auto &type = superType()->analyseType(TypeContext(TypeContext(classType), constraintChecks), false, false, true);

    if (type.type() != TypeType::Class) {
        throw CompilerError(superType()->position(), "The superclass must be a class.");
    }

    if (type.klass()->final()) {
        package()->compiler()->error(CompilerError(superType()->position(), type.toString(TypeContext(classType)),
                                                  " can’t be used as superclass as it was marked with 🔏."));
    }
    type.klass()->setHasSubclass();
    superTypeAnalysed_ = true;
}

Function* Class::findSuperFunction(Function *function, SemanticAnalyser *analyser) {
    Function *f;
    switch (function->functionType()) {
        case FunctionType::ObjectMethod:
        case FunctionType::Deinitializer:
            f = superclass()->methods().lookup(function, TypeContext(type()), analyser);
            break;
        case FunctionType::ClassMethod:
            f = superclass()->typeMethods().lookup(function, TypeContext(type()), analyser);
            break;
        case FunctionType::ObjectInitializer:
            f = superclass()->inits().lookup(function, TypeContext(type()), analyser);
            if (f != nullptr && !static_cast<Initializer*>(f)->required()) return nullptr;
            break;
        default:
            throw std::logic_error("Function of unexpected type in class");
    }
    if (f == nullptr) return f;
    if (!f->genericParameters().empty()) return nullptr;
    if (f->accessLevel() == AccessLevel::Private) return nullptr;
    return f;
}

void Class::inherit(SemanticAnalyser *analyser) {
    if (superType() == nullptr) {
        analyser->declareInstanceVariables(Type(this));
        eachFunction([](auto *function) {
            if (function->accessLevel() == AccessLevel::Default) {
                function->setAccessLevel(AccessLevel::Public);
            }
        });
        return;
    }

    methods().setSuper(&superclass()->methods());
    typeMethods().setSuper(&superclass()->typeMethods());
    if (instanceVariables().empty() && inits().list().empty()) {
        inits().setSuper(&superclass()->inits());
        // Inherited initializers still need a local allocation thunk: a type value must construct this class.
        auto declarator = superclass();
        while (declarator->inits().list().empty() && declarator->superclass() != nullptr) {
            declarator = declarator->superclass();
        }
        for (auto init : declarator->inits().list()) {
            if (init->required()) {
                auto thunk = typeMethods().add(buildRequiredInitThunk(this, init, analyser));
                analyser->enqueueFunction(thunk);
            }
        }
    }

    instanceScope() = superclass()->instanceScope();
    instanceScope().markInherited();

    analyser->declareInstanceVariables(Type(this));

    Type classType = Type(this);
    instanceVariablesMut().insert(instanceVariables().begin(), superclass()->instanceVariables().begin(),
                                  superclass()->instanceVariables().end());

    protocols_.reserve(superclass()->protocols().size());
    for (auto &protocol : superclass()->protocols()) {
        auto find = std::find_if(protocols_.begin(), protocols_.end(), [&](auto &a) {
            return a.type->type().identicalTo(protocol.type->type(), TypeContext(classType), nullptr);
        });
        if (find != protocols_.end()) {
            analyser->compiler()->error(CompilerError(position(), "Superclass already declared conformance to ",
                                                      protocol.type->type().toString(TypeContext(classType)), "."));
        }
        ProtocolConformance conf = protocol;
        conf.implementations.clear();
        protocols_.emplace_back(conf);
    }

    for (Initializer *init : superclass()->inits().list()) {
        if (!init->required() || inits().lookup(init, TypeContext(classType), analyser) != nullptr) {
            continue;
        }
        auto error = CompilerError(position(), "Required initializer ", utf8(init->name()), " was not implemented.");
        error.addNotes(init->position(), "Initializer is required here.");
        package()->compiler()->error(error);
    }

    eachFunctionWithoutInitializers([this, analyser](Function *function) {
        checkOverride(function, analyser);
    });
}

void Class::checkOverride(Function *function, SemanticAnalyser *analyser) {
    if (function->functionType() == FunctionType::Deinitializer) {
        return;
    }
    auto superFunction = findSuperFunction(function, analyser);
    // Required-initializer thunks override implicitly; enforce promises so a specializing thunk gets a boxing adapter.
    if (function->overriding() || (function->isThunk() && superFunction != nullptr)) {
        if (superFunction == nullptr) {
            throw CompilerError(function->position(), utf8(function->name()),
                                " was declared ✒️ but does not override anything.");
        }
        auto thunk = analyser->enforcePromises(function, superFunction, Type(superclass()),
                                               TypeContext(Type(this)), TypeContext(superType()->type()));
        if (function->accessLevel() == AccessLevel::Default) {
            function->setAccessLevel(superFunction->accessLevel());
        }
        if (thunk != nullptr) {
            function->setVirtualTableThunk(thunk.get());
            if (thunk->functionType() == FunctionType::ClassMethod) {
                typeMethods().add(std::move(thunk));
            }
            else {
                methods().add(std::move(thunk));
            }
        }
    }
    else if (superFunction != nullptr && !function->isThunk()) {
        analyser->compiler()->error(CompilerError(function->position(), "If you want to override ",
                                                  utf8(function->name()), " add ✒️."));
    }
    else if (function->accessLevel() == AccessLevel::Default) {
        function->setAccessLevel(AccessLevel::Public);
    }
    function->setSuperFunction(superFunction);
}

void Class::addInstanceVariable(const InstanceVariableDeclaration &declaration) {
    if (foreign()) {
        throw CompilerError(position(), "Instance variables are not allowed in foreign class.");
    }
    TypeDefinition::addInstanceVariable(declaration);
}

bool Class::canResolve(TypeDefinition *resolutionConstraint) const {
    if (auto cl = dynamic_cast<Class *>(resolutionConstraint)) {
        return inheritsFrom(cl);
    }
    return false;
}

bool Class::inheritsFrom(Class *from) const {
    for (const Class *a = this; a != nullptr; a = a->superclass()) {
        if (a == from) {
            return true;
        }
    }
    return false;
}

bool Class::storesGenericArgs() const {
    if (isGenericDynamismDisabled()) return false;
    return !genericParameters().empty() || offset() > 0;
}

}  // namespace EmojicodeCompiler
