//
//  Type.c
//  Emojicode
//
//  Created by Theo Weidmann on 04.03.15.
//  Copyright (c) 2015 Theo Weidmann. All rights reserved.
//

#include "Type.hpp"
#include <algorithm>
#include "Class.hpp"
#include "CommonTypeFinder.hpp"
#include "Emojis.h"
#include "Enum.hpp"
#include "Functions/Function.hpp"
#include "Package/Package.hpp"
#include "Protocol.hpp"
#include "TypeContext.hpp"
#include "Utils/StringUtils.hpp"
#include "Analysis/GenericInferer.hpp"
#include "ValueType.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace EmojicodeCompiler {

const std::u32string kDefaultNamespace = std::u32string(1, E_HOUSE_BUILDING);

/// Literal constraints (e.g. ⚪️) are analysed when parsed, so all constraints must be checked, not only the first.
template <typename T>
static bool constraintsAnalysed(const T *generic) {
    auto &params = generic->genericParameters();
    return !params.empty() && std::all_of(params.begin(), params.end(), [](auto &param) {
        return param.constraint->wasAnalysed();
    });
}

Type::Type(Protocol *protocol) : typeContent_(TypeType::Protocol), typeDefinition_(protocol) {}

Type::Type(std::vector<Type> protocols)
    : typeContent_(TypeType::MultiProtocol), genericArguments_(std::move(protocols)) {
    sortMultiProtocolType();
}

Type::Type(Enum *enumeration)
    : typeContent_(TypeType::Enum), typeDefinition_(enumeration) {}

Type::Type(ValueType *valueType)
: typeContent_(TypeType::ValueType), typeDefinition_(valueType), mutable_(false) {
    if (!constraintsAnalysed(valueType)) {
        return;
    }
    for (size_t i = 0; i < valueType->genericParameters().size(); i++) {
        genericArguments_.emplace_back(valueType->typeForVariable(i));
    }
}

Type::Type(Class *klass) : typeContent_(TypeType::Class), typeDefinition_(klass) {
    if ((klass->superType() != nullptr && !klass->superType()->wasAnalysed()) ||
        !constraintsAnalysed(klass)) {
        return;
    }
    genericArguments_ = klass->superGenericArguments();
    for (size_t i = klass->superGenericArguments().size();
         i < klass->superGenericArguments().size() + klass->genericParameters().size(); i++) { 
        genericArguments_.emplace_back(klass->typeForVariable(i));
    }
}

Type::Type(Function *function) : typeContent_(TypeType::Callable), cCallable_(function->isC()) {
    genericArguments_.reserve(function->parameters().size() + 2);
    genericArguments_.emplace_back(function->returnType()->type());
    genericArguments_.emplace_back(function->errorType()->type());
    for (auto &argument : function->parameters()) {
        genericArguments_.emplace_back(argument.type->type());
    }
}

Class* Type::klass() const {
    return dynamic_cast<Class *>(typeDefinition());
}

Protocol* Type::protocol() const {
    return dynamic_cast<Protocol *>(typeDefinition());
}

Enum* Type::enumeration() const {
    return dynamic_cast<Enum *>(typeDefinition());
}

ValueType* Type::valueType() const {
    return dynamic_cast<ValueType *>(typeDefinition());
}

TypeDefinition* Type::typeDefinition() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].typeDefinition();
    }
    assert(type() == TypeType::Class || type() == TypeType::Protocol || type() == TypeType::ValueType
           || type() == TypeType::Enum);
    return typeDefinition_;
}

Type Type::unboxed() const {
    if (type() == TypeType::Box) {
        auto copy = genericArguments_[0];
        copy.setMutable(mutable_);
        return copy;
    }
    return *this;
}

Type Type::boxedFor(const Type &forType) const  {
    return Type(MakeBoxType(), *this, forType.type() == TypeType::Box ? forType.genericArguments_[0] : forType);
}

bool Type::areMatchingBoxes(const Type &type, const TypeContext &context) const {
    return this->type() == TypeType::Box && type.type() == TypeType::Box &&
        boxedFor().identicalTo(type.boxedFor(), context, nullptr);
}

TypeType Type::unboxedType() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].typeContent_;
    }
    return typeContent_;
}

Type Type::applyMinimalBoxing() const {
    if (type() == TypeType::Something || type() == TypeType::Protocol || type() == TypeType::MultiProtocol) {
        return boxedFor(*this);
    }
    return *this;
}

Type Type::optionalized() const {
    if (unboxedType() == TypeType::Optional) {
        return *this;
    }
    if (type() == TypeType::Box) {
        auto t = *this;
        t.genericArguments_[0] = t.genericArguments_[0].optionalized();
        return t;
    }
    return Type(MakeOptionalType(), *this);
}

Type Type::optionalType() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].optionalType().boxedFor(boxedFor());
    }

    assert(type() == TypeType::Optional);
    return genericArguments_[0];
}

Type Type::typeOfTypeValue() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].typeOfTypeValue().boxedFor(boxedFor());
    }

    assert(type() == TypeType::TypeAsValue);
    return genericArguments_[0];
}

bool Type::isExact() const {
    return forceExact_ || (type() == TypeType::Class && klass()->final());
}

bool Type::canHaveGenericArguments() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].canHaveGenericArguments();
    }
    return type() == TypeType::Class || type() == TypeType::Protocol || type() == TypeType::ValueType
            || type() == TypeType::Enum || type() == TypeType::ListLiteral || type() == TypeType::DictionaryLiteral;
}

void Type::setGenericArguments(std::vector<Type> &&args) {
    if (type() == TypeType::Box) {
        genericArguments_[0].setGenericArguments(std::move(args));
    }
    else {
        assert(canHaveGenericArguments());
        genericArguments_ = args;
    }
}

bool Type::canHaveProtocol() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].canHaveProtocol();
    }
    return type() == TypeType::ValueType || type() == TypeType::Class || type() == TypeType::Enum;
}

void Type::sortMultiProtocolType() {
    assert(type() == TypeType::MultiProtocol);
    std::sort(genericArguments_.begin(), genericArguments_.end(), [](const Type &a, const Type &b) {
        return a.protocol() < b.protocol();
    });
}

size_t Type::genericVariableIndex() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].genericVariableIndex();
    }
    assert(type() == TypeType::GenericVariable || type() == TypeType::LocalGenericVariable);
    return genericArgumentIndex_;
}

TypeDefinition* Type::resolutionConstraint() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].resolutionConstraint();
    }
    assert(type() == TypeType::GenericVariable);
    return typeDefinition_;
}

Function* Type::localResolutionConstraint() const {
    if (type() == TypeType::Box) {
        return genericArguments_[0].localResolutionConstraint();
    }
    assert(type() == TypeType::LocalGenericVariable);
    return localResolutionConstraint_;
}

Type Type::resolveOnSuperArgumentsAndConstraints(const TypeContext &typeContext) const {
    if (type() == TypeType::Optional || type() == TypeType::Box) {
        return rewrapped(genericArguments_[0].resolveOnSuperArgumentsAndConstraints(typeContext));
    }

    Type t = *this;
    bool ref = isReference();
    bool mut = mutable_;

    TypeDefinition *c = nullptr;
    if (typeContext.calleeType().canHaveGenericArguments()) {
        c = typeContext.calleeType().typeDefinition();
    }
    else if (typeContext.calleeType().type() == TypeType::TypeAsValue) {
        c = typeContext.calleeType().typeOfTypeValue().typeDefinition();
    }

    if (c != nullptr) {
        // Try to resolve on the generic arguments to the superclass.
        while (t.unboxedType() == TypeType::GenericVariable &&
               t.genericVariableIndex() < c->superGenericArguments().size()) {
            t = c->superGenericArguments()[t.genericVariableIndex()];
        }
    }
    while (t.unboxedType() == TypeType::LocalGenericVariable && typeContext.function() != nullptr &&
           typeContext.function()->isWithin(t.localResolutionConstraint())) {
        t = t.localResolutionConstraint()->constraintForIndex(t.genericVariableIndex());
    }
    if (c != nullptr) {
        while (t.unboxedType() == TypeType::GenericVariable && c->canResolve(t.resolutionConstraint())) {
            t = c->constraintForIndex(t.genericVariableIndex());
        }
    }
    if (ref) {
        t.setReference();
    }
    if (mut) {
        t.setMutable(true);
    }
    return t;
}

std::vector<Type> Type::selfResolvedGenericArgs() const {
    TypeContext typeContext(*this);
    auto args = genericArguments_;
    for (auto &g : args) {
        g = g.resolveOn(typeContext);
    }
    return args;
}

Type Type::rewrapped(Type wrapped) const {
    if (type() == TypeType::Optional && (wrapped.type() == TypeType::Box || wrapped.type() == TypeType::Optional)) {
        // An optional must not contain a box, but a box an optional. Nor can an optional contain an optional, as 🍬🍬T
        // is 🍬T, so that a generic argument 🍬🔢 in 🍬Element must not make it 🍬🍬🔢.
        return wrapped.optionalized();
    }
    Type t = *this;
    t.genericArguments_[0] = type() == TypeType::Box ? wrapped.unboxed() : std::move(wrapped);
    return t;
}

Type Type::resolveOn(const TypeContext &typeContext) const {
    if (type() == TypeType::Optional || type() == TypeType::Box) {
        return rewrapped(genericArguments_[0].resolveOn(typeContext));
    }

    Type t = *this;
    auto keepStorage = [ref = isReference(), mut = mutable_](Type t) {
        if (ref) {
            t.setReference();
        }
        if (mut) {
            t.setMutable(true);
        }
        return t;
    };

    if (t.unboxedType() == TypeType::LocalGenericVariable && typeContext.function() == t.localResolutionConstraint()
        && typeContext.functionGenericArguments() != nullptr) {
        // The generic arguments are types of the calling code, which must not be resolved again. A recursive call
        // with its own generic variable would otherwise resolve it forever.
        return keepStorage((*typeContext.functionGenericArguments())[t.genericVariableIndex()]);
    }

    if (typeContext.calleeType().canHaveGenericArguments()) {
        while (t.unboxedType() == TypeType::GenericVariable  &&
               typeContext.calleeType().typeDefinition()->canResolve(t.resolutionConstraint())) {
            Type tn = typeContext.calleeType().genericArguments()[t.genericVariableIndex()];
            if (tn.unboxedType() == TypeType::GenericVariable
                && tn.genericVariableIndex() == t.genericVariableIndex()
                && tn.resolutionConstraint() == t.resolutionConstraint()) {
                break;
            }
            t = tn;
        }
    }

    if (t.type() != TypeType::Box) {
        for (auto &arg : t.genericArguments_) {
            arg = arg.resolveOn(typeContext);
        }
    }
    return keepStorage(std::move(t));
}

bool Type::identicalGenericArguments(Type to, const TypeContext &typeContext, GenericInferer *inf) const {
    for (size_t i = to.typeDefinition()->superGenericArguments().size(); i < to.genericArguments_.size(); i++) {
        if (!this->genericArguments_[i].identicalTo(to.genericArguments_[i], typeContext, inf)) {
            return false;
        }
    }
    return true;
}

std::optional<Type> Type::resolvedGenericVariable(const TypeContext &tc) const {
    auto resolved = resolveOnSuperArgumentsAndConstraints(tc);
    if (resolved.unboxedType() == type() && resolved.genericVariableIndex() == genericVariableIndex() &&
        (type() == TypeType::GenericVariable ? resolved.resolutionConstraint() == resolutionConstraint()
                                             : resolved.localResolutionConstraint() == localResolutionConstraint())) {
        return std::nullopt;
    }
    return resolved;
}

bool Type::compatibleToResolved(const Type &to, const TypeContext &tc, GenericInferer *inf) const {
    auto toResolved = to.resolvedGenericVariable(tc);
    return toResolved && compatibleTo(*toResolved, tc, inf);
}

bool Type::compatibleTo(const Type &to, const TypeContext &tc, GenericInferer *inf) const {
    if (type() == TypeType::Box) {
        return unboxed().compatibleTo(to, tc, inf);
    }
    if (to.type() == TypeType::Box) {
        return compatibleTo(to.unboxed(), tc, inf);
    }

    if (to.type() == TypeType::Something) {
        return true;
    }

    if (this->type() == TypeType::Optional) {
        if (to.type() != TypeType::Optional) {
            return false;
        }
        return optionalType().compatibleTo(to.optionalType(), tc, inf);
    }
    if (is<TypeType::NoValueLiteral>() && to.type() == TypeType::Optional) {
        return true;
    }
    if (this->type() != TypeType::Optional && to.type() == TypeType::Optional) {
        return compatibleTo(to.optionalType(), tc, inf);
    }

    if (is<TypeType::IntegerLiteral>() && to.is<TypeType::ValueType>() &&
            to.typeDefinition()->canInitFrom(*this)) {
        return true;
    }
    if (is<TypeType::RealLiteral>()) {
        if (to.is<TypeType::ValueType>() && to.valueType()->cRepresentation() &&
            to.valueType()->cRepresentation()->isFloat()) {
            return true;
        }
        return genericArguments_.front().compatibleTo(to, tc, inf);  // Otherwise the literal is a 💯.
    }
    if ((is<TypeType::ListLiteral>() || is<TypeType::DictionaryLiteral>()) &&
            to.is<TypeType::ValueType>() && to.typeDefinition()->canInitFrom(*this)) {
        return std::equal(genericArguments().begin(), genericArguments().end(),
                to.genericArguments().begin(), to.genericArguments().end(), [&](const Type &a, const Type &b) {
            return a.compatibleTo(b, tc, inf);
        });
    }

    if ((this->type() == TypeType::GenericVariable && to.type() == TypeType::GenericVariable) ||
        (this->type() == TypeType::LocalGenericVariable && to.type() == TypeType::LocalGenericVariable)) {
        // A variable of the calling code, e.g. its own generic parameter, can be inferred for one of the callee.
        if (to.type() == TypeType::GenericVariable && inf != nullptr && inf->inferringType()) {
            inf->addType(to.genericVariableIndex(), *this, tc);
            return true;
        }
        if (to.type() == TypeType::LocalGenericVariable && inf != nullptr && inf->inferringLocal()) {
            inf->addLocal(to.genericVariableIndex(), *this, tc);
            return true;
        }
        if (this->genericVariableIndex() == to.genericVariableIndex() && this->typeDefinition_ == to.typeDefinition_ &&
            this->localResolutionConstraint_ == to.localResolutionConstraint_) {
            return true;
        }
        auto resolved = resolvedGenericVariable(tc);
        auto toResolved = to.resolvedGenericVariable(tc);
        if (!resolved && !toResolved) {
            return false;
        }
        return resolved.value_or(*this).compatibleTo(toResolved.value_or(to), tc, inf);
    }
    if (type() == TypeType::GenericVariable || type() == TypeType::LocalGenericVariable) {
        if (inf != nullptr && (type() == TypeType::GenericVariable ? inf->inferringType() : inf->inferringLocal())) {
            return true;
        }
        auto resolved = resolvedGenericVariable(tc);
        return resolved && resolved->compatibleTo(to, tc, inf);
    }

    switch (to.type()) {
        case TypeType::TypeAsValue:
            return isCompatibleToTypeAsValue(to, tc, inf);
        case TypeType::GenericVariable:
            if (inf != nullptr && inf->inferringType()) {
                inf->addType(to.genericVariableIndex(), *this, tc);
                return true;
            }
            return compatibleToResolved(to, tc, inf);
        case TypeType::LocalGenericVariable:
            if (inf != nullptr && inf->inferringLocal()) {
                inf->addLocal(to.genericVariableIndex(), *this, tc);
                return true;
            }
            return compatibleToResolved(to, tc, inf);
        case TypeType::Class:
            return type() == TypeType::Class && klass()->inheritsFrom(to.klass()) &&
                identicalGenericArguments(to, tc, inf);
        case TypeType::ValueType:
            return type() == TypeType::ValueType && typeDefinition() == to.typeDefinition() &&
                identicalGenericArguments(to, tc, inf);
        case TypeType::Enum:
            return type() == TypeType::Enum && enumeration() == to.enumeration();
        case TypeType::Someobject:
            return type() == TypeType::Class || type() == TypeType::Someobject;
        case TypeType::MultiProtocol:
            return isCompatibleToMultiProtocol(to, tc, inf);
        case TypeType::Protocol:
            return isCompatibleToProtocol(to, tc, inf);
        case TypeType::Callable:
            return isCompatibleToCallable(to, tc, inf);
        case TypeType::NoReturn:
            return type() == TypeType::NoReturn;
        default:
            break;
    }
    return false;
}

bool Type::isCompatibleToTypeAsValue(const Type &to, const TypeContext &tc,
                                     GenericInferer *inf) const {
    if (type() != TypeType::TypeAsValue) {
        return false;
    }

    if (typeOfTypeValue().type() == TypeType::Class && to.typeOfTypeValue().type() == TypeType::Class) {
        return typeOfTypeValue().compatibleTo(to.typeOfTypeValue(), tc, inf);
    }

    return identicalTo(to, tc, inf);
}

bool Type::isCompatibleToMultiProtocol(const Type &to, const TypeContext &ct, GenericInferer *inf) const {
    if (type() == TypeType::MultiProtocol) {
        return std::equal(protocols().begin(), protocols().end(), to.protocols().begin(), to.protocols().end(),
                          [&](const Type &a, const Type &b) {
                              return a.compatibleTo(b, ct, inf);
                          });
    }

    return std::all_of(to.protocols().begin(), to.protocols().end(), [&](const Type &p) {
        return compatibleTo(p, ct);
    });
}

bool Type::isCompatibleToProtocol(const Type &to, const TypeContext &ct, GenericInferer *inf) const {
    if (type() == TypeType::Class) {
        for (Class *a = this->klass(); a != nullptr; a = a->superclass()) {
            for (auto &protocol : a->protocols()) {
                if (protocol.type->type().resolveOn(TypeContext(*this)).compatibleTo(to.resolveOn(ct), ct, inf)) {
                    return true;
                }
            }
        }
        return false;
    }
    if (type() == TypeType::ValueType || type() == TypeType::Enum) {
        for (auto &protocol : typeDefinition()->protocols()) {
            if (protocol.type->type().resolveOn(TypeContext(*this)).compatibleTo(to.resolveOn(ct), ct, inf)) {
                return true;
            }
        }
        return false;
    }
    if (type() == TypeType::Protocol) {
        return this->typeDefinition() == to.typeDefinition() && identicalGenericArguments(to, ct, inf);
    }
    if (type() == TypeType::MultiProtocol) {  // Reboxing takes the conformance to the protocol from the box.
        return std::any_of(protocols().begin(), protocols().end(), [&](const Type &protocol) {
            return protocol.isCompatibleToProtocol(to, ct, inf);
        });
    }
    return false;
}

bool Type::isCompatibleToCallable(const Type &to, const TypeContext &ct, GenericInferer *inf) const {
    if (type() == TypeType::Callable && cCallable_ == to.cCallable_) {
        if (returnType().compatibleTo(to.returnType(), ct, inf) &&
            errorType().compatibleTo(to.errorType(), ct, inf) && to.parametersCount() == parametersCount()) {
            for (size_t i = 0; i < to.parametersCount(); i++) {
                if (!to.parameters()[i].compatibleTo(parameters()[i], ct, inf)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

bool Type::identicalTo(Type to, const TypeContext &tc, GenericInferer *inf) const {
    if (inf != nullptr && inf->inferringLocal() && to.type() == TypeType::LocalGenericVariable) {
        inf->addLocal(to.genericVariableIndex(), *this, tc);
        return true;
    }
    if (inf != nullptr && inf->inferringType() && to.type() == TypeType::GenericVariable) {
        inf->addType(to.genericVariableIndex(), *this, tc);
        return true;
    }

    if (type() == TypeType::Box) {
        return unboxed().identicalTo(to, tc, inf);
    }
    if (to.type() == TypeType::Box) {
        return identicalTo(to.unboxed(), tc, inf);
    }

    if (type() == to.type()) {
        switch (type()) {
            case TypeType::Class:
            case TypeType::Protocol:
            case TypeType::ValueType:
                return typeDefinition() == to.typeDefinition() && identicalGenericArguments(to, tc, inf);
            case TypeType::Callable:
                return cCallable_ == to.cCallable_ && std::equal(to.genericArguments_.begin(), to.genericArguments_.end(),
                                  genericArguments_.begin(), genericArguments_.end(),
                                  [&tc, inf](const Type &a, const Type &b) { return a.identicalTo(b, tc, inf); });
            case TypeType::Optional:
            case TypeType::TypeAsValue:
                return genericArguments_[0].identicalTo(to.genericArguments_[0], tc, inf);
            case TypeType::Enum:
                return enumeration() == to.enumeration();
            case TypeType::GenericVariable:
            case TypeType::LocalGenericVariable:
                return genericVariableIndex() == to.genericVariableIndex();
            case TypeType::Something:
            case TypeType::Someobject:
            case TypeType::NoReturn:
                return true;
            case TypeType::MultiProtocol:
                return std::equal(protocols().begin(), protocols().end(), to.protocols().begin(), to.protocols().end(),
                                  [&tc, inf](const Type &a, const Type &b) { return a.identicalTo(b, tc, inf); });
            case TypeType::StorageExpectation:
            case TypeType::Box:
                return false;
        }
    }
    return false;
}

StorageType Type::storageType() const {
    switch (type()) {
        case TypeType::Box:
            return StorageType::Box;
        case TypeType::Optional:
            if (optionalType().type() == TypeType::Class || optionalType().type() == TypeType::Someobject ||
                optionalType().isCPointer() || optionalType().isCCallable()) {
                return StorageType::PointerOptional;
            }
            return StorageType::SimpleOptional;
        default:
            return StorageType::Simple;
    }
}

bool Type::isCPointer() const {
    if (type() != TypeType::ValueType || isReference()) {
        return false;
    }
    auto &representation = valueType()->cRepresentation();
    return representation && representation->isPointer();
}

bool Type::isCRepresentable() const {
    if (isReference()) {
        return false;
    }
    switch (type()) {
        case TypeType::ValueType:
            return valueType()->cRepresentation().has_value() || valueType()->isCStruct();
        case TypeType::Callable: {
            // Calls through C function pointers have no trampolines, so structs cannot be passed by value.
            auto passable = [](const Type &type) {
                return type.isCRepresentable() && !(type.type() == TypeType::ValueType && type.valueType()->isCStruct());
            };
            return cCallable_ && errorType().type() == TypeType::NoReturn &&
                (returnType().type() == TypeType::NoReturn || passable(returnType())) &&
                std::all_of(parameters(), parametersEnd(), passable);
        }
        case TypeType::Optional:
            return optionalType().isCPointer() || optionalType().isCCallable();
        default:
            return false;
    }
}

bool Type::isReferenceUseful() const {
    assert(type() != TypeType::StorageExpectation);
    return type() == TypeType::ValueType || type() == TypeType::Enum ||
        type() == TypeType::Box;
}

std::string Type::typePackage() const {
    switch (this->type()) {
        case TypeType::Class:
        case TypeType::ValueType:
        case TypeType::Protocol:
        case TypeType::Enum:
            return typeDefinition()->package()->name();
        case TypeType::StorageExpectation:
            throw std::logic_error("typePackage for StorageExpectation");
        default:
            return "";
    }
}

bool Type::containsGenericVariables() const {
    if (type() == TypeType::GenericVariable || type() == TypeType::LocalGenericVariable) {
        return true;
    }
    return std::any_of(genericArguments_.begin(), genericArguments_.end(), [](const Type &type) {
        return type.containsGenericVariables();
    });
}

Type Type::withMinimalBoxing() const {
    Type type = unboxed();
    if (type.type() == TypeType::Optional || type.canHaveGenericArguments()) {
        for (auto &argument : type.genericArguments_) {
            argument = argument.withMinimalBoxing();
        }
    }
    if (type.type() == TypeType::Optional) {
        return type.rewrapped(type.genericArguments_[0]);
    }
    return type.applyMinimalBoxing();
}

Type Type::withMinimallyBoxedGenericArguments() const {
    Type type = *this;
    if (type.type() == TypeType::Box || type.type() == TypeType::Optional) {
        type.genericArguments_[0] = type.genericArguments_[0].withMinimallyBoxedGenericArguments();
    }
    else if (type.type() == TypeType::Callable) {
        for (auto &argument : type.genericArguments_) {
            argument = argument.withMinimallyBoxedGenericArguments();
        }
    }
    else if (type.canHaveGenericArguments()) {
        for (auto &argument : type.genericArguments_) {
            argument = argument.withMinimalBoxing();
        }
    }
    return type;
}

bool Type::isCompileTimeOnly() const {
    switch (unboxedType()) {
        case TypeType::Invalid:
        case TypeType::StorageExpectation:
        case TypeType::IntegerLiteral:
        case TypeType::RealLiteral:
        case TypeType::ListLiteral:
        case TypeType::DictionaryLiteral:
        case TypeType::NoValueLiteral:
            return true;
        default:
            return false;
    }
}

bool Type::isManaged() const {
    return type() == TypeType::Class || type() == TypeType::Someobject || type() == TypeType::Box ||
        (type() == TypeType::Callable && !cCallable_) ||
        (type() == TypeType::ValueType && valueType()->isManaged()) ||
        (type() == TypeType::Optional && optionalType().isManaged());
}

bool Type::isMutable() const {
    return mutable_ || (type() == TypeType::ValueType && valueType()->isPrimitive());
}

void Type::typeName(Type type, const TypeContext &typeContext, std::string &string, Package *package) const {
    if (package != nullptr) {
        string.append(type.namespaceAccessor(package));
    }
    else {
        string.append(type.typePackage());
    }

    switch (type.type()) {
        case TypeType::Box:
            typeName(type.genericArguments_[0], typeContext, string, package);
            return;
        case TypeType::Optional:
            string.append("🍬");
            typeName(type.genericArguments_[0], typeContext, string, package);
            return;
        case TypeType::TypeAsValue:
            switch (type.typeOfTypeValue().type()) {
                case TypeType::Class:
                    string.append("🐇");
                    break;
                case TypeType::ValueType:
                    string.append("🕊");
                    break;
                case TypeType::Enum:
                    string.append("🦃");
                    break;
                case TypeType::Protocol:
                    string.append("🐊");
                    break;
                default:
                    break;
            }
            typeName(type.typeOfTypeValue(), typeContext, string, package);
            return;
        case TypeType::Class:
        case TypeType::Protocol:
        case TypeType::Enum:
        case TypeType::ValueType:
            string.append(utf8(type.typeDefinition()->name()));
            break;
        case TypeType::MultiProtocol:
            string.append("🍱");
            for (auto &protocol : type.protocols()) {
                typeName(protocol, typeContext, string, package);
            }
            string.append("🍱");
            return;
        case TypeType::NoReturn:
            string.append("◼️");
            return;
        case TypeType::Something:
            string.append("⚪️");
            return;
        case TypeType::Someobject:
            string.append("🔵");
            return;
        case TypeType::Callable:
            string.append(type.cCallable_ ? "🍇🎍🌊" : "🍇");

            for (auto it = type.parameters(); it < type.parametersEnd(); it++) {
                typeName(*it, typeContext, string, package);
            }

            if (type.returnType().type() != TypeType::NoReturn) {
                string.append("➡️");
                typeName(type.returnType(), typeContext, string, package);
            }

            if (type.errorType().type() != TypeType::NoReturn) {
                string.append("🚧");
                typeName(type.errorType(), typeContext, string, package);
            }

            string.append("🍉");
            return;
        case TypeType::GenericVariable:
            if (typeContext.calleeType().canHaveGenericArguments()) {
                auto str = typeContext.calleeType().typeDefinition()->findGenericName(type.genericVariableIndex());
                string.append(utf8(str));
            }
            else if (typeContext.calleeType().type() == TypeType::TypeAsValue) {
                auto callee = typeContext.calleeType().typeOfTypeValue();
                auto str = callee.typeDefinition()->findGenericName(type.genericVariableIndex());
                string.append(utf8(str));
            }
            else if (auto name = type.typeDefinition_ != nullptr ?
                     type.typeDefinition_->declaredGenericName(type.genericVariableIndex()) : nullptr) {
                // Without a context, e.g. in a declaration, the variable has the name its type gave it.
                string.append(utf8(*name));
            }
            else {
                string.append("T" + std::to_string(type.genericVariableIndex()) + "?");
            }
            return;
        case TypeType::LocalGenericVariable:
            if (typeContext.function() == type.localResolutionConstraint_) {
                string.append(utf8(typeContext.function()->findGenericName(type.genericVariableIndex())));
            }
            else if (auto name = type.localResolutionConstraint_ != nullptr ?
                     type.localResolutionConstraint_->declaredGenericName(type.genericVariableIndex()) : nullptr) {
                string.append(utf8(*name));
            }
            else {
                string.append("L" + std::to_string(type.genericVariableIndex()) + "?");
            }
            return;
        case TypeType::StorageExpectation:
            return;
    }

    if (type.canHaveGenericArguments()) {
        for (size_t i = type.typeDefinition()->superGenericArguments().size(); i < type.genericArguments().size(); i++) {
            string.append("🐚");
            typeName(type.genericArguments()[i], typeContext, string, package);
        }
    }
}

std::string Type::toString(const TypeContext &typeContext, Package *package) const {
    std::string string;
    typeName(*this, typeContext, string, package);
    return string;
}

std::string Type::namespaceAccessor(Package *package) const {
    auto ns = package->findNamespace(*this);
    if (!ns.empty()) {
        return "🔶" + utf8(ns);
    }
    return "";
}

}  // namespace EmojicodeCompiler
